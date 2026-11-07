#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep exFAT independently owned without constructing another FAT-family .so."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parent.parent
cfg = json.loads((root / "standalone/config.json").read_text())
assert cfg["repository"] == "Filesystem-exFAT"
assert cfg["module"] == "exFAT.so"
assert cfg["pdf"] == "docs/exFAT.pdf"
assert cfg["abi"] == "infiltrator_filesystem_exfat_defrag_v1"
assert "backend_revision" not in cfg
cmake = (root / "CMakeLists.txt").read_text()
assert len(re.findall(r"\badd_library\([^\n]*\bSHARED\b", cmake)) == 1
assert 'OUTPUT_NAME "exFAT"' in cmake
assert "install(TARGETS exfat-unified LIBRARY DESTINATION /System/Filesystems)" in cmake
assert "IFS_FATFS_ONLY=64" in cmake
for unwanted in ("fat", "ffs", "ofs", "ext2", "ext3", "ext4"):
    assert not (root / "native/filesystems" / unwanted).exists(), unwanted
fatfs = root / "native/portable/fatfs"
assert (fatfs / "ff.c").is_file()
assert (fatfs / "fatfs_module.c").is_file()
assert "#include \"../../../portable/fatfs/fatfs_module.c\"" in (root / "native/filesystems/exfat/module/exfat_module.c").read_text()
assert "#include \"../../../portable/fatfs/fatfs_module.h\"" in (root / "native/filesystems/exfat/module/exfat_api.h").read_text()
builder = (root / "standalone/build.py").read_text()
assert "native/portable/fatfs" in builder
assert "backend-ext4" not in builder
assert "Filesystem-EXT4" not in builder
assert "backend_revision" not in builder
# Every repository owns exactly one format implementation directory; shared
# portable support may live elsewhere, but foreign format trees may not return.
filesystem_root = root / "native/filesystems"
assert {p.name for p in filesystem_root.iterdir() if p.is_dir()} == {"exfat"}
assert not (root / "native/providers").exists()
# Installation builds exactly one corresponding filesystem SO. Additional
# test executables and freestanding regression SOs must remain noninstallable.
assert len(re.findall(r"\\binstall\\(TARGETS\\s+[^)]+\\)", cmake)) == 1
assert (root / "standalone/config.json").is_file()
release_source = (root / "standalone/release.py").read_text()
assert 'expected = {Path(f"System/Filesystems/{MODULE}"), Path(f"System/Resources/Documentation/{REPO}.pdf")}' in release_source
assert 'if actual != expected:' in release_source
print("EXFAT-ISOLATED-SO-SOURCE-OWNER-PASS")
