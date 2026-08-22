/*
 * CHGame flash/RAM map — SINGLE SOURCE OF TRUTH.
 *
 * Anything that needs these numbers (bootloader, linker scripts, image builder,
 * host uploader, Web Serial page) derives them from here. tools/gen_map.py
 * emits the linker-script and Python/JS copies so the numbers cannot drift.
 *
 *   0x0000 +---------------------------+
 *          | bootloader       12 KB    |
 *   0x3000 +---------------------------+ CHGAME_APP_START
 *          | application    50944 B    |
 *   0xF700 +---------------------------+ CHGAME_META_ADDR
 *          | app metadata    256 B     |  (one flash page, written LAST)
 *   0xF800 +---------------------------+ end of 62 KB user flash
 */
#ifndef CHGAME_MAP_H
#define CHGAME_MAP_H

/* --- Flash geometry (CH32X035G8U6) --------------------------------------- */
#define CHGAME_FLASH_BASE     0x00000000u
#define CHGAME_FLASH_SIZE     0x0000F800u   /* 62 KB user flash            */
#define CHGAME_PAGE_SIZE      256u          /* fast erase/program granule  */

/* --- Regions -------------------------------------------------------------- */
#define CHGAME_BOOT_START     CHGAME_FLASH_BASE
#define CHGAME_BOOT_SIZE      0x00003000u   /* 12 KB reservation           */

#define CHGAME_APP_START      (CHGAME_BOOT_START + CHGAME_BOOT_SIZE)   /* 0x2000 */
#define CHGAME_META_ADDR      (CHGAME_FLASH_BASE + CHGAME_FLASH_SIZE - CHGAME_PAGE_SIZE) /* 0xF700 */
#define CHGAME_APP_MAX_SIZE   (CHGAME_META_ADDR - CHGAME_APP_START)    /* 50944  */
#define CHGAME_APP_END        CHGAME_META_ADDR                          /* exclusive */

/* --- SRAM ----------------------------------------------------------------- */
#define CHGAME_RAM_BASE       0x20000000u
#define CHGAME_RAM_SIZE       0x00005000u   /* 20 KB */
/* First 16 bytes are carved out for the retained boot-request block, at the
   same address in BOTH the bootloader and the application linker scripts. */
#define CHGAME_MAGIC_ADDR     CHGAME_RAM_BASE
#define CHGAME_MAGIC_SIZE     16u

/* --- Boot request marker --------------------------------------------------- */
/* Stored as {magic, ~magic} so uninitialised SRAM cannot forge a request. */
#define CHGAME_BOOT_MAGIC     0x43484742u   /* "CHGB" */

/* --- Application metadata (one flash page at CHGAME_META_ADDR) ------------- */
#define CHGAME_META_MAGIC     0x4D474843u   /* "CHGM" */
#define CHGAME_META_VERSION   1u

#ifndef __ASSEMBLER__
#include <stdint.h>

typedef struct {
    uint32_t magic;        /* CHGAME_META_MAGIC                              */
    uint32_t meta_version; /* CHGAME_META_VERSION                            */
    uint32_t length;       /* application image length in bytes              */
    uint32_t crc32;        /* CRC-32/ISO-HDLC over the image                 */
    uint32_t app_version;  /* free for the application, 0 if unused          */
    uint32_t reserved[3];
    /* remainder of the page is 0xFF */
} chgame_meta_t;

typedef struct {
    uint32_t magic;        /* CHGAME_BOOT_MAGIC                              */
    uint32_t inverse;      /* ~CHGAME_BOOT_MAGIC                             */
    /* Counts how many times the bootloader has run without a power cycle.
     * Survives NVIC_SystemReset() but not power loss, which makes it both a
     * boot-loop detector and the test for whether SRAM retention across a warm
     * reset is a safe basis for the boot-request marker at all. */
    uint32_t boot_count;
    uint32_t boot_count_inv;
} chgame_bootreq_t;

_Static_assert(sizeof(chgame_meta_t) <= CHGAME_PAGE_SIZE, "metadata exceeds one flash page");
_Static_assert(sizeof(chgame_bootreq_t) <= CHGAME_MAGIC_SIZE, "boot request block too large");
_Static_assert(CHGAME_APP_MAX_SIZE == 50944u, "app region size changed unexpectedly");
_Static_assert(CHGAME_APP_START % CHGAME_PAGE_SIZE == 0, "APP_START must be page aligned");
_Static_assert(CHGAME_META_ADDR % CHGAME_PAGE_SIZE == 0, "META_ADDR must be page aligned");
#endif /* __ASSEMBLER__ */

#endif /* CHGAME_MAP_H */
