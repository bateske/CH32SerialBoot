#include "proto.h"
#include "crc16.h"
#include "chgame_map.h"
#include "appmeta.h"
#include "flash.h"
#include "crc32.h"
#include "jump.h"
#include "led.h"
#include "spin.h"
#include "sys.h"
#include "wch_usbcdc_internal.h"
#include "ch32x035.h"

/* Bench instrumentation for the multi-packet receive defect. Reported by
 * STATUS so the host can see whether the driver delivered the bytes at all,
 * which distinguishes "driver dropped the packet" from "parser mis-framed it". */
uint32_t proto_rx_bytes;
uint32_t proto_rx_calls;

/* ---- receive state machine ------------------------------------------------
 * Byte-at-a-time and resynchronising by construction: anything that is not a
 * well-formed frame eventually drops us back to hunting for 'C','G'. A host
 * that disconnects mid-frame, a line of terminal noise, or a truncated packet
 * all recover without the bootloader needing a reset.
 */
typedef enum {
    RX_SOF0 = 0, RX_SOF1, RX_VER, RX_CMD,
    RX_LEN_LO, RX_LEN_HI, RX_PAYLOAD, RX_CRC_LO, RX_CRC_HI
} rx_state_t;

static rx_state_t rx_state;
static uint32_t   rx_last_tick;   /* stamp of the last byte, for the frame timeout */
static uint8_t    rx_cmd;
static uint16_t   rx_len, rx_got, rx_crc_rx;
static uint8_t    rx_buf[PROTO_MAX_PAYLOAD];

/* ---- transmit -------------------------------------------------------------- */

/* Bounded write: a host that stops draining must not wedge the bootloader.
 *
 * The bound is a wall-clock deadline rather than a spin count, because a spin
 * count silently becomes shorter as the code gets faster, and a dropped
 * response looks exactly like a dead device from the host's side. 500 ms is far
 * longer than any healthy USB transfer and short enough that a genuinely absent
 * host does not wedge the update loop. */
static void tx_byte(uint8_t b)
{
    uint32_t start = sys_ticks();
    while (!CDC_write_nb((char)b)) {
        if (sys_elapsed_ms(start) > 500u) return;   /* host is gone; drop the frame */
    }
}

static void send_frame(uint8_t cmd, const uint8_t *payload, uint16_t len)
{
    uint8_t  hdr[4];
    uint16_t crc;

    hdr[0] = PROTO_VERSION;
    hdr[1] = (uint8_t)(cmd | PROTO_RESPONSE_BIT);
    hdr[2] = (uint8_t)(len & 0xFFu);
    hdr[3] = (uint8_t)(len >> 8);

    crc = crc16_update(0xFFFFu, hdr, sizeof(hdr));
    if (len) crc = crc16_update(crc, payload, len);

    tx_byte(PROTO_SOF0);
    tx_byte(PROTO_SOF1);
    for (unsigned i = 0; i < sizeof(hdr); i++) tx_byte(hdr[i]);
    for (uint16_t i = 0; i < len; i++)         tx_byte(payload[i]);
    tx_byte((uint8_t)(crc & 0xFFu));
    tx_byte((uint8_t)(crc >> 8));
    CDC_flush();
}

static void send_status(uint8_t cmd, uint8_t status)
{
    send_frame(cmd, &status, 1);
}

/* ---- little-endian payload builders ---------------------------------------- */

static uint16_t put_u8 (uint8_t *b, uint16_t o, uint8_t v)  { b[o] = v; return o + 1; }
static uint16_t put_u16(uint8_t *b, uint16_t o, uint16_t v)
{
    b[o] = (uint8_t)v; b[o+1] = (uint8_t)(v >> 8); return o + 2;
}
static uint16_t put_u32(uint8_t *b, uint16_t o, uint32_t v)
{
    b[o] = (uint8_t)v;         b[o+1] = (uint8_t)(v >> 8);
    b[o+2] = (uint8_t)(v >> 16); b[o+3] = (uint8_t)(v >> 24);
    return o + 4;
}

/* ---- little-endian payload readers ------------------------------------------ */

