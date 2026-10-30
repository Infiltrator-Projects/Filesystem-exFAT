#!/usr/bin/env bash
# Two-file exFAT native package. Never installs the training runtime.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
version=$(<"$root/VERSION")
revision=${EXFAT_SOURCE_REVISION:?Source commit must come from the validated native archive}
[[ $revision =~ ^[0-9a-f]{40}$ ]] || exit 1
profile=aggressive
build_only=0
output_dir=
while (( $# )); do
  case "$1" in
    --profile) profile=${2:?Missing profile}; shift 2 ;;
    --system-package-mode) shift ;;
    --build-only) build_only=1; shift ;;
    --output) output_dir=${2:?Missing output directory}; shift 2 ;;
    *) echo "Unsupported native installer option: $1" >&2; exit 2 ;;
  esac
done
[[ $profile == aggressive ]] || { echo 'exFAT native installer requires --profile aggressive' >&2; exit 2; }
if (( build_only )) && [[ -z $output_dir ]]; then
  echo '--build-only requires --output DIRECTORY' >&2
  exit 2
fi
for command in cc make mkfs.exfat mkfs.fat fsck.exfat readelf dpkg-deb sha256sum; do
  command -v "$command" >/dev/null || { echo "Missing build prerequisite: $command" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
out="$work/build"
pgo="$work/profile"
mkdir -p "$out" "$pgo"
flags='-std=c11 -O3 -march=native -mtune=native -flto -Wall -Wextra -Wpedantic -Werror'
# The instrumented shared library may use gcov/libc ONLY during test training.
# Its host-linked bytes are never installed and never enter the Debian payload.
make -C "$root" OUT="$out" FREESTANDING_LINK= STRICT_SHARED_LINK= CFLAGS="$flags -fprofile-generate=$pgo" test
if ! find "$pgo" -name '*.gcda' -type f -size +0c | grep -q .; then
  echo 'Measured exFAT PGO training generated no profile data' >&2
  exit 1
fi
rm -f -- "$out/exFAT.so" "$out/exfat-realimage-test"
# Final binary must be completely freestanding (zero DT_NEEDED libraries).
make -C "$root" OUT="$out" CFLAGS="$flags -fprofile-use=$pgo -Wno-missing-profile" test
if readelf -d "$out/exFAT.so" | grep -Fq '(NEEDED)'; then
  echo 'Qualified exFAT shared object unexpectedly links external runtime libraries' >&2
  exit 1
fi
for abi in 3 4 5; do
  readelf -Ws "$out/exFAT.so" | grep -Fq "infiltrator_filesystem_module_v$abi"
  readelf -p .ifsmeta "$out/exFAT.so" | grep -Fq "IFS-MODULE:exfat:$abi"
done
pkg="$work/root"
mkdir -p "$pkg/DEBIAN" "$pkg/System/Filesystems" "$pkg/System/Resources/Documentation/Filesystem-exFAT"
install -m 0755 "$out/exFAT.so" "$pkg/System/Filesystems/exFAT.so"
install -m 0644 "$root/docs/exFAT.pdf" "$pkg/System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf"
[[ $(head -c 5 "$pkg/System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf") == '%PDF-' ]]
sha=$(sha256sum "$pkg/System/Filesystems/exFAT.so" | cut -d' ' -f1)
pdfsha=$(sha256sum "$pkg/System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf" | cut -d' ' -f1)
cat > "$pkg/DEBIAN/control" <<CONTROL
Package: infiltrator-filesystem-exfat
Version: ${version}+native1
Architecture: amd64
Section: admin
Priority: optional
Maintainer: Infiltrator Projects <noreply@infiltrator-projects.invalid>
X-Infiltrator-Source-Revision: $revision
X-Infiltrator-Native-Profile: aggressive
X-Infiltrator-LTO: enabled
X-Infiltrator-PGO: trained-two-pass
X-Infiltrator-Cflags: $flags -fprofile-use
X-Infiltrator-Resource-SHA256: $sha
X-Infiltrator-PDF-SHA256: $pdfsha
Description: Independent native exFAT filesystem engine and illustrated PDF
 64-bit read/write/format; native two-pass measured PGO and LTO.
CONTROL
if [[ -z $output_dir ]]; then
  output_dir="$work/artifacts"
fi
mkdir -p -- "$output_dir"
deb="$output_dir/infiltrator-filesystem-exfat_${version}+native1_amd64.deb"
dpkg-deb --root-owner-group --build "$pkg" "$deb"
inspect="$work/inspect"
dpkg-deb -x "$deb" "$inspect"
mapfile -t installed < <(cd "$inspect" && find . -type f -print | LC_ALL=C sort)
expected=(./System/Filesystems/exFAT.so ./System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf)
if [[ ${installed[*]} != "${expected[*]}" ]]; then
  printf 'Unexpected native package files: %s\n' "${installed[*]}" >&2
  exit 1
fi
[[ $(dpkg-deb -f "$deb" X-Infiltrator-Resource-SHA256) == "$sha" ]] || exit 1
[[ $(dpkg-deb -f "$deb" X-Infiltrator-PDF-SHA256) == "$pdfsha" ]] || exit 1
[[ $(dpkg-deb -f "$deb" X-Infiltrator-PGO) == trained-two-pass ]] || exit 1
if (( build_only )); then
  printf 'Native exFAT package qualified: %s\n' "$deb"
else
  if (( EUID==0 )); then dpkg -i "$deb"; else sudo dpkg -i "$deb"; fi
  [[ $(dpkg-query -W '-f=${Version}' infiltrator-filesystem-exfat) == "${version}+native1" ]] || exit 1
  [[ $(sha256sum /System/Filesystems/exFAT.so | cut -d' ' -f1) == "$sha" ]] || exit 1
  [[ $(sha256sum /System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf | cut -d' ' -f1) == "$pdfsha" ]] || exit 1
  printf 'Native exFAT.so + illustrated PDF installed and verified.\n'
fi
