"""Runs the Baldur's Gate 3 plugin tests (see harness.py)."""

from __future__ import annotations

import faulthandler
import os
import sys


if "--hang-dump" in sys.argv:
    faulthandler.dump_traceback_later(30, exit=True)
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import harness  # noqa: E402

harness.setup_mobase()
# the progress windows and message boxes need QApplication (nothing is shown:
# the tests replace them); MO2's runtime has only the windows platform
os.environ.setdefault("QT_QPA_PLATFORM_PLUGIN_PATH", str(harness.RUNTIME / "platforms"))
sys.path.insert(0, str(harness.stage_plugins()))

from PyQt6.QtWidgets import QApplication  # noqa: E402

app = QApplication.instance() or QApplication([])

suite = unittest.defaultTestLoader.discover(str(HERE), pattern="test_*.py")
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(0 if result.wasSuccessful() else 1)
