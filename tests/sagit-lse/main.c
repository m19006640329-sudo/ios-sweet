/*
 * SAGIT LSE atomic emulation self-test.
 *
 * Runs bare metal on a cortex-a53 (ARMv8.0, no LSE) under QEMU and checks
 * three things against osfmk/arm64:
 *
 *   1. sagit_lse_table[2][10][4][4] dispatches to the helper its name says.
 *   2. Every helper implements the architectural semantics of its LSE
 *      instruction (old value returned, memory updated, sub-word operands
 *      zero/sign-extended correctly, ordering variants included).
 *   3. The COPYIO_RECOVER_RANGE table recovers a fault on a bad user address
 *      instead of dying, and reports status = 1.
 *
 * Plus: all 224 real instruction encodings produced by the assembler must
 * decode to the tuple the helper implements (decode_gen.h / probe_gen.h are
 * generated from sleh.c and the assembler at build time, see build.sh).
 */

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Console: PL011 at 0x09000000, mapped Device by start.S              */
/* ------------------------------------------------------------------ */
#define UART_DR (*(volatile uint32_t *)0x09000000u)
#define UART_FR (*(volatile uint32_t *)0x09000018u)

/*
 * Console output is mirrored into .logarea, which link.ld pins to a fixed
 * address: a run can then be read out of a guest RAM dump even when no
 * serial backend is usable.
 */
#define LOG_MAGIC 0x4c4f4731u		/* "LOG1" */
struct logarea {
	uint32_t magic;
	uint32_t len;
	char data[8184];
};
static struct logarea g_log __attribute__((section(".logarea")));

static void putc_(char c)
{
	if (g_log.len < sizeof(g_log.data) - 1) {
		g_log.data[g_log.len++] = c;
	}
	if (c == '\n') {
		while (UART_FR & (1u << 5)) {
		}
		UART_DR = (uint32_t)'\r';
	}
	while (UART_FR & (1u << 5)) {
	}
	UART_DR = (uint32_t)(uint8_t)c;
}

static void puts_(const char *s)
{
	while (*s != '\0') {
		putc_(*s++);
	}
}

static void putx(uint64_t v)
{
	static const char hex[] = "0123456789abcdef";
	char buf[16];
	int i;

	for (i = 0; i < 16; i++) {
		buf[15 - i] = hex[v & 0xf];
		v >>= 4;
	}
	for (i = 0; i < 16; i++) {
		putc_(buf[i]);
	}
}

