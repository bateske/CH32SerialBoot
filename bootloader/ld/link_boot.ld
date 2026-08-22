/* CHGame bootloader link script.
 * Numbers mirror bootloader/src/chgame_map.h — keep them in sync.
 *   FLASH 0x00000000 + 8K   (bootloader reservation)
 *   MAGIC 0x20000000 + 16   (retained boot-request block, NOLOAD)
 *   RAM   0x20000010 + 20K-16
 */
ENTRY( _start )

__stack_size = 2048;
PROVIDE( _stack_size = __stack_size );

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 12288
    MAGIC (rw)  : ORIGIN = 0x20000000, LENGTH = 16
    RAM   (xrw) : ORIGIN = 0x20000010, LENGTH = 20464
}

SECTIONS
{
    .init :
    {
        _sinit = .;
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init)))
        . = ALIGN(4);
        _einit = .;
    } >FLASH AT>FLASH

    .vector :
    {
        *(.vector);
        . = ALIGN(64);
    } >FLASH AT>FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } >FLASH AT>FLASH

    .fini :
    {
        KEEP(*(SORT_NONE(.fini)))
        . = ALIGN(4);
    } >FLASH AT>FLASH

    PROVIDE( _etext = . );

    .preinit_array :
    {
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP (*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);
    } >FLASH AT>FLASH

    .init_array :
    {
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
        KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
        PROVIDE_HIDDEN (__init_array_end = .);
    } >FLASH AT>FLASH

    .fini_array :
    {
        PROVIDE_HIDDEN (__fini_array_start = .);
        KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
        KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
        PROVIDE_HIDDEN (__fini_array_end = .);
    } >FLASH AT>FLASH

    /* Retained across NVIC_SystemReset(); never loaded, never zeroed. */
    .boot_magic (NOLOAD) :
    {
        KEEP(*(.boot_magic))
    } >MAGIC

    /* Flash erase/program routines execute from SRAM: the CH32X035 stalls
       instruction fetch while the flash controller is busy, so running the
       writer from flash can hang the core. Copied by ramfunc_init(). */
    .ramfunc :
    {
        . = ALIGN(4);
        PROVIDE( _ramfunc_vma = . );
        *(.ramfunc)
        *(.ramfunc.*)
        . = ALIGN(4);
        PROVIDE( _ramfunc_vma_end = . );
    } >RAM AT>FLASH
    PROVIDE( _ramfunc_lma = LOADADDR(.ramfunc) );

    .dalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_vma = .);
    } >RAM AT>FLASH

    .dlalign :
    {
        . = ALIGN(4);
        PROVIDE(_data_lma = .);
    } >FLASH AT>FLASH

    .data :
    {
        *(.gnu.linkonce.r.*)
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        PROVIDE( __global_pointer$ = . + 0x800 );
        *(.sdata .sdata.*)
        *(.sdata2.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        *(.srodata.cst16)
        *(.srodata.cst8)
        *(.srodata.cst4)
        *(.srodata.cst2)
        *(.srodata .srodata.*)
        . = ALIGN(4);
        PROVIDE( _edata = .);
    } >RAM AT>FLASH

    .bss :
    {
        . = ALIGN(4);
        PROVIDE( _sbss = .);
        *(.sbss*)
        *(.gnu.linkonce.sb.*)
        *(.bss*)
        *(.gnu.linkonce.b.*)
        *(COMMON*)
        . = ALIGN(4);
        PROVIDE( _ebss = .);
    } >RAM AT>FLASH

    PROVIDE( _end = _ebss);
    PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        PROVIDE( _heap_end = . );
        . = ALIGN(4);
        PROVIDE(_susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = .);
    } >RAM

    /* Hard guarantee: the bootloader may never spill into the application. */
    ASSERT( _etext <= 0x3000, "BOOTLOADER OVERFLOW: code+rodata would spill into the application region" )
}
