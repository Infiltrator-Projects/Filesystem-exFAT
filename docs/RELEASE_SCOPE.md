# exFAT standalone release scope

The canonical `exFAT.so` exposes Common V2 hosted filesystem operations and an embedded import-free freestanding reader. Both profiles are qualified separately against disposable real exFAT media, including 512-byte and 4 KiB transfer geometry, negative FAT32 controls, corrupted boot checksums and inconsistent geometry. The package tests exercise the actual extracted shared objects; physical BOOT handoff and general Common V2 relocation/recovery remain separate qualifications.

The portable amd64 DEB installs only `/System/Filesystems/exFAT.so` and `/System/Resources/Documentation/Filesystem-exFAT.pdf`. The CPU-native RUN fetches its exact Git revision, verifies it and builds the aggressive local package with LTO and two-pass PGO. It contains no source archive or DEB. No per-format kernel module, separate EFI driver or FUSE fallback is distributed. Checksums are retained in CI evidence, not as additional release assets. GitHub generates source archives.

See the [release workflow](../.github/workflows/release.yml), [README](../README.md) and [immutable releases](https://github.com/Infiltrator-Projects/Filesystem-exFAT/releases). Historical Linux-driver qualification is not a current package requirement.
