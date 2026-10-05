#!/usr/bin/env python3
"""See xqt_hwr/prepare.py (python prepare.py --help)."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xqt_hwr.prepare import main  # noqa: E402

if __name__ == "__main__":
    main()
