#!/usr/bin/env python3
"""Extract the LSE decode logic and dispatch table from osfmk/arm64/sleh.c.

The self-test must not carry a hand-copied decoder: a copy silently drifts
from the kernel.  Instead the decode block is lifted verbatim out of
sagit_handle_lse() and the dispatch table is re-read as names, both at build
time.  A change in sleh.c is then immediately visible to the test.
"""

import re
import sys

import patchsrc

TARGET = 'osfmk/arm64/sleh.c'
ROWS = 2 * 10 * 4          # [is_st][op][size], each row holds 4 orderings
NAME_RE = re.compile(r'^(sagit_\w+|0)$')


def fail(msg):
    sys.exit('extract_decode: ' + msg)


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: extract_decode.py <sleh.c|0084-*.patch> <out.h>')
    src_path, out_path = sys.argv[1], sys.argv[2]
    lines = patchsrc.load(src_path, TARGET)

    # ---- decode block -------------------------------------------------
    fstart = None
    for i in range(1, len(lines)):
        if 'sagit_handle_lse(' in lines[i] and 'static bool' in lines[i - 1]:
            fstart = i
            break
    if fstart is None:
        fail('sagit_handle_lse() not found')

    bstart = None
    for i in range(fstart, len(lines)):
        if 'uint32_t opcode =' in lines[i]:
            bstart = i
            break
    if bstart is None:
        fail('decode prologue not found')

    bend = None
    for i in range(bstart, len(lines)):
        if 'get_saved_state_reg(state, rn)' in lines[i] and 'addr' in lines[i]:
            bend = i
            break
    if bend is None:
        fail('end of decode block not found')

    block = '\n'.join(lines[bstart:bend])
    # Drop declarations of the values the decode block never fills in; their
    # types come from XNU headers the test does not have.
    block = '\n'.join(ln for ln in block.splitlines()
                      if 'rs_val' not in ln and 'sagit_lse_ret_t' not in ln)
    for needed in ('op_sub', 'is_st', 'op_idx', 'ord'):
        if needed not in block:
            fail('decode block is missing %r' % needed)

    # ---- dispatch table ----------------------------------------------
    tstart = None
    for i, line in enumerate(lines):
        if 'sagit_lse_table[2][10][4][4]' in line:
            tstart = i
            break
    if tstart is None:
        fail('sagit_lse_table not found')

    tend = None
    for i in range(tstart, len(lines)):
        if lines[i].strip() == '};':
            tend = i
            break
    if tend is None:
        fail('end of sagit_lse_table not found')

    rows = []
    for body in re.findall(r'\{([^{}]*)\}', '\n'.join(lines[tstart:tend])):
        items = [x.strip() for x in body.split(',') if x.strip()]
        if len(items) == 4 and all(NAME_RE.match(x) for x in items):
            rows.append(items)
    if len(rows) != ROWS:
        fail('expected %d dispatch rows, parsed %d' % (ROWS, len(rows)))

    flat = [x for row in rows for x in row]
    names = ',\n\t'.join('0' if x == '0' else '"%s"' % x for x in flat)

    out = """/* Generated from osfmk/arm64/sleh.c by extract_decode.py.  Do not edit. */

/* Verbatim decode block from sagit_handle_lse(): returns 1 when the word is
 * an LSE atomic, 0 when it is not. */
static int
test_lse_decode(uint32_t instr, int *o_op, int *o_size, int *o_rs, int *o_rn,
                int *o_rt, int *o_ord, int *o_is_st)
{
%s

	*o_op = op_idx;
	*o_size = (int)size;
	*o_rs = (int)rs;
	*o_rn = (int)rn;
	*o_rt = (int)rt;
	*o_ord = ord;
	*o_is_st = is_st ? 1 : 0;
	return 1;
}

/* sagit_lse_table[is_st][op][size][ord] as names, flattened; NULL entries
 * become 0. */
static const char *const kernel_table_names[%d] = {
	%s,
};
""" % (block, len(flat), names)

    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(out)


if __name__ == '__main__':
    main()
