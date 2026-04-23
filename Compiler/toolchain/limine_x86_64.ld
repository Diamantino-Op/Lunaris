ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text : ALIGN(16)
    {
        KEEP(*(.limine_requests))
        *(.text .text.*)
    }

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(16)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(16)
    {
        *(COMMON)
        *(.bss .bss.*)
    }
}