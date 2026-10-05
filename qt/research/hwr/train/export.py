#!/usr/bin/env python3
"""See xqt_hwr/export.py (python export.py --help)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xqt_hwr.export import main  # noqa: E402

if __name__ == "__main__":
    main()