static uint32_t get_u32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t get_u16(const uint8_t *b)
{
    return (uint16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
}

/* ---- update transaction ------------------------------------------------------
 * Writes must arrive strictly sequentially. That is not a limitation worth
 * relaxing: it removes any need to track which parts of the region have been
 * written, makes "bytes accepted" an exact resume point, and means a host bug
 * that skips or repeats a chunk is caught immediately rather than producing an
 * image that fails CRC for no visible reason.
 */
static struct {
    uint8_t  active;
    uint32_t size;        /* image length announced at BEGIN        */
    uint32_t crc32;       /* image CRC announced at BEGIN           */
    uint32_t written;     /* bytes accepted so far                  */
    uint32_t page_addr;   /* flash address of the page being filled */
    uint16_t page_fill;   /* bytes staged in page_buf               */
    uint8_t  page_buf[CHGAME_PAGE_SIZE];
} tx;

static void tx_reset(void)
{
    tx.active = 0; tx.size = 0; tx.crc32 = 0;
    tx.written = 0; tx.page_addr = CHGAME_APP_START; tx.page_fill = 0;
}

static void page_buf_clear(void)
{
    for (uint32_t i = 0; i < CHGAME_PAGE_SIZE; i++)
        tx.page_buf[i] = 0xFFu;      /* pad short pages with the erased value */
}

/* Commit the staged page. Returns a protocol status. */
static uint8_t page_commit(void)
{
    int rc;
    if (tx.page_fill == 0u)
        return ST_OK;
    rc = flash_write_page(tx.page_addr, tx.page_buf, FLASH_REGION_APP);
    if (rc != FLASH_OK)
        return (rc == FLASH_ERR_RANGE) ? ST_ERR_RANGE : ST_ERR_FLASH;
    tx.page_addr += CHGAME_PAGE_SIZE;
    tx.page_fill  = 0;
    page_buf_clear();
    return ST_OK;
}

/* ---- commands --------------------------------------------------------------- */

static void do_begin(const uint8_t *p, uint16_t len)
{
    uint32_t size, crc, erase_len;

    if (len < 8u) { send_status(CMD_BEGIN, ST_ERR_SIZE); return; }

    size = get_u32(p);
    crc  = get_u32(p + 4);

    /* Word-multiple keeps the CRC span and the metadata length unambiguous. */
    if (size == 0u || size > CHGAME_APP_MAX_SIZE || (size & 3u)) {
        send_status(CMD_BEGIN, ST_ERR_SIZE);
        return;
    }

    led_set(LED_BUSY);
    tx_reset();

    /* Metadata FIRST. From this instant the application is marked invalid, so a
       power loss anywhere in the rest of the update leaves the bootloader in
       charge rather than a half-written image looking launchable. */
    if (flash_erase_page(CHGAME_META_ADDR, FLASH_REGION_APP) != FLASH_OK) {
        send_status(CMD_BEGIN, ST_ERR_FLASH);
        led_set(LED_WAIT_UPDATE);
        return;
    }

    erase_len = ((size + CHGAME_PAGE_SIZE - 1u) / CHGAME_PAGE_SIZE) * CHGAME_PAGE_SIZE;
    if (flash_erase_range(CHGAME_APP_START, erase_len, FLASH_REGION_APP) != FLASH_OK) {
        send_status(CMD_BEGIN, ST_ERR_FLASH);
        led_set(LED_WAIT_UPDATE);
        return;
    }

    tx.active = 1;
    tx.size   = size;
    tx.crc32  = crc;
    page_buf_clear();

    send_status(CMD_BEGIN, ST_OK);
}

static void do_write(const uint8_t *p, uint16_t len)
{
    uint32_t offset;
    uint16_t dlen;
    const uint8_t *data;

    if (!tx.active)  { send_status(CMD_WRITE, ST_ERR_STATE); return; }
    if (len < 4u)    { send_status(CMD_WRITE, ST_ERR_SIZE);  return; }

    offset = get_u32(p);
    data   = p + 4;
    dlen   = (uint16_t)(len - 4u);

    if (offset != tx.written) { send_status(CMD_WRITE, ST_ERR_STATE); return; }
    /* Expressed as a subtraction from the remaining count so it cannot wrap. */
    if (dlen > tx.size - tx.written) { send_status(CMD_WRITE, ST_ERR_RANGE); return; }

    while (dlen--) {
        tx.page_buf[tx.page_fill++] = *data++;
        tx.written++;
        if (tx.page_fill >= CHGAME_PAGE_SIZE) {
            uint8_t st = page_commit();
            if (st != ST_OK) { tx_reset(); send_status(CMD_WRITE, st); return; }
        }
    }

    send_status(CMD_WRITE, ST_OK);
}

static void do_end(void)
{
    uint8_t  st, meta_page[CHGAME_PAGE_SIZE];
    uint32_t crc;
    chgame_meta_t m;

    if (!tx.active)             { send_status(CMD_END, ST_ERR_STATE); return; }
    if (tx.written != tx.size)  { send_status(CMD_END, ST_ERR_SIZE);  return; }

    st = page_commit();
    if (st != ST_OK) { tx_reset(); send_status(CMD_END, st); return; }

    /* Verify from FLASH, not from anything we buffered. The only claim worth
       making is about what is actually stored. */
    crc = crc32_buf((const void *)CHGAME_APP_START, tx.size);
    if (crc != tx.crc32) {
        tx_reset();
        send_status(CMD_END, ST_ERR_CRC);
        led_set(LED_WAIT_UPDATE);
        return;
    }

    m.magic        = CHGAME_META_MAGIC;
    m.meta_version = CHGAME_META_VERSION;
    m.length       = tx.size;
    m.crc32        = tx.crc32;
    m.app_version  = 0u;
    m.reserved[0] = m.reserved[1] = m.reserved[2] = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < CHGAME_PAGE_SIZE; i++) meta_page[i] = 0xFFu;
    for (uint32_t i = 0; i < sizeof(m); i++)        meta_page[i] = ((const uint8_t *)&m)[i];

    if (flash_write_page(CHGAME_META_ADDR, meta_page, FLASH_REGION_APP) != FLASH_OK) {
        tx_reset();
        send_status(CMD_END, ST_ERR_FLASH);
        led_set(LED_WAIT_UPDATE);
        return;
    }

    tx_reset();
    led_set(LED_WAIT_UPDATE);

    /* Re-read through the same path the boot decision uses, so a successful END
       means exactly "the next boot will launch this". */
    send_status(CMD_END, appmeta_check() == APP_VALID ? ST_OK : ST_ERR_CRC);
}

