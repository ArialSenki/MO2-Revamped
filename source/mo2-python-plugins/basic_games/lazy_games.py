"""MO2 Revamped: game plugins that import their module only when needed.

Importing every games/game_*.py at each start cost ~190 ms, for one game the
instance uses. MO2 sets the QApplication property "revampedInstanceGame" to
the instance's game before it loads the plugins. With a games/manifest.json
(written when a release is built, by make_basic_games_manifest.py) the
module of that game is imported as before; every other game becomes a
ManifestGame: a BasicGame with the class attributes and the load-time answers
(name, version, settings, ...) its real class gave when the manifest was
written. That is all MO2 asks a game it doesn't manage for: the instance
window's game list, nxm links, the plugin list. Any other method the real
class overrides imports the module the first time it is called and hands the
call to a real object.

A module whose file doesn't match the manifest's hash, or that the manifest
doesn't list, is imported as before; so is everything when MO2 doesn't say
which game the instance has.
"""

from __future__ import annotations

import hashlib
import importlib
import json
import os
from typing import Any

import mobase

from .basic_game import BasicGame

MANIFEST_FORMAT = 1

# answered from the manifest: MO2 asks every game plugin for these when it
# loads them, and they don't depend on the game's folder
STATIC_GETTERS = (
    "name",
    "author",
    "description",
    "gameName",
    "gameShortName",
    "validShortNames",
    "gameNexusName",
    "nexusGameID",
    "nexusModOrganizerID",
)

# the BasicGame methods a stub always keeps for itself
OWN_METHODS = set(STATIC_GETTERS) | {"version", "settings", "init", "setGamePath", "setGameVariant", "gameDirectory"}


def instance_game() -> str | None:
    """The instance's game name MO2 passed, or None (unknown: load everything)."""
    try:
        from PyQt6.QtCore import QCoreApplication

        app = QCoreApplication.instance()
        value = app.property("revampedInstanceGame") if app is not None else None
    except Exception:
        return None
    return value if isinstance(value, str) and value else None


def file_hash(path: str) -> str:
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def read_manifest(games_dir: str) -> dict[str, Any] | None:
    path = os.path.join(games_dir, "manifest.json")
    try:
        with open(path, encoding="utf-8") as f:
            manifest = json.load(f)
    except (OSError, ValueError):
        return None
    if not isinstance(manifest, dict) or manifest.get("format") != MANIFEST_FORMAT:
        return None
    # anything unexpected and the loader imports every game, as without one
    modules, tools = manifest.get("modules"), manifest.get("tools", {})
    if not isinstance(modules, dict) or not isinstance(tools, dict):
        return None
    for entry in modules.values():
        if not isinstance(entry, dict) or not isinstance(entry.get("sha256"), str):
            return None
        classes = entry.get("classes")
        if not isinstance(classes, list) or not all(
            isinstance(c, dict)
            and isinstance(c.get("class"), str)
            and isinstance(c.get("answers"), dict)
            and isinstance(c["answers"].get("gameName"), str)
            for c in classes
        ):
            return None
    for games in tools.values():
        if not isinstance(games, list) or not all(isinstance(g, str) for g in games):
            return None
    return manifest


def _setting(entry: list[Any]) -> mobase.PluginSetting:
    key, description, default = entry
    return mobase.PluginSetting(key, description, default)


class ManifestGame(BasicGame):
    """Base of the generated stubs; see make_stub_class."""

    _manifest_module: str = ""
    _manifest_class: str = ""
    _manifest_entry: dict[str, Any] = {}

    def __init__(self):
        self._fromName = self._manifest_class
        self._real_game: BasicGame | None = None
        self._stub_organizer: mobase.IOrganizer | None = None
        self._stub_variant: str | None = None
        super().__init__()

    # what the real class answered when the manifest was written
    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(self._manifest_entry["version"])

    def settings(self) -> list[mobase.PluginSetting]:
        return [_setting(s) for s in self._manifest_entry.get("settings", [])]

    def init(self, organizer: mobase.IOrganizer) -> bool:
        # the real init() registers what the game needs once it is managed;
        # this one isn't
        self._stub_organizer = organizer
        return BasicGame.init(self, organizer)

    def setGamePath(self, path) -> None:
        # MO2 sets every game's path when it detects them, at each start: the
        # real class's own setGamePath (Elden Ring's fixes the letter case)
        # runs once the path is asked for, in gameDirectory()
        BasicGame.setGamePath(self, path)
        if self._real_game is not None:
            self._real_game.setGamePath(path)

    def gameDirectory(self):
        if "setGamePath" in self._manifest_entry.get("overrides", []) and self._gamePath:
            return self._real().gameDirectory()
        return BasicGame.gameDirectory(self)

    def setGameVariant(self, variant: str) -> None:
        self._stub_variant = variant
        BasicGame.setGameVariant(self, variant)
        if self._real_game is not None:
            self._real_game.setGameVariant(variant)

    def _real(self) -> BasicGame:
        if self._real_game is None:
            module = importlib.import_module(self._manifest_module, __package__)
            real = getattr(module, self._manifest_class)()
            if self._stub_organizer is not None:
                real.init(self._stub_organizer)
            if self._gamePath:
                real.setGamePath(self._gamePath)
            if self._stub_variant is not None:
                real.setGameVariant(self._stub_variant)
            self._real_game = real
        return self._real_game


def _forward(name: str):
    def method(self, *args, **kwargs):
        return getattr(self._real(), name)(*args, **kwargs)

    method.__name__ = name
    return method


def _answer(value: Any):
    def method(self):
        return value

    return method


def make_stub_class(module_name: str, entry: dict[str, Any]) -> type:
    attributes: dict[str, Any] = dict(entry.get("attributes", {}))
    attributes.update(
        _manifest_module=module_name,
        _manifest_class=entry["class"],
        _manifest_entry=entry,
    )
    for getter in STATIC_GETTERS:
        if getter in entry.get("answers", {}):
            attributes[getter] = _answer(entry["answers"][getter])
    for name in entry.get("overrides", []):
        if name not in OWN_METHODS:
            attributes[name] = _forward(name)
    return type(entry["class"], (ManifestGame,), attributes)
