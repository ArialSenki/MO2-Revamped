"""The bug report finds the logs and the last crash dump of an instance whose
base directory is elsewhere (a linked Nolvus or Wabbajack list): MO2 writes
them into the instance's own folder, not into the base directory (not shipped).

    py -3.12 -I source/revamped/tests/stability/test_report_linked_instance.py

MO2_RUNTIME points to an MO2 Revamped folder (default D:\\mo2build\\complete-dev)
whose plugin_python has mobase.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
RUNTIME = Path(os.environ.get("MO2_RUNTIME", r"D:\mo2build\complete-dev"))
for folder in (RUNTIME, RUNTIME / "dlls", RUNTIME / "plugins/plugin_python/dlls"):
    if folder.is_dir():
        os.add_dll_directory(str(folder))
sys.path.insert(0, str(RUNTIME / "plugins/plugin_python/libs"))
sys.path.insert(0, str(REPO / "source/revamped/plugins"))

from PyQt6.QtCore import QCoreApplication  # noqa: E402

import mo2_revamped_info  # noqa: E402

APP = QCoreApplication.instance() or QCoreApplication([])


class FakeOrganizer:
    def __init__(self, base: Path):
        self._base = base

    def basePath(self) -> str:
        return str(self._base)


class LinkedInstance(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        root = Path(self._tmp.name)
        # the instance's own folder, with what MO2 wrote, and its mods elsewhere
        self.instance = root / "PortableInstances" / "Linked"
        self.base = root / "elsewhere" / "MODS"
        (self.instance / "logs").mkdir(parents=True)
        (self.instance / "crashDumps").mkdir()
        self.base.mkdir(parents=True)
        (self.instance / "logs" / "mo_interface.log").write_text("[D] a line\n", encoding="utf-8")
        (self.instance / "crashDumps" / "ModOrganizer-1.dmp").write_bytes(b"MDMP" + bytes(2048))
        self.tool = mo2_revamped_info.Mo2RevampedInfo()
        self.tool._organizer = FakeOrganizer(self.base)

    def tearDown(self) -> None:
        APP.setProperty("dataPath", None)
        self._tmp.cleanup()

    def test_dump_and_logs_of_a_linked_instance(self) -> None:
        APP.setProperty("dataPath", str(self.instance))
        facts = "\n".join(self.tool._crash_dump_facts())
        self.assertIn("ModOrganizer-1.dmp", facts)
        self.assertNotIn("Last crash dump: none", facts)
        self.assertEqual(self.tool._diagnostic_log_dir(), self.instance / "logs")

    def test_base_directory_still_used_without_the_instance_folder(self) -> None:
        # the instance folder isn't known (an older MO2): the base directory, as before
        (self.base / "logs").mkdir()
        (self.base / "crashDumps").mkdir()
        (self.base / "crashDumps" / "ModOrganizer-2.dmp").write_bytes(b"MDMP")
        self.assertIn("ModOrganizer-2.dmp", "\n".join(self.tool._crash_dump_facts()))
        self.assertEqual(self.tool._diagnostic_log_dir(), self.base / "logs")

    def test_no_dump_anywhere(self) -> None:
        APP.setProperty("dataPath", str(self.base))
        self.assertEqual(self.tool._crash_dump_facts(), ["Last crash dump: none"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
