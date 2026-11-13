OUTPUT_ARCH(aarch64)
ENTRY(_start)

SECTIONS
{
	. = 0x40000000;			/* qemu -M virt RAM base */

	.text : ALIGN(4096) {
		KEEP(*(.text.boot))
		*(.text .text.*)
	}

	.rodata : ALIGN(4096) {
		*(.rodata .rodata.*)
	}

	/*
	 * Fault recovery ranges recorded by COPYIO_RECOVER_RANGE.  In the
	 * kernel each entry is an offset from _copyio_recover_table; here the
	 * base is resolved to 0 so the entries are absolute addresses.
	 */
	.copyio_vectors : ALIGN(8) {
		__copyio_vectors_start = .;
		KEEP(*(.copyio_vectors))
		__copyio_vectors_end = .;
	}

	.data : ALIGN(4096) {
		*(.data .data.*)
	}

	.bss : ALIGN(4096) {
		__bss_start = .;
		*(.bss .bss.*)
		*(COMMON)
		. = ALIGN(4096);
		__bss_end = .;
	}

	__image_end = .;

	/*
	 * Console mirror at a fixed address so a run can be read back from a
	 * guest RAM dump (RAM offset 0x100000).
	 */
	. = 0x40100000;
	.logarea (NOLOAD) : {
		KEEP(*(.logarea))
	}

	/DISCARD/ : {
		*(.comment) *(.note*) *(.eh_frame*) *(.ARM.exidx*)
	}
}
