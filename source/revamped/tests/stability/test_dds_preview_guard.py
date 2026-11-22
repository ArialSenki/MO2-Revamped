"""The DDS preview when its OpenGL modules can't load: its own message, not an
exception into MO2 (not shipped).

    py -3.12 -I source/revamped/tests/stability/test_dds_preview_guard.py

MO2_RUNTIME points to an MO2 Revamped folder (default D:\\mo2build\\complete-dev)
whose plugin_python has mobase.
"""

from __future__ import annotations

import builtins
import os
import sys
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
RUNTIME = Path(os.environ.get("MO2_RUNTIME", r"D:\mo2build\complete-dev"))
for folder in (RUNTIME, RUNTIME / "dlls", RUNTIME / "plugins/plugin_python/dlls"):
    if folder.is_dir():
        os.add_dll_directory(str(folder))
sys.path.insert(0, str(RUNTIME / "plugins/plugin_python/libs"))
sys.path.insert(0, str(REPO / "source/mo2-python-plugins"))
sys.path.insert(0, str(REPO / "source/mo2-python-plugins/data"))
os.environ.setdefault("QT_QPA_PLATFORM_PLUGIN_PATH", str(RUNTIME / "platforms"))

from PyQt6.QtCore import QSize  # noqa: E402
from PyQt6.QtWidgets import QApplication, QLabel, QWidget  # noqa: E402

app = QApplication.instance() or QApplication([])

import DDSPreview  # noqa: E402


class Guard(unittest.TestCase):
    def setUp(self):
        self.real_import = builtins.__import__

        def no_opengl(name, *args, **kwargs):
            if name.startswith("DDS.DDSFile") or name.startswith("PyQt6.QtOpenGL"):
                raise ImportError(f"DLL load failed while importing {name}")
            return self.real_import(name, *args, **kwargs)

        sys.modules.pop("DDS.DDSFile", None)
        builtins.__import__ = no_opengl

    def tearDown(self):
        builtins.__import__ = self.real_import

    def texts(self, widget: QWidget) -> str:
        return " ".join(label.text() for label in widget.findChildren(QLabel))

    def test_file_preview_gives_its_message(self):
        widget = DDSPreview.DDSPreview().genFilePreview("x.dds", QSize(100, 100))
        self.assertIsInstance(widget, QWidget)
        self.assertTrue(self.texts(widget))

    def test_data_preview_gives_its_message(self):
        widget = DDSPreview.DDSPreview().genDataPreview(b"DDS ", "x.dds", QSize(100, 100))
        self.assertIsInstance(widget, QWidget)
        self.assertTrue(self.texts(widget))


if __name__ == "__main__":
    unittest.main(verbosity=2)
