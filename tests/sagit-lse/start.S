/*
 * Bare-metal AArch64 startup for the SAGIT LSE helper self-test.
 *
 * Target: qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic
 *
 * Sets up an identity-mapped MMU so the helpers' LDXR/STXR run against
 * normal memory (exclusives on Device memory are architecturally
 * unpredictable), then branches to main().
 *
 * The recovery table emitted by COPYIO_RECOVER_RANGE is wired to a minimal
 * data-abort handler.  The handler mimics the kernel's copyio recovery: if
 * the faulting PC falls inside a recorded range, it resumes at the range's
 * recovery label instead of dying.  That is what lets the test exercise the
 * helpers' fault path (unmapped user address) without a kernel.
 */

	.section .text.boot, "ax"
	.globl	_start
	.type	_start, %function
_start:
	mrs	x0, CurrentEL
	lsr	x0, x0, #2
	cmp	x0, #2
	b.eq	from_el2
	cmp	x0, #1
	b.eq	el1_entry
	mov	x0, #5			/* unexpected initial EL */
	b	test_exit

from_el2:
	mov	x0, #(1 << 31)		/* HCR_EL2.RW: EL1 is AArch64 */
	msr	hcr_el2, x0
	mov	x0, #0x3c5		/* EL1h, DAIF masked */
	msr	spsr_el2, x0
	adr	x0, el1_entry
	msr	elr_el2, x0
	eret

el1_entry:
	msr	daifset, #0xf
	ldr	x0, =boot_stack_top
	mov	sp, x0
	ldr	x0, =vector_table
	msr	vbar_el1, x0
	mov	x0, #(3 << 20)		/* CPACR_EL1.FPEN = 0b11 */
	msr	cpacr_el1, x0

	/* Zero .bss (also clears the page tables). */
	ldr	x0, =__bss_start
	ldr	x1, =__bss_end
1:	cmp	x0, x1
	b.hs	2f
	str	xzr, [x0], #8
	b	1b
2:
	/* MAIR_EL1: attr0 = Device-nGnRnE, attr1 = Normal WB RW-allocate. */
	mov	x0, #0xff00
	msr	mair_el1, x0

	/* T0SZ=32 (4GB VA), TG0=4K, WB inner/outer, inner-shareable,
	 * IPS=40-bit.  T1SZ must be 32 as well: the two VA ranges may not
	 * overlap from the bottom (T0SZ + T1SZ >= 64), even though TTBR1 is
	 * never used. */
	ldr	x0, =0x2B5203520
	msr	tcr_el1, x0

	/* L1[0..3] -> one L2 table each, covering VA 0..4GB. */
	ldr	x4, =l1_table
	ldr	x5, =l2_tables
	mov	x6, #4
3:	orr	x7, x5, #3
	str	x7, [x4], #8
	add	x5, x5, #4096
	subs	x6, x6, #1
	b.ne	3b

	/* l2[0][72] = 0x09000000, the PL011 UART, as a 2MB Device block.
	 * Everything else below 1GB stays unmapped so the fault test has a
	 * reliably faulting address (VA 0). */
	ldr	x4, =l2_tables
	mov	x6, #0x09000000
	mov	x9, #0x401		/* valid|block|AF, AttrIdx 0 */
	orr	x6, x6, x9
	mov	x7, #72
	str	x6, [x4, x7, lsl #3]

	/* l2[1] = 0x40000000..0x5fffffff identity, Normal WB RW. */
	add	x4, x4, #4096
	mov	x5, #0x40000000
	mov	x9, #0x705		/* valid|block|AttrIdx 1|SH inner|AF */
	mov	x6, #512
4:	orr	x7, x5, x9
	str	x7, [x4], #8
	add	x5, x5, #0x200000
	subs	x6, x6, #1
	b.ne	4b

	ldr	x0, =l1_table
	msr	ttbr0_el1, x0
	dsb	sy
	tlbi	vmalle1
	dsb	sy
	isb

	mrs	x0, sctlr_el1
	mov	x1, #1			/* M */
	orr	x0, x0, x1
	mov	x1, #(1 << 2)		/* C */
	orr	x0, x0, x1
	mov	x1, #(1 << 12)		/* I */
	orr	x0, x0, x1
	msr	sctlr_el1, x0
	isb
	ic	ialluis
	dsb	sy
	isb

	bl	main
	bl	test_exit
5:	b	5b

/* ------------------------------------------------------------------ */
/* Exit through PSCI SYSTEM_OFF.  The status is also stored in RAM so a */
/* run can be inspected from a memory dump.                            */
/* ------------------------------------------------------------------ */
	.globl	test_exit
	.type	test_exit, %function
test_exit:				/* x0 = exit code */
	adr	x1, exit_status
	str	w0, [x1]
	dsb	sy
	movz	x0, #0x0008		/* PSCI SYSTEM_OFF */
	movk	x0, #0x8400, lsl #16
	hvc	#0
1:	b	1b

/* ------------------------------------------------------------------ */
/* Exception vectors                                                   */
/* ------------------------------------------------------------------ */
	.section .vectors, "ax"
	.balign	2048
	.globl	vector_table
vector_table:
	.macro	ventry label
	.balign	128
	b	\label
	.endm

	ventry	unhandled		/* 0x000 current EL, SP0 */
	ventry	unhandled
	ventry	unhandled
	ventry	unhandled

	ventry	el1_sync		/* 0x200 current EL, SPx */
	ventry	unhandled
	ventry	unhandled
	ventry	unhandled

	ventry	unhandled		/* 0x400 lower EL, AArch64 */
	ventry	unhandled
	ventry	unhandled
	ventry	unhandled

	ventry	unhandled		/* 0x600 lower EL, AArch32 */
	ventry	unhandled
	ventry	unhandled
	ventry	unhandled

	.text
	.balign	4
el1_sync:
	sub	sp, sp, #160
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x19, x20, [sp, #80]
	stp	x21, x22, [sp, #96]
	stp	x23, x24, [sp, #112]
	stp	x25, x26, [sp, #128]
	stp	x27, x28, [sp, #144]

	mrs	x19, esr_el1
	mrs	x20, elr_el1
	lsr	x21, x19, #26
	cmp	x21, #0x25		/* Data abort, same EL */
	b.ne	2f

	ldr	x22, =__copyio_vectors_start
	ldr	x23, =__copyio_vectors_end
1:	cmp	x22, x23
	b.hs	2f
	ldr	x24, [x22, #0]		/* range start */
	ldr	x25, [x22, #8]		/* range end   */
	cmp	x20, x24
	b.lo	3f
	cmp	x20, x25
	b.hs	3f
	ldr	x26, [x22, #16]		/* recovery PC */
	msr	elr_el1, x26
	b	4f
3:	add	x22, x22, #24
	b	1b
4:
	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x19, x20, [sp, #80]
	ldp	x21, x22, [sp, #96]
	ldp	x23, x24, [sp, #112]
	ldp	x25, x26, [sp, #128]
	ldp	x27, x28, [sp, #144]
	add	sp, sp, #160
	eret
2:	mov	x0, #3			/* unrecoverable abort */
	b	test_exit

unhandled:
	mov	x0, #4
	b	test_exit

/* ------------------------------------------------------------------ */
/* Storage                                                             */
/* ------------------------------------------------------------------ */
	.section .data
	.balign	8
exit_status:
	.word	0

	.section .bss
	.balign	4096
boot_stack:
	.space	0x10000
boot_stack_top:

	.balign	4096
l1_table:
	.space	4096

	.balign	4096
l2_tables:
	.space	16384
