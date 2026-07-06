#!/usr/bin/env python3
"""Rewrite the example's external_components source to the local checkout.

The published example installs this component from GitHub
(`source: github://zacs/esphome-person_detector`). In CI that would try to clone
the (possibly private) repo over unauthenticated HTTPS and fail, and it would
test GitHub's copy rather than the checked-out code. Swap it for a local source
so CI validates/builds exactly what is in the working tree.
"""

import pathlib
import sys

EXAMPLE = pathlib.Path("example/reterminal_d1001.yaml")
GITHUB_SOURCE = "  - source: github://zacs/esphome-person_detector"
LOCAL_SOURCE = "  - source:\n      type: local\n      path: ../components"

text = EXAMPLE.read_text()
if GITHUB_SOURCE not in text:
    sys.exit(f"Expected source line not found in {EXAMPLE}")
EXAMPLE.write_text(text.replace(GITHUB_SOURCE, LOCAL_SOURCE))
print(f"Rewrote {EXAMPLE} to use local component source:\n")
print(EXAMPLE.read_text())
