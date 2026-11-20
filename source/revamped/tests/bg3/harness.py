"""Runs the Baldur's Gate 3 plugin's code outside MO2, with MO2's own mobase.

MO2_RUNTIME points to an MO2 Revamped folder (default D:\\mo2build\\complete-dev)
whose plugin_python has mobase.cp312-win_amd64.pyd; run with Python 3.12:

    py -3.12 -I source/revamped/tests/bg3/run_tests.py
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
RUNTIME = Path(os.environ.get("MO2_RUNTIME", r"D:\mo2build\complete-dev"))


def setup_mobase() -> None:
    for folder in (RUNTIME, RUNTIME / "dlls", RUNTIME / "plugins/plugin_python/dlls"):
        if folder.is_dir():
            os.add_dll_directory(str(folder))
    sys.path.insert(0, str(RUNTIME / "plugins/plugin_python/libs"))
    # psutil and the other libraries basic_games bundles
    sys.path.append(str(RUNTIME / "plugins/basic_games/lib"))


def stage_plugins() -> Path:
    """basic_games as the installer stages it: MO2's plugin, then MO2
    Revamped's files over it. Returns the folder to put on sys.path."""
    stage = Path(tempfile.mkdtemp(prefix="bg3-plugins-"))
    shutil.copytree(
        REPO / "source/mo2-python-plugins/basic_games",
        stage / "basic_games",
        ignore=shutil.ignore_patterns("__pycache__", "lib"),
    )
    revamped = REPO / "source/revamped/plugins/basic_games"
    for file in revamped.rglob("*.py"):
        if "__pycache__" in file.parts or "vendor" in file.parts or "me3_compat" in file.parts:
            continue
        target = stage / "basic_games" / file.relative_to(revamped)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
    # the games the tests don't need, so the package imports fast
    games = stage / "basic_games/games"
    for file in games.glob("game_*.py"):
        if file.name != "game_baldursgate3.py":
            file.unlink()
    return stage


def make_tree(paths: list[str]):
    """An IFileTree from paths; a path ending in / is a folder."""
    import mobase

    tree = mobase.private.makeTree()  # type: ignore[attr-defined]
    for path in paths:
        if path.endswith("/"):
            tree.addDirectory(path.rstrip("/"))
        else:
            tree.addFile(path)
    return tree


def tree_paths(tree) -> list[str]:
    import mobase

    paths: list[str] = []

    def visit(path: str, entry) -> mobase.IFileTree.WalkReturn:
        full = (path + entry.name()).replace("\\", "/")
        if isinstance(entry, mobase.IFileTree):
            if len(entry) == 0:
                paths.append(full + "/")
        else:
            paths.append(full)
        return mobase.IFileTree.WalkReturn.CONTINUE

    tree.walk(visit, "/")
    return sorted(paths)
