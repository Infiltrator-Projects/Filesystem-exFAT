# exFAT 0.1.0 standalone release scope

The two required outputs are an independently built full exFAT.so and an original
Linux exfat.ko built against precisely Linux 6.12.107. Tests must pass on the
same commits that generate released bytes. The .deb installs only the .so under
/System/Filesystems; the .ko is a separate version-pinned artifact and must
never be inserted into an arbitrary running kernel.

exFAT.so retains the V3/V4 FatFs read/write/format paths and the 64-bit exFAT
V5 ABI. Its independent fixture uses exfatprogs and foreign FAT32 controls.
exfat.ko retains the original Samsung-origin Linux sources and must pass
QEMU mount/write/rename/remount/fsck on disposable images. Publication is
gated on both lanes.

The later generic kernel-side SO loader is deliberately excluded. exFAT Boot
SO and Defragmenter engines are also separate outstanding integration work.
This status does not claim full end-to-end completion or physical drive tests.
