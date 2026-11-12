**0.1.4 Python-native packaging:** Portable `.deb` and source-embedded native
`.run` now use the same `standalone/release.py` / `standalone/build.py`
structure as FAT and OFS. The portable package owns exactly `exFAT.so` and
`exFAT.pdf`; the native installer recompiles the shared object with measured
two-pass PGO and LTO, builds a `+native1` Debian package, and verifies its
installed bytes and package ownership. Real-image exFAT/FAT32 qualification
runs during both native training and final verification. The separate
Linux 6.12 `exfat.ko` build and QEMU checks remain mandatory.

# Filesystem-exFAT


**Source ownership and release boundary:** This repository owns only the exFAT filesystem engine and its single distributable `exFAT.so`. Common support code is compiled into that module where required, never published as an additional filesystem driver. CI rejects foreign format source directories, provider submodules, extra shared-object installation targets, and unexpected package payloads. The release manual is generated from `docs/exFAT.pages`; the checked-in PDF and preview files are reference copies, not separate installed manuals.
**One repository, one distributable filesystem library:** `exFAT.so` contains this filesystem’s read/write, format, defragmentation and test-media interfaces. `exFAT-legacy.so` is a regression-test artifact only. No other filesystem driver is a release or installable artifact.

**Independent canonical exFAT source owner for InfiltratorOS.** This repository contains genuine C filesystem source copied from the qualified candidate `Infiltrator-Projects/Filesystems` commit `41d2db4610320348831d5cf04a28b6a32a48c7af`, with original paths, third-party attributions and source notices preserved.

The embedded, format-neutral ChaN FatFs source now lives at `native/portable/fatfs/` within **this exFAT repository**. It is compiled **only with the exFAT selector (`IFS_FATFS_ONLY=64`)** into `exFAT.so`; this repository never builds or installs FAT12, FAT16 or FAT32 `.so` files. The independent `native/filesystems/exfat/module/` exposes V3/V4 filesystem functions and a V5 64-bit file size/offset API. The real Linux `exfat.ko` implementation is under `native/filesystems/exfat/linux/` for separate kernel compilation and runtime checks. We do not redistribute the other FAT12, FAT16 or FAT32 drivers from here.

Run `make test` on a Linux host with `exfatprogs` and `dosfstools`; the image tests use only disposable temporary regular files, independently formatted exFAT and FAT32 media and checksum tools. No system partition, UEFI variables or physical media are modified.

**Independent 0.1.0 release qualification:** an exact portable `exFAT.so` exposes the original FatFs V3/V4 read/write/format and exFAT V5 64-bit file operations, checked against exfatprogs images and foreign FAT32 rejection. The preserved Linux `exfat.ko` is compiled against Linux 6.12.107 separately and must pass an actual QEMU boot, read/write/rename, remount and offline fsck before publishing. The package installs only `/System/Filesystems/exFAT.so`; the pinned `.ko` is a separate release asset. The generic future kernel SO bridge is **not** a release gate.

**Outstanding:** directory moves are deliberately refused pending ancestry checks; Boot through exFAT.so, Defragmenter integration, power-loss recovery, all capacity/error profiles, other kernel versions and physical firmware/media qualification are not yet complete. A published pair of separately qualified SO/KO artifacts is *not* full exFAT end-to-end integration.

## Hardware-native source installer

Since 0.1.2, the independent `filesystem-exfat-VERSION-native-installer.run`
contains the committed source and supports `--profile aggressive --system-package-mode`.
It optimises for the local CPU with `-O3 -march=native -mtune=native -flto`,
uses real exFAT test images for GCC PGO training, and rebuilds the final
freestanding driver from those measured profiles. A failed or missing
training profile aborts installation. The training runtime is never installed.
The resulting Debian package owns only exFAT.so and the illustrated exFAT.pdf.
The separately qualified kernel module remains a release asset only.


## Unified module release contract

The **default `make` and CMake `exfat-unified` target** now produce the full
`exFAT.so`: V3/V4/V5 read/write/format, 64-bit file operations, plus the
separately versioned Defragment/Growth Defrag/Recover entrypoint. The older
freestanding file-API-only build is retained strictly as
`exFAT-legacy.so` for original ABI and no-DT_NEEDED regression tests.
It is **not** the distributable module.

`make test` validates both artifacts against independent exfatprogs images,
including rejecting foreign FAT32 volumes. The native `.run` profiles
the unified object through those real-media operations in two GCC passes.
The Debian package still installs only `exFAT.so` and `exFAT.pdf`; the normal
Linux OpenSSL, SQLite and C runtime libraries are declared package
dependencies rather than misrepresented as a freestanding image.
The application may load `exFAT.so` to defragment as well as create Test Media.

**0.1.3 reliability repair:** The standalone native installer now probes PGO
training output without a SIGPIPE-prone find/grep pipeline and writes the
complete exported ELF symbol table before checking V3/V4/V5 and exFAT
defragmentation symbols. Under strict `set -euo pipefail`, successful native
compilation and media verification can no longer be misclassified as exit 141.
Both source-embedded RUN and Debian release qualification remain mandatory.

## Installer files

- `/System/Filesystems/exFAT.so`
- `/System/Resources/Documentation/Filesystem-exFAT.pdf`
- `/System/Kernel/Filesystems/exfat.ko`

The published Debian package installs these three regular files. A manual native RUN compiles the shared object and Linux adapter locally, generates the PDF, and replaces all three files. It uses the running kernel headers by default. Use `--kernel-build DIRECTORY` for a prepared kernel build with `Module.symvers`. The kernel module is stored for manual use.

InfiltratorOS calls `--system-package-mode` and decides which outputs need updating. Its worker builds a missing or older SO, repairs the PDF independently, and stores the matching released kernel module when available. Use `--build-only --output DIRECTORY` to build a Debian package.

## Common V2 filesystem ABI

`exFAT.so` exposes `infiltrator_filesystem_api_v2` for file and directory
operations, volume query, sync and formatting through the native 64-bit
exFAT engine. Formatting uses the logical sector size supplied by the host.
Writes require exclusive access. Relocation and recovery still use the
dedicated Defragmenter interface. Kernel and firmware execution require
separate qualification.