static void do_abort(void)
{
    /* Erase the metadata so a partially written image can never be launched. */
    flash_erase_page(CHGAME_META_ADDR, FLASH_REGION_APP);
    tx_reset();
    led_set(LED_WAIT_UPDATE);
    send_status(CMD_ABORT, ST_OK);
}

static void do_read(const uint8_t *p, uint16_t len)
{
    uint8_t  out[PROTO_MAX_PAYLOAD];
    uint32_t offset;
    uint16_t rlen;

    if (len < 6u) { send_status(CMD_READ, ST_ERR_SIZE); return; }

    offset = get_u32(p);
    rlen   = get_u16(p + 4);

    if (rlen == 0u || rlen > PROTO_MAX_PAYLOAD - 1u) { send_status(CMD_READ, ST_ERR_SIZE); return; }
    if (offset > CHGAME_FLASH_SIZE ||
        rlen > CHGAME_FLASH_SIZE - offset) { send_status(CMD_READ, ST_ERR_RANGE); return; }

    out[0] = ST_OK;
    for (uint16_t i = 0; i < rlen; i++)
        out[1 + i] = *(const volatile uint8_t *)(offset + i);

    send_frame(CMD_READ, out, (uint16_t)(rlen + 1u));
}

static void do_hello(void)
{
    uint8_t  p[40];
    uint16_t o = 0;
    const volatile uint32_t *uid = (const volatile uint32_t *)CH32X035_ESIG_UNIID1;

    o = put_u8 (p, o, ST_OK);
    o = put_u8 (p, o, PROTO_VERSION);
    o = put_u8 (p, o, MODE_BOOTLOADER);
    o = put_u8 (p, o, (uint8_t)appmeta_check());
    o = put_u16(p, o, BOOT_VERSION);
    o = put_u32(p, o, CHGAME_APP_START);
    o = put_u32(p, o, CHGAME_APP_MAX_SIZE);
    o = put_u16(p, o, (uint16_t)CHGAME_PAGE_SIZE);
    o = put_u16(p, o, (uint16_t)PROTO_MAX_PAYLOAD);
    o = put_u32(p, o, uid[0]);
    o = put_u32(p, o, uid[1]);
    o = put_u32(p, o, uid[2]);

    send_frame(CMD_HELLO, p, o);
}

static void do_status(void)
{
    uint8_t  p[32];
    uint16_t o = 0;

    o = put_u8 (p, o, ST_OK);
    o = put_u8 (p, o, MODE_BOOTLOADER);
    o = put_u8 (p, o, (uint8_t)appmeta_check());
    o = put_u32(p, o, tx.written);
    o = put_u32(p, o, tx.active ? tx.size : 0u);
    o = put_u32(p, o, proto_rx_bytes);
    o = put_u8 (p, o, (uint8_t)rx_state);
    o = put_u32(p, o, sys_ticks());
    o = put_u8 (p, o, (uint8_t)sys_counts_down());

    send_frame(CMD_STATUS, p, o);
}

