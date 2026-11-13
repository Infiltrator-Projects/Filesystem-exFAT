# exFAT standalone release scope

`exFAT.so` exposes the hosted Common V2 filesystem ABI. Its qualification
uses independently formatted exFAT images and FAT32 negative controls.
Firmware execution and Common V2 relocation and recovery remain pending.

The release builds `exfat.ko` separately against Linux 6.12.107. Its QEMU
qualification mounts an exFAT image, writes and renames files, remounts it
and checks it offline. Publication requires the shared-object and kernel
tests on the release commit.

The released Debian package contains the shared object, generated manual
and kernel adapter. The adapter matches the tested kernel build and is
stored under `/System/Kernel/Filesystems` for separate use.
