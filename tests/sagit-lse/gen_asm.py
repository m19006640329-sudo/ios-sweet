#!/usr/bin/env python3
"""Turn osfmk/arm64/sagit_lse_asm.s into something an ELF assembler accepts.

Only Mach-O specifics are rewritten, so the instruction bodies under test are
byte-for-byte the ones the kernel builds:

  * drop the XNU-only ``#include "asm.h"``
  * ``.pushsection __TEXT, __copyio_vectors, regular`` -> ELF section
  * ``.globl _\\name`` / ``_\\name:`` -> ``\\name`` (Mach-O's leading
    underscore is implicit there, but literal in ELF)
  * resolve ``_copyio_recover_table`` to 0, because GAS cannot subtract two
    relocatable symbols that live in different sections.  The recorded ranges
    therefore hold absolute addresses; start.S's handler compares absolute
    PCs, which is the same contract with a different base.
"""

import re
import sys

import patchsrc

TARGET = 'osfmk/arm64/sagit_lse_asm.s'

GLOBL = r'(?m)^([ \t]*\.globl[ \t]+)_\\name[ \t]*$'
LABEL = r'(?m)^_\\name:'

def main():
    if len(sys.argv) != 3:
        sys.exit('usage: gen_asm.py <sagit_lse_asm.s|0084-*.patch> <out.s>')
    src_path, out_path = sys.argv[1], sys.argv[2]
    text = '\n'.join(patchsrc.load(src_path, TARGET)) + '\n'

    text = text.replace('#include "asm.h"\n', '')
    text = text.replace('.pushsection __TEXT, __copyio_vectors, regular',
                        '.pushsection .copyio_vectors, "a"')
    text, n_globl = re.subn(GLOBL, r'\1\\name', text)
    text, n_label = re.subn(LABEL, r'\\name:', text)

    if n_globl != 16 or n_label != 16:
        sys.exit('gen_asm: expected 16 helper macros, rewrote %d globl / %d labels'
                 % (n_globl, n_label))
    if 'asm.h' in text or '__TEXT' in text or re.search(r'(?m)^_\\name', text):
        sys.exit('gen_asm: unreplaced XNU/Mach-O construct left behind')
    if '.copyio_vectors' not in text:
        sys.exit('gen_asm: recovery section rewrite did not apply')

    header = """/* Generated from osfmk/arm64/sagit_lse_asm.s by gen_asm.py.  Do not edit. */
/* _copyio_recover_table is resolved to 0 here, so the recorded ranges are
 * absolute addresses (see tests/sagit-lse/start.S). */
	.set	_copyio_recover_table, 0

"""
    with open(out_path, 'w', encoding='utf-8') as f:
        f.write(header)
        f.write(text)


if __name__ == '__main__':
    main()
