"""Runs the Valheim plugin tests (see ../cyberpunk2077/harness.py)."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "cyberpunk2077"))

import harness  # noqa: E402

harness.setup_mobase()
sys.path.insert(0, str(harness.stage_plugins(keep=("game_valheim.py",))))

from PyQt6.QtCore import QCoreApplication  # noqa: E402

app = QCoreApplication.instance() or QCoreApplication([])
suite = unittest.defaultTestLoader.discover(str(HERE), pattern="test_*.py")
result = unittest.TextTestRunner(verbosity=2).run(suite)
sys.exit(0 if result.wasSuccessful() else 1)