static void putu(uint64_t v)
{
	char buf[20];
	int i = 0;

	if (v == 0) {
		putc_('0');
		return;
	}
	while (v != 0) {
		buf[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (i > 0) {
		putc_(buf[--i]);
	}
}

/* ------------------------------------------------------------------ */
/* Helper ABI: a 16-byte struct return lands in x0/x1, exactly the      */
/* register pair the assembly helpers use.                             */
/* ------------------------------------------------------------------ */
typedef struct {
	uint64_t old_val;
	uint64_t status;
} lse_ret_t;

typedef lse_ret_t (*lse_fn_t)(uint64_t, uint64_t, uint64_t);

enum { OP_ADD, OP_CLR, OP_EOR, OP_SET, OP_SMAX, OP_SMIN, OP_UMAX, OP_UMIN,
       OP_SWP, OP_CAS };
enum { SZ_B, SZ_H, SZ_W, SZ_X };
enum { ORD_NONE, ORD_A, ORD_L, ORD_AL };

struct tcase {
	const char *name;
	uint8_t op;
	uint8_t sz;
	uint8_t ord;
	uint8_t is_st;
	lse_fn_t fn;
};

#define E(n) extern lse_ret_t n(uint64_t, uint64_t, uint64_t);
#define D4(b) E(b) E(b##a) E(b##l) E(b##al)
#define D2(b) E(b) E(b##l)
#define DECL_LD(op) D4(sagit_##op##b) D4(sagit_##op##h) D4(sagit_##op) D4(sagit_##op##_x)
#define DECL_ST(op) D2(sagit_##op##b) D2(sagit_##op##h) D2(sagit_##op) D2(sagit_##op##_x)

DECL_LD(ldadd) DECL_LD(ldclr) DECL_LD(ldeor) DECL_LD(ldset)
DECL_LD(ldsmax) DECL_LD(ldsmin) DECL_LD(ldumax) DECL_LD(ldumin)
DECL_LD(swp) DECL_LD(cas)
DECL_ST(stadd) DECL_ST(stclr) DECL_ST(steor) DECL_ST(stset)
DECL_ST(stsmax) DECL_ST(stsmin) DECL_ST(stumax) DECL_ST(stumin)

#define R4(b, op, sz, st) \
	{ #b, op, sz, ORD_NONE, st, b }, \
	{ #b "a", op, sz, ORD_A, st, b##a }, \
	{ #b "l", op, sz, ORD_L, st, b##l }, \
	{ #b "al", op, sz, ORD_AL, st, b##al },
#define R2(b, op, sz, st) \
	{ #b, op, sz, ORD_NONE, st, b }, \
	{ #b "l", op, sz, ORD_L, st, b##l },
#define T_LD(op, opid) \
	R4(sagit_##op##b, opid, SZ_B, 0) R4(sagit_##op##h, opid, SZ_H, 0) \
	R4(sagit_##op, opid, SZ_W, 0) R4(sagit_##op##_x, opid, SZ_X, 0)
#define T_ST(op, opid) \
	R2(sagit_##op##b, opid, SZ_B, 1) R2(sagit_##op##h, opid, SZ_H, 1) \
	R2(sagit_##op, opid, SZ_W, 1) R2(sagit_##op##_x, opid, SZ_X, 1)

static const struct tcase cases[] = {
	T_LD(ldadd, OP_ADD) T_LD(ldclr, OP_CLR) T_LD(ldeor, OP_EOR)
	T_LD(ldset, OP_SET) T_LD(ldsmax, OP_SMAX) T_LD(ldsmin, OP_SMIN)
	T_LD(ldumax, OP_UMAX) T_LD(ldumin, OP_UMIN) T_LD(swp, OP_SWP)
	T_LD(cas, OP_CAS)
	T_ST(stadd, OP_ADD) T_ST(stclr, OP_CLR) T_ST(steor, OP_EOR)
	T_ST(stset, OP_SET) T_ST(stsmax, OP_SMAX) T_ST(stsmin, OP_SMIN)
	T_ST(stumax, OP_UMAX) T_ST(stumin, OP_UMIN)
};

struct probe {
	uint32_t word;
	uint8_t op;
	uint8_t sz;
	uint8_t ord;
	uint8_t is_st;
};

#include "decode_gen.h"
#include "probe_gen.h"

/* ------------------------------------------------------------------ */
/* Reference model                                                     */
/* ------------------------------------------------------------------ */
#define POISON 0xdeadbeef00000000ull

static uint64_t mask_of(int sz)
{
	switch (sz) {
	case SZ_B: return 0xffull;
	case SZ_H: return 0xffffull;
	case SZ_W: return 0xffffffffull;
	default:   return ~0ull;
	}
}

static int64_t sext_(uint64_t v, int sz)
{
	switch (sz) {
	case SZ_B: return (int64_t)(int8_t)(uint8_t)v;
	case SZ_H: return (int64_t)(int16_t)(uint16_t)v;
	case SZ_W: return (int64_t)(int32_t)(uint32_t)v;
	default:   return (int64_t)v;
	}
}

/* Value the instruction leaves in memory, given the old memory contents. */
static uint64_t ref_new(int op, int sz, uint64_t old, uint64_t rs)
{
	uint64_t m = mask_of(sz);
	uint64_t o = old & m;
	uint64_t r = rs & m;

	switch (op) {
	case OP_ADD:  return (o + r) & m;
	case OP_CLR:  return (o & ~r) & m;
	case OP_EOR:  return (o ^ r) & m;
	case OP_SET:  return (o | r) & m;
	case OP_SMAX: return (sext_(o, sz) >= sext_(r, sz)) ? o : r;
	case OP_SMIN: return (sext_(o, sz) <= sext_(r, sz)) ? o : r;
	case OP_UMAX: return (o >= r) ? o : r;
	case OP_UMIN: return (o <= r) ? o : r;
	default:      return r;              /* SWP */
	}
}

static uint64_t ref_cas(int sz, uint64_t old, uint64_t exp, uint64_t newv)
{
	uint64_t m = mask_of(sz);
	return ((old & m) == (exp & m)) ? (newv & m) : (old & m);
}

static uint64_t mk_mem(int sz, uint64_t v)
{
	return (sz == SZ_X) ? v : (POISON | (v & mask_of(sz)));
}

/* ------------------------------------------------------------------ */
/* Test vectors                                                        */
/* ------------------------------------------------------------------ */
struct vec {
	uint64_t old;
	uint64_t rs;
};

/* Sub-word cases include operands whose bits above the access size are
 * dirty: the instruction operand is only <size> bits wide. */
static const struct vec vec_b[] = {
	{ 0x00, 0x00 }, { 0x7f, 0x01 }, { 0x80, 0x7f }, { 0xff, 0x80 },
	{ 0x12, 0x23 }, { 0x00, 0x1ffff80ull }, { 0xff, 0x100ull },
	{ 0x80, 0xffffff80ull },
};
static const struct vec vec_h[] = {
	{ 0x0000, 0x0000 }, { 0x7fff, 0x0001 }, { 0x8000, 0x7fff },
	{ 0xffff, 0x8000 }, { 0x1234, 0x9abc }, { 0x0000, 0x1ffff8000ull },
	{ 0xffff, 0x10000ull },
};
static const struct vec vec_w[] = {
	{ 0x00000000u, 0x00000000u }, { 0x7fffffffu, 0x00000001u },
	{ 0x80000000u, 0xffffffffu }, { 0xffffffffu, 0x80000000u },
	{ 0x12345678u, 0x9abcdef0u }, { 0x00000001u, 0x100000001ull },
};
static const struct vec vec_x[] = {
	{ 0x0000000000000000ull, 0x0000000000000000ull },
	{ 0x7fffffffffffffffull, 0x0000000000000001ull },
	{ 0x8000000000000000ull, 0xffffffffffffffffull },
	{ 0xffffffffffffffffull, 0x8000000000000000ull },
	{ 0x0123456789abcdefull, 0xfedcba9876543210ull },
};

struct cvec {
	uint64_t old;
	uint64_t exp;
	uint64_t newv;
};

static const struct cvec cvec_b[] = {
	{ 0x12, 0x12, 0x34 },           /* match */
	{ 0x12, 0x13, 0x34 },           /* mismatch */
	{ 0x00, 0x100ull, 0x55 },       /* matches on the low byte only */
	{ 0x80, 0xffffff80ull, 0x01 },  /* low byte matches, sign-extended */
};
static const struct cvec cvec_h[] = {
	{ 0x1234, 0x1234, 0x5678 },
	{ 0x1234, 0x1235, 0x5678 },
	{ 0x0000, 0x10000ull, 0x55 },
	{ 0x8000, 0xffff8000ull, 0x01 },
};
static const struct cvec cvec_w[] = {
	{ 0x12345678u, 0x12345678u, 0x9abcdef0u },
	{ 0x12345678u, 0x12345679u, 0x9abcdef0u },
	{ 0x00000000u, 0x100000000ull, 0x55u },
	{ 0x80000000u, 0xffffffff80000000ull, 0x01u },
};
static const struct cvec cvec_x[] = {
	{ 0x0123456789abcdefull, 0x0123456789abcdefull, 0xfedcba9876543210ull },
	{ 0x0123456789abcdefull, 0x0123456789abcdeeull, 0xfedcba9876543210ull },
	{ 0x0000000000000000ull, 0x0000000000000000ull, 0x55ull },
	{ 0x8000000000000000ull, 0x8000000000000000ull, 0x01ull },
};

/* ------------------------------------------------------------------ */
/* Reporting                                                           */
/* ------------------------------------------------------------------ */
static uint64_t g_pass;
static uint64_t g_fail;
static int g_shown;
#define MAX_SHOWN 60

static void fail_(const struct tcase *t, int vi, const char *what,
                  uint64_t exp, uint64_t got)
{
	g_fail++;
	if (g_shown >= MAX_SHOWN) {
		return;
	}
	g_shown++;
	puts_("FAIL ");
	puts_(t->name);
	puts_(" vec");
	putu((uint64_t)vi);
	puts_(" ");
	puts_(what);
	puts_(" exp=");
	putx(exp);
	puts_(" got=");
	putx(got);
	putc_('\n');
}

static void ok_(void)
{
	g_pass++;
}

/* ------------------------------------------------------------------ */
/* Phase 1: dispatch table contents                                    */
/* ------------------------------------------------------------------ */
static const struct tcase *find_case(int op, int sz, int ord, int is_st)
{
	unsigned i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		if (cases[i].op == op && cases[i].sz == sz &&
		    cases[i].ord == ord && cases[i].is_st == is_st) {
			return &cases[i];
		}
	}
	return 0;
}

static int streq(const char *a, const char *b)
{
	if (a == 0 || b == 0) {
		return a == b;
	}
	while (*a != '\0' && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static void check_table(void)
{
	int is_st, op, sz, ord;

	for (is_st = 0; is_st < 2; is_st++) {
		for (op = 0; op < 10; op++) {
			for (sz = 0; sz < 4; sz++) {
				for (ord = 0; ord < 4; ord++) {
					int idx = ((is_st * 10 + op) * 4 + sz) * 4 + ord;
					const char *kn = kernel_table_names[idx];
					const struct tcase *mine =
						find_case(op, sz, ord, is_st);

					if (streq(kn, mine ? mine->name : 0)) {
						ok_();
					} else {
						g_fail++;
						if (g_shown < MAX_SHOWN) {
							g_shown++;
							puts_("FAIL table[");
							putu((uint64_t)is_st);
							putc_(',');
							putu((uint64_t)op);
							putc_(',');
							putu((uint64_t)sz);
							putc_(',');
							putu((uint64_t)ord);
							puts_("] kernel=");
							puts_(kn ? kn : "<null>");
							puts_(" test=");
							puts_(mine ? mine->name : "<null>");
							putc_('\n');
						}
					}
				}
			}
		}
	}
}

/* ------------------------------------------------------------------ */
/* Phase 2/3: helper semantics and fault recovery                      */
/* ------------------------------------------------------------------ */
static int nvec(int sz)
{
	switch (sz) {
	case SZ_B: return (int)(sizeof(vec_b) / sizeof(vec_b[0]));
	case SZ_H: return (int)(sizeof(vec_h) / sizeof(vec_h[0]));
	case SZ_W: return (int)(sizeof(vec_w) / sizeof(vec_w[0]));
	default:   return (int)(sizeof(vec_x) / sizeof(vec_x[0]));
	}
}

static const struct vec *vec_of(int sz)
{
	switch (sz) {
	case SZ_B: return vec_b;
	case SZ_H: return vec_h;
	case SZ_W: return vec_w;
	default:   return vec_x;
	}
}

static void check_exec(const struct tcase *t, int vi, uint64_t old, uint64_t rs,
                       uint64_t rt)
{
	uint64_t mem = mk_mem(t->sz, old);
	uint64_t m = mask_of(t->sz);
	lse_ret_t r = t->fn((uint64_t)&mem, rs, rt);
	uint64_t exp_new = (t->op == OP_CAS) ? ref_cas(t->sz, old, rs, rt)
	                                     : ref_new(t->op, t->sz, old, rs);
	uint64_t exp_old = t->is_st ? 0 : (old & m);

	if (r.status != 0) {
		fail_(t, vi, "status", 0, r.status);
		return;
	}
	if (r.old_val != exp_old) {
		fail_(t, vi, "old", exp_old, r.old_val);
		return;
	}
	if (mem != mk_mem(t->sz, exp_new)) {
		fail_(t, vi, "mem", mk_mem(t->sz, exp_new), mem);
		return;
	}
	ok_();
}

static void run_semantics(void)
{
	unsigned i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const struct tcase *t = &cases[i];
		int n = nvec(t->sz);
		int v;

		if (t->op == OP_CAS) {
			const struct cvec *cv = (t->sz == SZ_B) ? cvec_b :
			                        (t->sz == SZ_H) ? cvec_h :
			                        (t->sz == SZ_W) ? cvec_w : cvec_x;
			int cn = (t->sz == SZ_B) ? (int)(sizeof(cvec_b) / sizeof(cvec_b[0])) :
			         (t->sz == SZ_H) ? (int)(sizeof(cvec_h) / sizeof(cvec_h[0])) :
			         (t->sz == SZ_W) ? (int)(sizeof(cvec_w) / sizeof(cvec_w[0])) :
			                           (int)(sizeof(cvec_x) / sizeof(cvec_x[0]));
			for (v = 0; v < cn; v++) {
				check_exec(t, v, cv[v].old, cv[v].exp, cv[v].newv);
			}
			continue;
		}
		for (v = 0; v < n; v++) {
			const struct vec *pv = vec_of(t->sz);
			check_exec(t, v, pv[v].old, pv[v].rs, 0);
		}
	}
}

static void run_faults(void)
{
	unsigned i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const struct tcase *t = &cases[i];
		lse_ret_t r = t->fn(0, 0x1234, 0x5678);

		if (r.status == 1) {
			ok_();
		} else {
			fail_(t, 0, "fault status", 1, r.status);
		}
	}
}

/* ------------------------------------------------------------------ */
/* Phase 4: real instruction encodings                                 */
/* ------------------------------------------------------------------ */
static void run_probes(void)
{
	unsigned i;
	int n = (int)(sizeof(probes) / sizeof(probes[0]));

	for (i = 0; i < (unsigned)n; i++) {
		const struct probe *p = &probes[i];
		int op = 0, sz = 0, rs = 0, rn = 0, rt = 0, ord = 0, is_st = 0;
		const struct tcase *t;

		if (!test_lse_decode(p->word, &op, &sz, &rs, &rn, &rt, &ord, &is_st)) {
			g_fail++;
			if (g_shown < MAX_SHOWN) {
				g_shown++;
				puts_("FAIL decode rejected 0x");
				putx(p->word);
				putc_('\n');
			}
			continue;
		}
		if (op != p->op || sz != p->sz || ord != p->ord || is_st != p->is_st) {
			g_fail++;
			if (g_shown < MAX_SHOWN) {
				g_shown++;
				puts_("FAIL decode 0x");
				putx(p->word);
				puts_(" got(");
				putu((uint64_t)op);
				putc_(',');
				putu((uint64_t)sz);
				putc_(',');
				putu((uint64_t)ord);
				putc_(',');
				putu((uint64_t)is_st);
				puts_(") want(");
				putu((uint64_t)p->op);
				putc_(',');
				putu((uint64_t)p->sz);
				putc_(',');
				putu((uint64_t)p->ord);
				putc_(',');
				putu((uint64_t)p->is_st);
				puts_(")\n");
			}
			continue;
		}

		t = find_case(op, sz, ord, is_st);
		if (t == 0) {
			g_fail++;
			if (g_shown < MAX_SHOWN) {
				g_shown++;
				puts_("FAIL decode 0x");
				putx(p->word);
				puts_(" has no helper\n");
			}
			continue;
		}

		/* Execute through a register file, the way the kernel's trap
		 * handler does, and compare with the reference model. */
		{
			uint64_t regs[32];
			uint64_t mem;
			lse_ret_t r;
			uint64_t old, exp, newv;
			uint64_t want_old;

			if (op == OP_CAS) {
				const struct cvec *cv = (sz == SZ_B) ? cvec_b :
				                        (sz == SZ_H) ? cvec_h :
				                        (sz == SZ_W) ? cvec_w : cvec_x;
				old = cv[0].old;
				exp = cv[0].exp;
				newv = cv[0].newv;
			} else {
				const struct vec *pv = vec_of(sz);
				old = pv[0].old;
				exp = pv[0].rs;
				newv = 0;
			}

			mem = mk_mem(sz, old);
			for (int k = 0; k < 32; k++) {
				regs[k] = 0;
			}
			regs[rn] = (uint64_t)&mem;
			regs[rs] = exp;
			if (rt != 31) {
				regs[rt] = newv;
			}

			r = t->fn(regs[rn], regs[rs], rt == 31 ? 0 : regs[rt]);
			if (r.status != 0) {
				fail_(t, (int)i, "probe status", 0, r.status);
				continue;
			}
			if (!is_st) {
				/* The trap handler writes the old value back into Rt. */
				regs[rt] = r.old_val;
				want_old = old & mask_of(sz);
				if (regs[rt] != want_old) {
					fail_(t, (int)i, "probe Rt", want_old, regs[rt]);
					continue;
				}
			}
			{
				uint64_t want_new = (op == OP_CAS)
					? ref_cas(sz, old, exp, newv)
					: ref_new(op, sz, old, exp);
				if (mem != mk_mem(sz, want_new)) {
					fail_(t, (int)i, "probe mem",
					      mk_mem(sz, want_new), mem);
					continue;
				}
			}
			ok_();
		}
	}
}

/* ------------------------------------------------------------------ */
int main(void)
{
	g_log.magic = LOG_MAGIC;
	g_log.len = 0;

	puts_("\r\nsagit-lse self-test on cortex-a53 (ARMv8.0, no LSE)\r\n");
	puts_("helpers=");
	putu(sizeof(cases) / sizeof(cases[0]));
	puts_(" probes=");
	putu(sizeof(probes) / sizeof(probes[0]));
	puts_("\r\n");

	check_table();
	puts_("phase 1 dispatch table done\r\n");

	run_semantics();
	puts_("phase 2 helper semantics done\r\n");

	run_faults();
	puts_("phase 3 fault recovery done\r\n");

	run_probes();
	puts_("phase 4 instruction decode done\r\n");

	puts_("PASS ");
	putu(g_pass);
	puts_(" FAIL ");
	putu(g_fail);
	puts_("\r\n");
	if (g_fail == 0) {
		puts_("RESULT: OK\r\n");
		return 0;
	}
	puts_("RESULT: FAILED\r\n");
	return 1;
}