static void do_run(void)
{
    if (appmeta_check() != APP_VALID) {
        send_status(CMD_RUN, ST_ERR_CRC);
        return;
    }
    send_status(CMD_RUN, ST_OK);

    /* Let the acknowledgement reach the host before the USB device vanishes.
       Busy-loop, because SysTick is about to be torn down anyway. */
    spin_ms(60);

    led_set(LED_OFF);
    jump_to_app();
}

/* ---- developer self-update ---------------------------------------------------
 * Two-step by design. The new bootloader image is first staged through the
 * ORDINARY upload path into the application region, so it gets the same bounds
 * checking, the same per-page verify and the same CRC treatment as any firmware.
 * Only then does DEV_WRITE_BOOT promote it into the boot region.
 *
 * Staging in flash rather than buffering the image in SRAM keeps the RAM cost at
 * one page instead of the whole reservation, and means the image has already
 * been read back and verified before anything irreversible happens.
 */
#if CHGAME_ALLOW_SELFUPDATE
static uint8_t dev_unlocked;

static void do_dev_unlock(const uint8_t *p, uint16_t len)
{
    if (len < 4u) { send_status(CMD_DEV_UNLOCK, ST_ERR_SIZE); return; }
    if (get_u32(p) != CHGAME_DEV_KEY) {
        dev_unlocked = 0;
        send_status(CMD_DEV_UNLOCK, ST_ERR_LOCKED);
        return;
    }
    dev_unlocked = 1;
    send_status(CMD_DEV_UNLOCK, ST_OK);
}

static void do_dev_write_boot(const uint8_t *p, uint16_t len)
{
    uint32_t size, crc;

    if (!dev_unlocked)  { send_status(CMD_DEV_WRITE_BOOT, ST_ERR_LOCKED); return; }
    if (tx.active)      { send_status(CMD_DEV_WRITE_BOOT, ST_ERR_STATE);  return; }
    if (len < 8u)       { send_status(CMD_DEV_WRITE_BOOT, ST_ERR_SIZE);   return; }

    size = get_u32(p);
    crc  = get_u32(p + 4);

    if (size == 0u || size > CHGAME_BOOT_SIZE || (size & 3u)) {
        send_status(CMD_DEV_WRITE_BOOT, ST_ERR_SIZE);
        return;
    }

    /* Verify the staged image from flash BEFORE erasing anything. Past this
       point the device has no working bootloader until the copy completes. */
    if (crc32_buf((const void *)CHGAME_APP_START, size) != crc) {
        send_status(CMD_DEV_WRITE_BOOT, ST_ERR_CRC);
        return;
    }

    /* Drop the application metadata first. The staged bytes are a bootloader
       image linked for address 0, so launching them as an application would run
       code at the wrong address. If power is lost between here and the reset,
       the device comes up with no valid application - which is the safe
       outcome, because it means whatever bootloader survives stays in charge. */
    if (flash_erase_page(CHGAME_META_ADDR, FLASH_REGION_APP) != FLASH_OK) {
        send_status(CMD_DEV_WRITE_BOOT, ST_ERR_FLASH);
        return;
    }

    /* Acknowledge while we still can - the reply cannot be sent afterwards. */
    send_status(CMD_DEV_WRITE_BOOT, ST_OK);
    CDC_flush();
    spin_ms(60);

    led_set(LED_BUSY);
    proto_shutdown();               /* detach cleanly; the host expects a drop */

    flash_selfupdate(CHGAME_APP_START, size);   /* never returns */
}
#endif /* CHGAME_ALLOW_SELFUPDATE */

static void dispatch(uint8_t cmd)
{
    switch (cmd) {
        case CMD_HELLO:  do_hello();  break;
        case CMD_STATUS: do_status(); break;
        case CMD_RUN:    do_run();    break;
        case CMD_BEGIN:  do_begin(rx_buf, rx_len); break;
        case CMD_WRITE:  do_write(rx_buf, rx_len); break;
        case CMD_END:    do_end();    break;
        case CMD_ABORT:  do_abort();  break;
        case CMD_READ:   do_read(rx_buf, rx_len);  break;

#if CHGAME_ALLOW_SELFUPDATE
        case CMD_DEV_UNLOCK:     do_dev_unlock(rx_buf, rx_len);     break;
        case CMD_DEV_WRITE_BOOT: do_dev_write_boot(rx_buf, rx_len); break;
#else
        /* Compiled out for production: report locked rather than unknown, so a
           developer tool gets a meaningful answer instead of a puzzle. */
        case CMD_DEV_UNLOCK: case CMD_DEV_WRITE_BOOT:
            send_status(cmd, ST_ERR_LOCKED); break;
#endif

        default:
            send_status(cmd, ST_ERR_BADCMD); break;
    }
}


