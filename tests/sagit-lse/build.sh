#!/usr/bin/env bash
# Build the SAGIT LSE helper self-test as a bare-metal cortex-a53 image.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$here/build}

# The kernel sources live only inside the patch in this checkout, so the
# generators read the patch's post-image (see patchsrc.py).
patch="$root/xnu/patches/0084-sagit-lse-emulation.patch"

CLANG=${CLANG:-clang}
LLD=${LLD:-ld.lld}
PYTHON=${PYTHON:-python3}

mkdir -p "$out"

"$PYTHON" "$here/gen_asm.py" "$patch" "$out/sagit_lse_elf.s"
"$PYTHON" "$here/extract_decode.py" "$patch" "$out/decode_gen.h"
"$PYTHON" "$here/gen_probe.py" --clang "$CLANG" --work "$out" --out "$out/probe_gen.h"

CFLAGS="--target=aarch64-none-elf -march=armv8-a -ffreestanding -nostdlib
	-fno-builtin -mgeneral-regs-only -O2 -Wall -Wextra -Werror -I$out"
# Assembler invocations must not carry the C-only warnings flags: clang warns
# about unused options (-Wunused-command-line-argument) and -Werror turns that
# warning into a build failure.
AFLAGS="--target=aarch64-none-elf -march=armv8-a -I$out"

# shellcheck disable=SC2086
"$CLANG" $AFLAGS -c "$here/start.S" -o "$out/start.o"
# shellcheck disable=SC2086
"$CLANG" $AFLAGS -c "$out/sagit_lse_elf.s" -o "$out/lse.o"
# shellcheck disable=SC2086
"$CLANG" $CFLAGS -c "$here/main.c" -o "$out/main.o"

"$LLD" -T "$here/link.ld" -o "$out/test.elf" \
	"$out/start.o" "$out/main.o" "$out/lse.o" --no-undefined -z noexecstack

echo "built $out/test.elf"
