"""A small stand-in for MO2's IOrganizer over real folders (Elden Ring tests):
an instance folder with mods, Overwrite and a profile, the game folder and a
Steam save folder under a temporary APPDATA."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path

import mobase

from harness import make_tree


class FakeMod:
    def __init__(self, name: str, path: Path):
        self._name, self._path = name, path

    def name(self) -> str:
        return self._name

    def absolutePath(self) -> str:
        return str(self._path)

    def isOverwrite(self) -> bool:
        return False

    def isSeparator(self) -> bool:
        return False

    def pluginSettings(self, _plugin: str) -> dict:
        return {}

    def fileTree(self):
        return make_tree(
            [f.relative_to(self._path).as_posix() for f in self._path.rglob("*") if f.is_file()]
        )


class FakeModList:
    def __init__(self, organizer: "FakeOrganizer"):
        self._organizer = organizer

    def allModsByProfilePriority(self) -> list[str]:
        return list(self._organizer.mod_order)

    def allMods(self) -> list[str]:
        return list(self._organizer.mod_order)

    def state(self, name: str):
        if name in self._organizer.active:
            return mobase.ModState.ACTIVE | mobase.ModState.VALID
        return mobase.ModState.VALID

    def getMod(self, name: str):
        if name not in self._organizer.mod_order:
            return None
        return FakeMod(name, self._organizer.mods / name)

    def setActive(self, name: str, active: bool) -> None:
        (self._organizer.active.add if active else self._organizer.active.discard)(name)

    def onModStateChanged(self, callback) -> bool:
        return True

    onModInstalled = onModRemoved = onModMoved = onModStateChanged


class FakeProfile:
    def __init__(self, path: Path, local_saves: bool = True):
        self._path, self._local = path, local_saves

    def absolutePath(self) -> str:
        return str(self._path)

    def name(self) -> str:
        return self._path.name

    def localSavesEnabled(self) -> bool:
        return self._local


class FakeFeatures:
    def __init__(self):
        self.features = []

    def registerFeature(self, game, feature, priority, replace) -> bool:
        self.features.append(feature)
        return True


class FakeOrganizer:
    """mods: <base>/mods/<name>; the game: <base>/game/Game."""

    def __init__(self, base: Path | None = None, mods_folder: Path | None = None):
        self.base = Path(base or tempfile.mkdtemp(prefix="er-instance-"))
        self.mods = Path(mods_folder or self.base / "mods")
        self.mods.mkdir(parents=True, exist_ok=True)
        (self.base / "overwrite").mkdir(exist_ok=True)
        self.profile_dir = self.base / "profiles" / "Default"
        self.profile_dir.mkdir(parents=True, exist_ok=True)
        self.game_dir = self.base / "game"
        (self.game_dir / "Game").mkdir(parents=True, exist_ok=True)
        self.mod_order: list[str] = []
        self.active: set[str] = set()
        self.settings: dict[str, object] = {}
        self.persist: dict[str, object] = {}
        self.features = FakeFeatures()
        self.managed = None
        self.callbacks: dict[str, list] = {}
        self._profile = FakeProfile(self.profile_dir)

    # --- building the instance ---
    def add_mod(self, name: str, files: list[str], active: bool = True) -> Path:
        root = self.mods / name
        for relative in files:
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"MZ" if relative.lower().endswith(".dll") else b"x")
        root.mkdir(parents=True, exist_ok=True)
        self.mod_order.append(name)
        if active:
            self.active.add(name)
        return root

    def write_profile_ini(self, text: str) -> Path:
        path = self.profile_dir / "eldenring_mo2_startup.ini"
        path.write_text(text, encoding="utf-8")
        return path

    # --- IOrganizer ---
    def gameFeatures(self):
        return self.features

    def managedGame(self):
        return self.managed

    def profile(self):
        return self._profile

    def modList(self):
        return FakeModList(self)

    def pluginSetting(self, plugin: str, key: str):
        return self.settings.get(key, {"load_native_dlls": True, "save_backups_kept": 5}.get(key))

    def setPluginSetting(self, plugin: str, key: str, value) -> None:
        self.settings[key] = value

    def persistent(self, plugin: str, key: str, default=None):
        return self.persist.get(key, default)

    def setPersistent(self, plugin: str, key: str, value, sync: bool = True) -> None:
        self.persist[key] = value

    def basePath(self) -> str:
        return str(self.base)

    def modsPath(self) -> str:
        return str(self.mods)

    def overwritePath(self) -> str:
        return str(self.base / "overwrite")

    def getFileOrigins(self, virtual: str) -> list[str]:
        """The winning mod first, then the others by priority (as MO2)."""
        parts = Path(virtual).parts
        return [
            name for name in reversed(self.mod_order)
            if name in self.active and (self.mods / name).joinpath(*parts).is_file()
        ]

    def findFiles(self, directory: str, pattern: str) -> list[str]:
        found = set()
        for name in self.mod_order:
            if name not in self.active:
                continue
            folder = self.mods / name / directory
            if folder.is_dir():
                found.update(
                    str(Path(directory) / f.relative_to(folder)) for f in folder.rglob("*") if f.is_file()
                )
        return sorted(found)

    def _register(self, kind: str, callback) -> bool:
        self.callbacks.setdefault(kind, []).append(callback)
        return True

    def onUserInterfaceInitialized(self, callback) -> bool:
        return self._register("ui", callback)

    def onAboutToRun(self, callback) -> bool:
        return self._register("run", callback)

    def onFinishedRun(self, callback) -> bool:
        return self._register("finished", callback)

    def onProfileChanged(self, callback) -> bool:
        return self._register("profile", callback)


def game_for(organizer: FakeOrganizer, nightreign: bool = False):
    """The plugin with init() run against the fake organizer, managing the
    fake game folder."""
    from basic_games.games import game_eldenring_mo2 as module

    cls = module.EldenRingNightreignMo2Game if nightreign else module.EldenRingMo2Game
    game = cls()
    organizer.managed = game
    assert game.init(organizer)
    game.setGamePath(str(organizer.game_dir))
    return game


def appdata_folder() -> Path:
    """A temporary APPDATA: the plugin's Steam save folder lives there."""
    folder = Path(tempfile.mkdtemp(prefix="er-appdata-"))
    os.environ["APPDATA"] = str(folder)
    return folder
