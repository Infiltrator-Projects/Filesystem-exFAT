#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
set -eu
module=${1:?exFAT.so required}
symbols=$(nm -D --defined-only --format=posix "$module" | awk '{print $1}')
if [ "$symbols" != 'infiltrator_filesystem_api_v2' ]; then
    printf 'Expected only Common V2 export, found: %s\n' "$symbols" >&2
    exit 1
fi
readelf -p .ifsmeta "$module" | grep -Fq 'IFS-MODULE:exfat:common2' ||
    { echo 'Missing Common V2 exFAT identity' >&2; exit 1; }
