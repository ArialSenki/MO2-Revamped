"""Valheim checks behind the problem report: plugins without BepInEx, plugins
whose required plugins ([BepInDependency], read from their metadata) aren't
there, InSlimVML mods without their loader."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from ..revamped_shared.dotnet import AssemblyIndex, plugin_dlls

# well-known plugins a mod may require: their name and Nexus page
KNOWN = {
    "com.jotunn.jotunn": ("Jotunn", "https://www.nexusmods.com/valheim/mods/1138"),
}


@dataclass
class ModFolder:
    name: str
    path: Path


@dataclass
class Report:
    without_bepinex: list[str] = field(default_factory=list)
    # required plugin GUID (casefolded) -> the mods whose plugins require it
    missing: dict[str, list[str]] = field(default_factory=dict)
    without_inslimvml: list[str] = field(default_factory=list)


def _has(mods: list[ModFolder], game_dir: Path | None, relative: str) -> bool:
    if game_dir is not None and (game_dir / relative).is_file():
        return True
    return any((mod.path / relative).is_file() for mod in mods)


def _vml_mods(folder: Path) -> bool:
    mods = folder / "InSlimVML" / "Mods"
    try:
        return mods.is_dir() and any(p.name.casefold().endswith("_vml.dll") for p in mods.iterdir())
    except OSError:
        return False


def _game_plugin_dlls(game_dir: Path | None) -> list[Path]:
    if game_dir is None:
        return []
    plugins = game_dir / "BepInEx" / "plugins"
    found = []
    if plugins.is_dir():
        for folder, _dirs, files in os.walk(plugins):
            found.extend(Path(folder) / name for name in files if name.casefold().endswith(".dll"))
    return found


def analyze(mods: list[ModFolder], game_dir: Path | None, index: AssemblyIndex) -> Report:
    """mods: the active ones."""
    report = Report()
    dlls = {mod.name: plugin_dlls(mod.path) for mod in mods}
    with_plugins = [mod.name for mod in mods if dlls[mod.name] or (mod.path / "BepInEx" / "patchers").is_dir()]
    bepinex = _has(mods, game_dir, "BepInEx/core/BepInEx.dll") and _has(mods, game_dir, "winhttp.dll")
    if with_plugins and not bepinex:
        report.without_bepinex = with_plugins

    provided: set[str] = set()
    for dll in _game_plugin_dlls(game_dir) + [d for files in dlls.values() for d in files]:
        provided |= {guid.casefold() for guid in index.info(dll).plugin_guids}
    for mod in mods:
        for dll in dlls[mod.name]:
            for guid in index.info(dll).required:
                key = guid.casefold()
                if key not in provided:
                    users = report.missing.setdefault(key, [])
                    if mod.name not in users:
                        users.append(mod.name)

    if not _has(mods, game_dir, "inslimvml.ini"):
        report.without_inslimvml = [mod.name for mod in mods if _vml_mods(mod.path)]
    return report