/* ---- receive pump ----------------------------------------------------------- */

static void rx_byte(uint8_t b)
{
    rx_last_tick = sys_ticks();

    switch (rx_state) {
    case RX_SOF0:
        if (b == PROTO_SOF0) rx_state = RX_SOF1;
        break;

    case RX_SOF1:
        /* A repeated 'C' is a plausible start of the real frame, so stay here
           rather than dropping back and losing it. */
        if      (b == PROTO_SOF1) rx_state = RX_VER;
        else if (b != PROTO_SOF0) rx_state = RX_SOF0;
        break;

    case RX_VER:
        if (b != PROTO_VERSION) { rx_state = RX_SOF0; break; }
        rx_state = RX_CMD;
        break;

    case RX_CMD:
        rx_cmd = b; rx_state = RX_LEN_LO;
        break;

    case RX_LEN_LO:
        rx_len = b; rx_state = RX_LEN_HI;
        break;

    case RX_LEN_HI:
        rx_len |= (uint16_t)b << 8;
        if (rx_len > PROTO_MAX_PAYLOAD) {   /* refuse before allocating anything */
            send_status(rx_cmd, ST_ERR_SIZE);
            rx_state = RX_SOF0;
            break;
        }
        rx_got   = 0;
        rx_state = rx_len ? RX_PAYLOAD : RX_CRC_LO;
        break;

    case RX_PAYLOAD:
        rx_buf[rx_got++] = b;
        if (rx_got >= rx_len) rx_state = RX_CRC_LO;
        break;

    case RX_CRC_LO:
        rx_crc_rx = b; rx_state = RX_CRC_HI;
        break;

    case RX_CRC_HI: {
        uint8_t  hdr[4];
        uint16_t crc;

        rx_crc_rx |= (uint16_t)b << 8;

        hdr[0] = PROTO_VERSION;
        hdr[1] = rx_cmd;
        hdr[2] = (uint8_t)(rx_len & 0xFFu);
        hdr[3] = (uint8_t)(rx_len >> 8);

        crc = crc16_update(0xFFFFu, hdr, sizeof(hdr));
        if (rx_len) crc = crc16_update(crc, rx_buf, rx_len);

        if (crc == rx_crc_rx) dispatch(rx_cmd);
        else                  send_status(rx_cmd, ST_ERR_FRAME);

        rx_state = RX_SOF0;
        break;
    }
    }
}

static uint8_t usb_started;


void proto_init(void)
{
    rx_state     = RX_SOF0;
    rx_last_tick = sys_ticks();
    CDC_init();
    usb_started = 1;
}

void proto_shutdown(void)
{
    if (!usb_started)
        return;

    USBFSD->INT_EN   = 0;
    USBFSD->INT_FG   = 0xFF;
    USBFSD->BASE_CTRL = 0;          /* drops the D+ pull-up: host sees a detach */
    USBFSD->UDEV_CTRL = 0;

    /* Release the PHY and stop clocking the peripheral. */
    AFIO->CTLR &= ~(uint32_t)(AFIO_CTLR_USB_IOEN | AFIO_CTLR_UDP_PUE | AFIO_CTLR_UDM_PUE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_USBFS, DISABLE);

    /* Give the host time to notice the disconnect before the application
       potentially re-attaches. Busy-loop: SysTick is about to be torn down. */
    spin_ms(30);

    usb_started = 0;
}

void proto_task(void)
{
    int16_t b;
    /* Drain what is buffered, but bounded, so LED and other housekeeping still
       get serviced under a continuous inbound stream. */
    for (int i = 0; i < 64; i++) {
        b = CDC_read_nb();
        if (b < 0) break;
        proto_rx_bytes++;
        rx_byte((uint8_t)b);
    }
    proto_rx_calls++;

    /* Abandon a frame that stalled part-way through. Silent by design: this is
       recovery, not an error the host needs told about, and answering here
       would just add a spurious frame to whatever the host does next. */
    if (rx_state != RX_SOF0 && sys_elapsed_ms(rx_last_tick) > PROTO_RX_TIMEOUT_MS)
        rx_state = RX_SOF0;
}
