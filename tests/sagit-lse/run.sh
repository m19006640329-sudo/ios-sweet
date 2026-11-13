#!/usr/bin/env bash
# Run the LSE self-test under QEMU on a cortex-a53 (ARMv8.0, no LSE).
#
# The image ends with PSCI SYSTEM_OFF, so QEMU exits by itself and a hang
# (e.g. an unmapped access outside a recovery range) is caught by timeout.
# The gate is the "RESULT: OK" line, not the exit status.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
out=${1:-$here/build}
QEMU=${QEMU:-qemu-system-aarch64}
SECS=${SECS:-180}

if [ ! -f "$out/test.elf" ]; then
	echo "missing $out/test.elf; run build.sh first" >&2
	exit 2
fi

status=0
timeout "$SECS" "$QEMU" \
	-M virt -cpu cortex-a53 -m 256M -no-reboot \
	-display none -monitor none -serial stdio \
	-kernel "$out/test.elf" >"$out/serial.txt" 2>&1 || status=$?

cat "$out/serial.txt"

if [ "$status" -eq 124 ]; then
	echo "QEMU timed out after ${SECS}s" >&2
	exit 1
fi
if [ "$status" -ne 0 ]; then
	echo "QEMU exited with status $status" >&2
	exit 1
fi
if ! grep -q 'RESULT: OK' "$out/serial.txt"; then
	echo "self-test did not report RESULT: OK" >&2
	exit 1
fi
echo "self-test passed"
