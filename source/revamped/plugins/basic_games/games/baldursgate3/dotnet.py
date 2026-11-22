""".NET runtimes: Divine.exe (LSLib) and BG3 Mod Manager need the .NET 8
Desktop Runtime and close at once without it.

From MO2 Revamped's fixes to basic_games' Baldur's Gate 3 plugin.
"""

from __future__ import annotations

import os
import typing
from pathlib import Path

DOWNLOAD_URL = "https://dotnet.microsoft.com/download/dotnet/{0}.0"
BG3MM_BINARY = "bg3modmanager.exe"


def _dotnet_roots() -> list[Path]:
    roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "dotnet",
    ]
    if os.environ.get("DOTNET_ROOT"):
        roots.insert(0, Path(os.environ["DOTNET_ROOT"]))
    return roots


def _installed_majors(framework: str) -> set[int]:
    majors: set[int] = set()
    for root in _dotnet_roots():
        try:
            for d in (root / "shared" / framework).iterdir():
                head = d.name.split(".", 1)[0]
                if d.is_dir() and head.isdigit():
                    majors.add(int(head))
        except OSError:
            continue
    return majors


def missing_dotnet_major(exe: Path) -> typing.Optional[int]:
    """MO2 Revamped: the major version of a .NET framework that the program's
    runtimeconfig.json asks for and this PC doesn't have, or None (all there,
    or a self-contained build). With rollForward Major/LatestMajor a newer
    major does too."""
    import json

    config = exe.with_name(exe.stem + ".runtimeconfig.json")
    if not config.is_file():
        # a self-contained build carries the runtime next to it; a single-file
        # build has its runtimeconfig inside the exe: the .NET 8 it always used
        if any(exe.with_name(dll).is_file() for dll in ("hostfxr.dll", "coreclr.dll")):
            return None
        return None if 8 in _installed_majors("Microsoft.WindowsDesktop.App") else 8
    try:
        options = json.loads(config.read_text(encoding="utf-8-sig"))["runtimeOptions"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    # MO2 Revamped: "runtimeOptions": 5 or "frameworks": 5 raised
    # AttributeError or TypeError below
    if not isinstance(options, dict):
        return None
    frameworks = options.get("frameworks") or [options.get("framework") or {}]
    if not isinstance(frameworks, list):
        return None
    roll = str(options.get("rollForward", "")).casefold()
    for fw in frameworks:
        name = fw.get("name", "") if isinstance(fw, dict) else ""
        head = str(fw.get("version", "")).split(".", 1)[0] if name else ""
        if not head.isdigit():
            continue
        want = int(head)
        have = _installed_majors(name)
        ok = (
            any(m >= want for m in have)
            if roll in ("major", "latestmajor")
            else want in have
        )
        if not ok:
            return want
    return None


