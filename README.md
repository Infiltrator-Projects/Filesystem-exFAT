# Filesystem-exFAT

**Independent canonical exFAT source owner for InfiltratorOS.** This repository contains genuine C filesystem source copied from the qualified candidate `Infiltrator-Projects/Filesystems` commit `41d2db4610320348831d5cf04a28b6a32a48c7af`, with original paths, third-party attributions and source notices preserved.

The shared `native/filesystems/fat/portable/` ChaN FatFs engine is compiled **only with the exFAT format selector (IFS_FATFS_ONLY=64)** into `exFAT.so`. The independent `native/filesystems/exfat/module/` exposes V3/V4 filesystem functions and a V5 64-bit file size/offset API. The real Linux `exfat.ko` implementation is under `native/filesystems/exfat/linux/` for separate kernel compilation and runtime checks. We do not redistribute the other FAT12, FAT16 or FAT32 drivers from here.

Run `make test` on a Linux host with `exfatprogs` and `dosfstools`; the image tests use only disposable temporary regular files, independently formatted exFAT and FAT32 media and checksum tools. No system partition, UEFI variables or physical media are modified.

**Outstanding:** directory moves are deliberately refused pending ancestry checks, BOOT/kernel generic shared-object loading, full interrupted-write recovery, all capacity/error profiles and firmware execution are not yet qualified. The Linux .ko is a separate transitional implementation. Do not claim that these source imports constitute a complete filesystem or an installed/released driver.
