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


class NexusSignIn(unittest.TestCase):
    """The tokens and the code of "Sign in with Nexus Mods" (OAuth)."""

    def setUp(self) -> None:
        self.redact = mo2_revamped_info._Redactor()

    def test_tokens_in_json(self) -> None:
        out = self.redact('{"access_token":"AT-SECRET-1","refresh_token":"RT-SECRET-2",'
                          '"expires_in":3600}')
        self.assertNotIn("AT-SECRET-1", out)
        self.assertNotIn("RT-SECRET-2", out)
        self.assertIn('"expires_in":3600', out)

    def test_code_in_redirect(self) -> None:
        out = self.redact("GET /?code=CODE-SECRET-3&state=abc HTTP/1.1\n")
        self.assertNotIn("CODE-SECRET-3", out)
        self.assertIn("state=abc", out)

    def test_code_and_verifier_in_form(self) -> None:
        out = self.redact("grant_type=authorization_code&code=CODE-4&code_verifier=VER-5\n")
        self.assertNotIn("CODE-4", out)
        self.assertNotIn("VER-5", out)
        self.assertIn("grant_type=authorization_code", out)

    def test_bearer_header(self) -> None:
        jwt = "eyJhbGciOiJSUzI1NiJ9.eyJzdWIiOiIxMjM0In0.c2lnbmF0dXJlLXZhbHVl"
        out = self.redact("Authorization: Bearer " + jwt + "\n")
        self.assertNotIn(jwt, out)
        out = self.redact("sent with bearer " + jwt)
        self.assertNotIn("c2lnbmF0dXJl", out)

    def test_ordinary_codes_stay(self) -> None:
        text = "exit code 0, error code: 5\n"
        self.assertEqual(self.redact(text), text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
