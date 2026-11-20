"""A small stand-in for MO2's IOrganizer over real folders: a game folder, mods
and Overwrite, merged by priority like the VFS."""

from __future__ import annotations

import fnmatch
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

    def isSeparator(self) -> bool:
        return self._name.endswith("_separator")

    def isForeign(self) -> bool:
        return False

    def isOverwrite(self) -> bool:
        return False

    def fileTree(self):
        return make_tree(_relative_files(self._path))


def _relative_files(root: Path) -> list[str]:
    return [file.relative_to(root).as_posix() for file in root.rglob("*") if file.is_file()]


class FakeModList:
    def __init__(self, organizer: "FakeOrganizer"):
        self._organizer = organizer

    def allModsByProfilePriority(self) -> list[str]:
        return list(self._organizer.mod_order)

    def state(self, name: str):
        if name in self._organizer.active:
            return mobase.ModState.ACTIVE | mobase.ModState.VALID
        return mobase.ModState.VALID

    def getMod(self, name: str):
        return FakeMod(name, self._organizer.mods / name)

    def onModStateChanged(self, callback) -> bool:
        return True

    onModInstalled = onModRemoved = onModStateChanged


class FakeFeatures:
    def __init__(self):
        self.features = []

    def registerFeature(self, game, feature, priority, replace) -> bool:
        self.features.append(feature)
        return True


class FakeOrganizer:
    def __init__(self, base: Path):
        self.base = base
        self.mods = base / "mods"
        self.overwrite = base / "overwrite"
        self.mods.mkdir(parents=True, exist_ok=True)
        self.overwrite.mkdir(parents=True, exist_ok=True)
        self.game_dir: Path | None = None
        self.mod_order: list[str] = []
        self.active: set[str] = set()
        self.settings: dict[tuple[str, str], object] = {}
        self.persist: dict[tuple[str, str], object] = {}
        self.enabled_plugins: set[str] = set()
        self.started: list[tuple[str, list[str], str]] = []
        self.exit_code = 0
        self.game = None
        self.about_to_run = []
        self._features = FakeFeatures()

    # test helpers

    def add_mod(self, name: str, files: list[str], active: bool = True) -> Path:
        root = self.mods / name
        for relative in files:
            file = root / relative
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(relative.encode())
        self.mod_order.append(name)
        if active:
            self.active.add(name)
        return root

    def origins(self) -> dict[str, Path]:
        """Virtual path (casefolded) -> the real file that wins."""
        winners: dict[str, Path] = {}
        roots = ([self.game_dir] if self.game_dir else []) + [
            self.mods / name for name in self.mod_order if name in self.active
        ] + [self.overwrite]
        for root in roots:
            if root.is_dir():
                for file in root.rglob("*"):
                    if file.is_file():
                        winners[file.relative_to(root).as_posix().casefold()] = file
        return winners

    # IOrganizer

    def gameFeatures(self):
        return self._features

    def managedGame(self):
        return self.game

    def pluginSetting(self, plugin: str, key: str):
        if (plugin, key) in self.settings:
            return self.settings[(plugin, key)]
        for setting in self.game.settings():
            if setting.key == key:
                return setting.default_value
        return None

    def setPluginSetting(self, plugin: str, key: str, value) -> None:
        self.settings[(plugin, key)] = value

    def persistent(self, plugin: str, key: str, default=None):
        return self.persist.get((plugin, key), default)

    def setPersistent(self, plugin: str, key: str, value, sync: bool = True) -> None:
        self.persist[(plugin, key)] = value

    def isPluginEnabled(self, name) -> bool:
        return name in self.enabled_plugins

    def modList(self):
        return FakeModList(self)

    def modsPath(self) -> str:
        return str(self.mods)

    def overwritePath(self) -> str:
        return str(self.overwrite)

    def findFiles(self, path: str, pattern: str) -> list[str]:
        folder = path.replace("\\", "/").strip("/").casefold()
        found = []
        for virtual, real in self.origins().items():
            parent, _, name = virtual.rpartition("/")
            if parent == folder and fnmatch.fnmatch(name, pattern.casefold()):
                found.append(str(real))
        return found

    def getFileOrigins(self, path: str) -> list[str]:
        """The mods a file comes from, winner first ("data" for the game)."""
        key = path.replace("\\", "/").strip("/").casefold()
        origins = []
        for name in reversed([n for n in self.mod_order if n in self.active]):
            if (self.mods / name / key).is_file() or any(
                f.relative_to(self.mods / name).as_posix().casefold() == key
                for f in (self.mods / name).rglob("*") if f.is_file()
            ):
                origins.append(name)
        if self.game_dir and (self.game_dir / key).is_file():
            origins.append("data")
        return origins

    def virtualFileTree(self):
        return make_tree(sorted(self.origins()))

    def startApplication(self, binary, args, cwd="", *rest):
        self.started.append((str(binary), list(args), str(cwd)))
        for callback in self.about_to_run:
            if not callback(str(binary), cwd, " ".join(args)):
                return 0
        if Path(str(binary)).name.casefold() == "redmod.exe" and self.exit_code == 0:
            deployed = self.overwrite / "r6/cache/modded/final.redscripts"
            deployed.parent.mkdir(parents=True, exist_ok=True)
            deployed.write_text("deployed")
        return 1234

    def waitForApplication(self, handle, refresh=True):
        return (self.exit_code == 0, self.exit_code)

    def onUserInterfaceInitialized(self, callback) -> bool:
        return True

    def onPluginEnabled(self, *args) -> bool:
        return True

    def onAboutToRun(self, callback) -> bool:
        self.about_to_run.append(callback)
        return True

    def onProfileChanged(self, callback) -> bool:
        return True

    def onFinishedRun(self, callback) -> bool:
        return True
