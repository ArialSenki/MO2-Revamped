"""mobase.getFileVersion / getProductVersion on files that aren't real exes.

Outside MO2 there is no default logger, and uibase's failure path logs before it
returns "": the first log line dereferenced the empty logger and killed the
process with an access violation, no exception. The checks run in a child
process so a regression shows as a failed test instead of killing the suite.
"""

from __future__ import annotations

import json
import os
import random
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
HARNESS = HERE.parent / "cyberpunk2077"

CHILD = r"""
import json, sys
sys.path.insert(0, sys.argv[1])
import harness
harness.setup_mobase()
import mobase
out = {}
for path in sys.argv[2:]:
    out[path] = [mobase.getFileVersion(path), mobase.getProductVersion(path)]
print("RESULT " + json.dumps(out), flush=True)
"""


class FileVersionOnBrokenFiles(unittest.TestCase):
    def setUp(self) -> None:
        self.dir = Path(tempfile.mkdtemp(prefix="gfv-"))
        self.addCleanup(shutil.rmtree, self.dir, ignore_errors=True)

    def _versions(self, paths: list[Path]) -> dict[str, list[str]]:
        proc = subprocess.run(
            [sys.executable, "-I", "-X", "faulthandler", "-c", CHILD, str(HARNESS)]
            + [str(p) for p in paths],
            capture_output=True,
            text=True,
            timeout=120,
            env=os.environ.copy(),
        )
        self.assertEqual(
            proc.returncode, 0, f"child died ({proc.returncode:#x}):\n{proc.stderr}"
        )
        line = next(
            (x for x in proc.stdout.splitlines() if x.startswith("RESULT ")), None
        )
        self.assertIsNotNone(line, proc.stdout + proc.stderr)
        return json.loads(line[len("RESULT ") :])

    def test_broken_files_give_empty_and_real_exe_keeps_its_version(self) -> None:
        real = Path(sys.executable)
        data = real.read_bytes()
        random.seed(2612)
        broken = {
            "empty.exe": b"",
            "garbage.exe": bytes(random.randrange(256) for _ in range(8192)),
            "header_only.exe": data[:1024],
            "half.exe": data[: len(data) // 2],
            "no_tail.exe": data[: len(data) - 512],
        }
        paths = []
        for name, content in broken.items():
            path = self.dir / name
            path.write_bytes(content)
            paths.append(path)
        missing = self.dir / "missing.exe"
        paths.append(missing)
        paths.append(real)

        got = self._versions(paths)

        for path in paths[:-1]:
            file_version, product_version = got[str(path)]
            # a truncated copy may still carry its whole version resource; then
            # it must read the same as the real exe, never garbage
            if file_version:
                self.assertEqual(got[str(real)][0], file_version, path.name)
            else:
                self.assertEqual("", product_version, path.name)
        self.assertEqual(["", ""], got[str(self.dir / "empty.exe")])
        self.assertEqual(["", ""], got[str(self.dir / "garbage.exe")])
        self.assertEqual(["", ""], got[str(missing)])

        file_version, product_version = got[str(real)]
        major_minor = f"{sys.version_info.major}.{sys.version_info.minor}."
        self.assertTrue(file_version.startswith(major_minor), file_version)
        self.assertEqual(4, len(file_version.split(".")), file_version)
        self.assertTrue(product_version.startswith(major_minor), product_version)


if __name__ == "__main__":
    unittest.main()
