OUTPUT_ARCH(i386:x86-64)

SECTIONS
{
    . = 0;
    .text : { *(.text) }
}

ENTRY(realmode_entry)
