r"""Runs the Elden Ring plugin's code outside MO2, with MO2's own mobase.

MO2_RUNTIME points to an MO2 Revamped folder (default D:\mo2build\complete-dev)
whose plugin_python has mobase.cp312-win_amd64.pyd; run with Python 3.12:

    py -3.12 -I source/revamped/tests/eldenring/run_tests.py

Nothing here starts the game, MO2 or a window.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
RUNTIME = Path(os.environ.get("MO2_RUNTIME", r"D:\mo2build\complete-dev"))
PLUGINS = REPO / "source/revamped/plugins"


def setup_mobase() -> None:
    for folder in (RUNTIME, RUNTIME / "dlls", RUNTIME / "plugins/plugin_python/dlls"):
        if folder.is_dir():
            os.add_dll_directory(str(folder))
    sys.path.insert(0, str(RUNTIME / "plugins/plugin_python/libs"))
    sys.path.append(str(RUNTIME / "plugins/basic_games/lib"))


def stage_plugins() -> Path:
    """basic_games as the installer stages it: MO2's plugin, then MO2
    Revamped's files over it, with only the Elden Ring game. Returns the
    folder to put on sys.path."""
    stage = Path(tempfile.mkdtemp(prefix="er-plugins-"))
    shutil.copytree(
        REPO / "source/mo2-python-plugins/basic_games",
        stage / "basic_games",
        ignore=shutil.ignore_patterns("__pycache__", "lib"),
    )
    revamped = PLUGINS / "basic_games"
    for file in revamped.rglob("*.py"):
        if "__pycache__" in file.parts or "vendor" in file.parts or "me3_compat" in file.parts:
            continue
        target = stage / "basic_games" / file.relative_to(revamped)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
    games = stage / "basic_games/games"
    for file in games.glob("game_*.py"):
        if file.name != "game_eldenring_mo2.py":
            file.unlink()
    for folder in ("baldursgate3", "cyberpunk2077"):
        shutil.rmtree(games / folder, ignore_errors=True)
    # the archive installer and the tools import the game plugin by this name
    for name in ("eldenring_mo2_archive_installer", "eldenring_mo2_tools"):
        shutil.copytree(PLUGINS / name, stage / name, ignore=shutil.ignore_patterns("__pycache__"))
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
