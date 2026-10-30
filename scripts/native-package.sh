#!/usr/bin/env bash
# Two-file exFAT package: the full native filesystem API and its PDF.
# Profile the actual unified SO with independently formatted exFAT media.
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
for command in cc cmake git make mkfs.exfat mkfs.fat fsck.exfat readelf nm dpkg-deb sha256sum; do
  command -v "$command" >/dev/null || { echo "Missing build prerequisite: $command" >&2; exit 1; }
done
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
out="$work/build"
pgo="$work/profile"
mkdir -p "$out" "$pgo"
flags='-std=c11 -O3 -march=native -mtune=native -flto -Wall -Wextra -Wpedantic -Werror'
media_test="$work/exfat-realimage-test"
cc -std=c11 -O2 -Wall -Wextra -Werror \
   -I"$root/native/filesystems/exfat/module" -I"$root/native/filesystems/fat/portable" \
   "$root/tests/exfat_so_realimage_test.c" -ldl -o "$media_test"

# Only disposable regular images are used to train and verify the complete SO.
train_media() {
  local root_media
  root_media=$(mktemp -d "$work/real-media-XXXXXXXX")
  truncate -s 128M "$root_media/exfat.img"
  truncate -s 128M "$root_media/fat32.img"
  mkfs.exfat -n NATIVE "$root_media/exfat.img"
  mkfs.fat -F 32 -I "$root_media/fat32.img"
  "$media_test" "$out/exFAT.so" "$root_media/exfat.img" "$root_media/fat32.img"
  fsck.exfat -n "$root_media/exfat.img"
  rm -rf -- "$root_media"
}

# Keep source/object paths identical across instrumented and final passes:
# profile matching must not silently degrade to an untrained build.
cmake -S "$root" -B "$out" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="$flags -fprofile-generate=$pgo" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-generate=$pgo"
cmake --build "$out" --target exfat-unified exfat-unified-contract --parallel 2
ctest --test-dir "$out" --output-on-failure
train_media
if ! find "$pgo" -name '*.gcda' -type f -size +0c | grep -q .; then
  echo 'Measured native exFAT PGO training generated no profile data' >&2
  exit 1
fi
cmake -S "$root" -B "$out" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_C_FLAGS="$flags -fprofile-use=$pgo -Wno-missing-profile" \
  -DCMAKE_SHARED_LINKER_FLAGS="-fprofile-use=$pgo"
cmake --build "$out" --target exfat-unified exfat-unified-contract --clean-first --parallel 2
ctest --test-dir "$out" --output-on-failure
train_media
for abi in 3 4 5; do
  nm -D "$out/exFAT.so" | grep -Fq "infiltrator_filesystem_module_v$abi"
done
nm -D "$out/exFAT.so" | grep -Fq 'infiltrator_filesystem_exfat_defrag_v1'
# Dynamic dependencies are limited to the normal Linux C/crypto/database
# runtime; never invoke external filesystem programs to perform operations.
while IFS= read -r needed; do
  case "$needed" in
    libc.so.*|libcrypto.so.*|libsqlite3.so.*|libm.so.*|libpthread.so.*|ld-linux-x86-64.so.2) ;;
    *) printf 'Unexpected linked runtime dependency: %s\n' "$needed" >&2; exit 1 ;;
  esac
done < <(readelf -d "$out/exFAT.so" | sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p')

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
Depends: libc6, libssl3t64 | libssl3, libsqlite3-0
X-Infiltrator-Source-Revision: $revision
X-Infiltrator-Native-Profile: aggressive
X-Infiltrator-LTO: enabled
X-Infiltrator-PGO: trained-two-pass
X-Infiltrator-Cflags: $flags -fprofile-use
X-Infiltrator-Resource-SHA256: $sha
X-Infiltrator-PDF-SHA256: $pdfsha
Description: Independent unified exFAT filesystem engine and illustrated PDF
 Complete V3/V4/V5 file API plus native defragmentation/recovery.
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
  printf 'Native unified exFAT package qualified: %s\n' "$deb"
else
  if (( EUID==0 )); then dpkg -i "$deb"; else sudo dpkg -i "$deb"; fi
  [[ $(dpkg-query -W '-f=${Version}' infiltrator-filesystem-exfat) == "${version}+native1" ]] || exit 1
  [[ $(sha256sum /System/Filesystems/exFAT.so | cut -d' ' -f1) == "$sha" ]] || exit 1
  [[ $(sha256sum /System/Resources/Documentation/Filesystem-exFAT/exFAT.pdf | cut -d' ' -f1) == "$pdfsha" ]] || exit 1
  printf 'Native exFAT.so + illustrated PDF installed and verified.\n'
fi
