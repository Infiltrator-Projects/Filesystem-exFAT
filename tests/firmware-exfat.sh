#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# BOOT must load the EFI stub and initramfs from a Common V2 exFAT module.
set -euo pipefail
export LC_ALL=C
boot=$(realpath "$1");module=$(realpath "$2")
kernel=$(realpath "$3");writer=$(realpath "$4")
work=$(realpath -m "$5");mkdir -p "$work"
esp="$work/esp.img";media="$work/exfat.img";disk="$work/gpt.img"
ram="$work/ram";mkdir -p "$ram/bin" "$ram/dev" "$ram/proc"
cp "$(command -v busybox)" "$ram/bin/busybox"
ln -sf busybox "$ram/bin/sh"
cat > "$ram/init" <<'GUEST'
#!/bin/sh
set -eu
b=/bin/busybox
$b mount -t devtmpfs devtmpfs /dev
exec </dev/ttyS0 >/dev/ttyS0 2>&1
trap '/bin/busybox poweroff -f' EXIT
$b mount -t proc proc /proc
$b grep -qw exfat_so_trial /proc/cmdline
echo EXFAT_COMMON_V2_LINUX_HANDOFF_PASS
GUEST
chmod +x "$ram/init"
(cd "$ram" && find . -print0 | cpio --null -o -H newc --owner=0:0 --quiet | gzip -1) > "$work/native.initrd"
truncate -s 256M "$media"
mkfs.exfat -n EXFATBOOT "$media"
"$writer" "$module" "$media" "$kernel" "$work/native.initrd"
fsck.exfat -n "$media"
sha256sum "$media" > "$work/before.sha256"
truncate -s 64M "$esp"
mkfs.fat -F 32 "$esp"
mmd -i "$esp" ::/EFI ::/EFI/BOOT ::/EFI/BOOT/filesystems_x64
mcopy -i "$esp" "$boot" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$esp" "$module" ::/EFI/BOOT/filesystems_x64/exFAT.so
truncate -s 350M "$disk"
sgdisk --clear --new=1:2048:+64M --typecode=1:ef00 --new=2:133120:+256M --typecode=2:8300 "$disk"
partuuid=$(sgdisk --info=2 "$disk" | awk '/Partition unique GUID: / { print $4 }')
[[ $partuuid =~ ^[[:xdigit:]]{8}(-[[:xdigit:]]{4}){3}-[[:xdigit:]]{12}$ ]]
cat > "$work/boot.conf" <<BOOT
timeout 1
textonly true
use_nvram false
driver_diagnostics true
scanfor manual
menuentry "InfiltratorOS kernel from exFAT.so" {
    volume $partuuid
    filesystem_module exfat
    loader \System\Kernel\Images\native.img
    initrd \System\Kernel\Images\native.initrd
    linux_load memory
    ostype Linux
    options "console=ttyS0 rdinit=/init panic=1 exfat_so_trial"
}
BOOT
mcopy -i "$esp" "$work/boot.conf" ::/EFI/BOOT/
dd if="$esp" of="$disk" bs=1M seek=1 conv=notrunc status=none
dd if="$media" of="$disk" bs=1M seek=65 conv=notrunc status=none
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$work/vars.fd"
if ! timeout 150 qemu-system-x86_64 -machine q35,accel=tcg -cpu max -m 768 \
   -display none -monitor none -net none -no-reboot -serial "file:$work/serial.log" \
   -drive "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd" \
   -drive "if=pflash,format=raw,file=$work/vars.fd" \
   -drive "file=$disk,format=raw,if=none,id=bootdisk" \
   -device nvme,drive=bootdisk,serial=exfat-qualification ||
   ! grep -Fq EXFAT_COMMON_V2_LINUX_HANDOFF_PASS "$work/serial.log"; then
  tail -n 100 "$work/serial.log" >&2;exit 1
fi
grep -Fq "validated exFAT.so" "$work/serial.log"
grep -Fq "close /System/Kernel/Images/native.img: $(stat -c %s "$kernel") bytes read through module" "$work/serial.log"
grep -Fq "close /System/Kernel/Images/native.initrd: $(stat -c %s "$work/native.initrd") bytes read through module" "$work/serial.log"
dd if="$disk" bs=1M skip=65 count=256 status=none | cmp - "$media"
(cd "$work" && sha256sum -c before.sha256)
echo EXFAT_COMMON_V2_LINUX_HANDOFF_PASS
