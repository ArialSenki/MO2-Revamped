"""The bug report's redaction of Seamless Co-op's password, wherever the key is
on a line (not shipped).

    py -3.12 -I source/revamped/tests/stability/test_report_redaction.py

MO2_RUNTIME points to an MO2 Revamped folder (default D:\\mo2build\\complete-dev)
whose plugin_python has mobase.
"""

from __future__ import annotations

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
sys.path.insert(0, str(REPO / "source/revamped/plugins"))

import mo2_revamped_info  # noqa: E402


class CoopPassword(unittest.TestCase):
    def setUp(self) -> None:
        self.redact = mo2_revamped_info._Redactor()

    def check(self, text: str) -> str:
        out = self.redact(text)
        self.assertNotIn("two", out)
        self.assertNotIn("words", out)
        self.assertIn("<removed>", out)
        return out

    def test_at_the_start_of_a_line(self) -> None:
        self.check("cooppassword = two words\n")

    def test_in_the_middle_of_a_log_line(self) -> None:
        self.check("[W] note: cooppassword = two words\n")

    def test_in_json(self) -> None:
        out = self.check('{"cooppassword": "two words", "x": 1}')
        self.assertIn('"x": 1', out)

    def test_after_a_byte_order_mark(self) -> None:
        self.check("\ufeffcooppassword = two words\n")

    def test_other_lines_stay(self) -> None:
        self.assertEqual(self.redact("save_file_extension = co2\n"), "save_file_extension = co2\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
