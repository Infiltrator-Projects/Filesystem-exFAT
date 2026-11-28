# Filesystem-exFAT

## 1. Purpose and format scope

Microsoft exFAT; embedded format-selected ChaN FatFs engine. This repository owns the format-specific implementation of `exFAT.so` for InfiltratorOS.

## 2. On-disk architecture and invariants

Boot regions, allocation bitmap, FAT and directory entry sets jointly identify allocation. Distinguish contiguous NoFatChain streams from FAT-linked chains, validate entry-set and name metadata, and use host logical sector geometry during formatting.

## 3. Common V2 implementation and capabilities

The source declares hosted probe, mount, stat, enumeration, read, write, create, volume query, sync and format; the read-only profile does not expose the writable callbacks. Native read faults are preserved across namespace operations. The canonical entry point is `infiltrator_filesystem_api_v2`. Consumers must negotiate the actual profile and operation masks and use bounded, partition-relative host I/O. A source-level callback does not by itself establish that every format variant or host profile is qualified.

## 4. Integrity, mutation, allocation and recovery

Exclusive durable host I/O is required for writing. Hosted and freestanding qualification are separate; do not infer production BOOT acceptance, arbitrary relocation or recovery from ordinary file operations. For destructive operations, require the applicable exclusive writable host, durability contract and format-specific validation. Never silently substitute a kernel or FUSE driver or treat a rejected or unavailable operation as success. Preserve original disk-format semantics; relocation and recovery are separate from ordinary file I/O.

## 5. Build, installation and release

The target installation is `/System/Filesystems/exFAT.so` and `/System/Resources/Documentation/Filesystem-exFAT.pdf`. The project release policy is a portable amd64 DEB and a CPU-native source-first RUN, with GitHub's automatic source archive; do not treat this policy statement as verification of the current installer payload. The release workflow is the authoritative executable build and package specification. Historical adapters and test artifacts are not additional production filesystem authorities.

## 6. Qualification and limitations

[Recorded qualification workflow](https://github.com/Infiltrator-Projects/Filesystem-exFAT/actions/runs/45526922601) · [Current workflows](https://github.com/Infiltrator-Projects/Filesystem-exFAT/actions?query=branch%3Amain). A successful workflow establishes the tested scope only, not complete original-format parity, physical-media crash safety, or production BOOT/kernel integration. Use disposable images for destructive qualification.

## 7. Technical references and provenance

- [Provider implementation](module/exfat_common.c)
- [Native design or source provenance](native/filesystems/exfat/DESIGN.md)
- [Migration and source history](docs/RELEASE_SCOPE.md)
- [Original-format reference source](docs/exFAT.pdfstreams) and [PDF](docs/exFAT.pdf)
- [Project philosophy](docs/PHILOSOPHY.md)
- [Common ABI](https://github.com/Infiltrator-Projects/Libraries)

The original-format reference and historical migration notes describe different evidence from the current production provider. When descriptions disagree, the current exported descriptor, implementation, executable tests and exact-revision release evidence take precedence.

The [engineering reference](docs/exFAT-TECHNICAL-REFERENCE.md) is the unified technical manual; worked examples are integrated into the appropriate chapters.

Hosted, firmware and external test builds use the upstream Common V2 header. The source archive and native installer resolve the same immutable Libraries revision as the Git checkout.
