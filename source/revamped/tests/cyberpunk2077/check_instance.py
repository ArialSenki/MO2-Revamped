"""The Cyberpunk 2077 plugin against a real MO2 instance, read only.

    py -3.12 -I source/revamped/tests/cyberpunk2077/check_instance.py <instance folder>

- installer: every download (zip, 7z, rar, listed with Windows' tar) goes
  through the plugin's mod data checker as MO2's installer would; the result
  is compared with the mod MO2 installed from it (FOMOD archives apart);
- frameworks: the framework check on the profile's active mods, then with
  each framework's mod taken out in turn.

Nothing in the instance is changed.
"""

from __future__ import annotations

import configparser
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402

harness.setup_mobase()
sys.path.insert(0, str(harness.stage_plugins()))
from PyQt6.QtCore import QCoreApplication  # noqa: E402

app = QCoreApplication([])
import mobase  # noqa: E402
from basic_games.games.cyberpunk2077 import frameworks  # noqa: E402
from basic_games.games.cyberpunk2077.layout import CyberpunkModDataChecker  # noqa: E402

TAR = r"C:\Windows\System32\tar.exe"
DOCS = (".txt", ".md", ".png", ".jpg", ".jpeg", ".pdf", ".url", ".webp", ".gif")


def long_path(path: Path) -> str:
    text = str(path.resolve())
    if text.startswith("\\\\?\\"):
        return text
    return "\\\\?\\UNC\\" + text[2:] if text.startswith("\\\\") else "\\\\?\\" + text


def files_of(folder: Path) -> list[str]:
    """A folder's files, relative, also past Windows' 260 character limit."""
    import os

    root = long_path(folder)
    found = []
    for base, _dirs, names in os.walk(root):
        for name in names:
            if name != "meta.ini":
                found.append(os.path.relpath(os.path.join(base, name), root).replace("\\", "/"))
    return found


def archive_files(path: Path) -> list[str] | None:
    out = subprocess.run([TAR, "-tf", str(path)], capture_output=True, timeout=600)
    if out.returncode != 0:
        return None
    entries = [e.replace("\\", "/") for e in out.stdout.decode("utf-8", "replace").splitlines() if e]
    entries = [e[2:] if e.startswith("./") else e for e in entries]
    # RAR lists its folders without a trailing slash: the parent of a path is a folder
    parents = set()
    for entry in entries:
        parts = entry.rstrip("/").split("/")
        for i in range(1, len(parts)):
            parents.add("/".join(parts[:i]).casefold())
    return [e for e in entries if not e.endswith("/") and e.casefold() not in parents]


def tree_of(paths: list[str]):
    tree = mobase.private.makeTree()  # type: ignore[attr-defined]
    for path in paths:
        try:
            tree.addFile(path)
        except RuntimeError:
            pass
    return tree


def game_files(paths) -> set[str]:
    """The game files of a list, a file hidden in MO2 (.mohidden) by its name."""
    names = {p.casefold().removesuffix(".mohidden") for p in paths}
    return {p for p in names if "/" in p and not p.endswith(DOCS)}


def installer_check(instance: Path) -> None:
    installed: dict[str, str] = {}
    for meta in (instance / "mods").glob("*/meta.ini"):
        parser = configparser.ConfigParser(interpolation=None, strict=False)
        try:
            parser.read(meta, encoding="utf-8")
        except Exception:  # noqa: BLE001
            continue
        name = parser.get("General", "installationFile", fallback="")
        if name:
            installed[Path(name.replace("\\", "/")).name.casefold()] = meta.parent.name
    downloads = [f for f in (instance / "downloads").iterdir() if f.suffix.lower() in (".zip", ".7z", ".rar")]
    with ThreadPoolExecutor(8) as pool:
        listed = dict(zip(downloads, pool.map(archive_files, downloads)))
    checker = CyberpunkModDataChecker(lambda: False)
    stats: Counter[str] = Counter()
    notes = []
    for archive, files in sorted(listed.items()):
        if files is None:
            stats["unreadable"] += 1
            continue
        if any(f.casefold().endswith("fomod/moduleconfig.xml") for f in files):
            stats["fomod (its own installer)"] += 1
            continue
        tree = tree_of(files)
        status = checker.dataLooksValid(tree)
        if status is mobase.ModDataChecker.INVALID:
            stats["invalid (manual installer)"] += 1
            notes.append(f"invalid: {archive.name}: {files[:5]}")
            continue
        kind = "valid" if status is mobase.ModDataChecker.VALID else "fixed"
        if status is mobase.ModDataChecker.FIXABLE:
            tree = checker.fix(tree)
        mod = installed.get(archive.name.casefold())
        if mod is None or not (instance / "mods" / mod).is_dir():
            stats[f"{kind}, not installed"] += 1
            continue
        got = game_files(harness.tree_paths(tree))
        want = game_files(files_of(instance / "mods" / mod))
        if got == want:
            stats[f"{kind}, same as installed"] += 1
        else:
            stats[f"{kind}, differs"] += 1
            notes.append(f"differs: {archive.name} -> {mod}: plugin {sorted(got - want)[:4]}, installed {sorted(want - got)[:4]}")
    print("installer:", dict(stats))
    for note in notes:
        print("  " + note)


def framework_check(instance: Path) -> None:
    ini = (instance / "ModOrganizer.ini").read_text(encoding="utf-8", errors="replace")
    profile = "Default"
    for line in ini.splitlines():
        if line.startswith("selected_profile="):
            profile = line.split("=", 1)[1].removeprefix("@ByteArray(").removesuffix(")")
    lines = (instance / "profiles" / profile / "modlist.txt").read_text(encoding="utf-8").splitlines()
    active = [line[1:] for line in reversed(lines) if line.startswith("+")]
    mods = [
        frameworks.ModFiles(name, instance / "mods" / name, harness.make_tree(files_of(instance / "mods" / name)))
        for name in active
        if (instance / "mods" / name).is_dir()
    ]
    started = time.perf_counter()
    report = frameworks.analyze(mods, None, None)
    print(f"frameworks, profile {profile} ({len(mods)} mods): missing {report.missing or 'none'}"
          f" in {(time.perf_counter() - started) * 1000:.0f} ms")
    providers: dict[str, list[str]] = {}
    for mod in mods:
        for key in frameworks.provided_by_tree(mod.tree):
            providers.setdefault(key, []).append(mod.name)
    for key, names in sorted(providers.items()):
        left = [m for m in mods if m.name not in names]
        started = time.perf_counter()
        users = frameworks.analyze(left, None, None).missing.get(key, [])
        print(f"  without {', '.join(names)}: {len(users)} mods warned ({', '.join(users[:3])}...)"
              f" in {(time.perf_counter() - started) * 1000:.0f} ms")


if __name__ == "__main__":
    folder = Path(sys.argv[1])
    installer_check(folder)
    framework_check(folder)
