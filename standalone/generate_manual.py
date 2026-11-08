#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the canonical exFAT manual for the CMake install path.

The source-embedded native RUN and portable DEB already generate their manuals
via standalone/manual.py; this entry point keeps direct CMake installs equal.
"""
from pathlib import Path
import re
import sys

from manual import build


def main() -> None:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: generate_manual.py VERSION_FILE OUTPUT_PDF")
    version_file = Path(sys.argv[1])
    version = version_file.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise SystemExit("Invalid exFAT release version")
    source = Path(__file__).resolve().parents[1] / "docs/exFAT.pages"
    destination = Path(sys.argv[2])
    destination.parent.mkdir(parents=True, exist_ok=True)
    build(destination, source, version)


if __name__ == "__main__":
    main()
