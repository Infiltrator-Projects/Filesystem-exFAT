# Filesystem-exFAT

**Independent canonical exFAT source owner for InfiltratorOS.** This repository contains genuine C filesystem source copied from the qualified candidate `Infiltrator-Projects/Filesystems` commit `41d2db4610320348831d5cf04a28b6a32a48c7af`, with original paths, third-party attributions and source notices preserved.

The shared `native/filesystems/fat/portable/` ChaN FatFs engine is compiled **only with the exFAT format selector (IFS_FATFS_ONLY=64)** into `exFAT.so`. The independent `native/filesystems/exfat/module/` exposes V3/V4 filesystem functions and a V5 64-bit file size/offset API. The real Linux `exfat.ko` implementation is under `native/filesystems/exfat/linux/` for separate kernel compilation and runtime checks. We do not redistribute the other FAT12, FAT16 or FAT32 drivers from here.

Run `make test` on a Linux host with `exfatprogs` and `dosfstools`; the image tests use only disposable temporary regular files, independently formatted exFAT and FAT32 media and checksum tools. No system partition, UEFI variables or physical media are modified.

**Independent 0.1.0 release qualification:** an exact portable `exFAT.so` exposes the original FatFs V3/V4 read/write/format and exFAT V5 64-bit file operations, checked against exfatprogs images and foreign FAT32 rejection. The preserved Linux `exfat.ko` is compiled against Linux 6.12.107 separately and must pass an actual QEMU boot, read/write/rename, remount and offline fsck before publishing. The package installs only `/System/Filesystems/exFAT.so`; the pinned `.ko` is a separate release asset. The generic future kernel SO bridge is **not** a release gate.

**Outstanding:** directory moves are deliberately refused pending ancestry checks; Boot through exFAT.so, Defragmenter integration, power-loss recovery, all capacity/error profiles, other kernel versions and physical firmware/media qualification are not yet complete. A published pair of separately qualified SO/KO artifacts is *not* full exFAT end-to-end integration.
