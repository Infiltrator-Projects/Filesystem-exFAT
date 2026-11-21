#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
test_bin=$(realpath "${1:?Common V2 media test}")
module=$(realpath "${2:?release carrier or freestanding ELF}")
shift 2
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
readelf -SW "$module" > "$work/sections"
if grep -Fw '.infiltrator_boot' "$work/sections" >/dev/null; then
    objcopy --dump-section ".infiltrator_boot=$work/firmware.so" "$module" "$work/carrier-copy.so"
    module="$work/firmware.so"
fi
"$test_bin" "$module" "$@"
