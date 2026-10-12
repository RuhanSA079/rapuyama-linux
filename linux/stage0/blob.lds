/* flat link for the position-independent stage0 blob. base 0 is fine since
 * the code is pic (no absolute refs), the loader copies .text wherever it got
 * executable kernel memory and branches to offset 0. only .text is kept and
 * everything else dropped so objcopy -O binary gives pure code */
ENTRY(blob_entry)
SECTIONS
{
    . = 0x0;
    .text : {
        *(.text.boot)
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
    }
    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.comment)
        *(.note.*)
        *(.data*)
        *(.bss*)
    }
}