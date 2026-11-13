# Filesystem-exFAT

This repository owns the C exFAT filesystem implementation and its distributable
`exFAT.so`. The embedded ChaN FatFs engine is compiled with the exFAT selector
`IFS_FATFS_ONLY=64`. The original notices and third-party attributions are
preserved from the [source import](https://github.com/Infiltrator-Projects/Filesystems/tree/41d2db4610320348831d5cf04a28b6a32a48c7af).

## Common V2

`exFAT.so` exports `infiltrator_filesystem_api_v2` for the hosted Common V2
profile. It owns filesystem detection, file and directory operations, volume
query, sync and formatting. Formatting uses the host's logical sector size.
Writes require an exclusive target and durable host I/O.

The shared object uses Linux runtime libraries. BOOT requires a freestanding
firmware-compatible module, so its loader rejects the published exFAT build.
The Common V2 relocation and recovery group is pending.

## Build and install

Run `make test` with `exfatprogs` and `dosfstools` to qualify the shared object
on disposable exFAT and FAT32 images. The native installer compiles from its
embedded source and qualifies the hardware-optimised build with measured
two-pass profile-guided optimisation. Use `--build-only --output DIRECTORY`
to build a Debian package. Use `--kernel-build DIRECTORY` to supply a prepared
kernel build with `Module.symvers`.

The installer manages the module, generated manual and kernel adapter.

- `/System/Filesystems/exFAT.so`
- `/System/Resources/Documentation/Filesystem-exFAT.pdf`
- `/System/Kernel/Filesystems/exfat.ko`

InfiltratorOS uses `--system-package-mode` and manages these outputs
individually. The separate Linux adapter is built and tested against its
matching kernel. See the [release scope](docs/RELEASE_SCOPE.md).
