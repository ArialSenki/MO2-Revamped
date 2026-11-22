"""MO2 Revamped: the save route of an MO2 profile (profile, instance or
Steam's) and the VFS mapping that gives it to the game.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import configparser
import json
import os
from pathlib import Path
import stat


import mobase
from PyQt6.QtCore import (
    QDir,
)

from .common import (
    qInfo,
)


class EldenRingLocalSavegames(mobase.LocalSavegames):
    """Route Elden Ring saves according to the active profile's save mode."""

    # the instance's folder for saves shared by its profiles
    SharedSavesFolderName = "Elden Ring Shared Saves"

    ConfigFilename = "eldenring_mo2_saves.ini"
    ProfileIsolated = "profile"
    InstanceShared = "instance_shared"
    GlobalShared = "global"
    SaveProfilesDirectoryName = "save_profiles"
    ProfileSaveProfileKey = "profile_save_profile"
    InstanceSaveProfileKey = "instance_save_profile"
    Modes = {ProfileIsolated, InstanceShared, GlobalShared}

    # the organizer of this MO2 instance, for its base folder
    _path_organizer: mobase.IOrganizer | None = None

    def __init__(self, game, organizer: mobase.IOrganizer):
        super().__init__()
        self._game = game
        self._organizer = organizer
        EldenRingLocalSavegames._path_organizer = organizer
        self._prepared_profile_routes: dict[str, tuple[str, bool, str]] = {}

    @staticmethod
    def _profile_key(profile: mobase.IProfile) -> str:
        path = Path(profile.absolutePath())
        try:
            return os.path.normcase(str(path.resolve()))
        except (OSError, RuntimeError):
            return os.path.normcase(os.path.abspath(path))

    @classmethod
    def mode_for_profile_directory(cls, profile_directory: Path) -> str:
        config_path = profile_directory / cls.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(config_path, encoding="utf-8-sig")
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated).strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            mode = cls.ProfileIsolated
        return mode if mode in cls.Modes else cls.ProfileIsolated

    @classmethod
    def mode_for_profile(cls, profile) -> str:
        directory = Path(profile.absolutePath())
        mode = cls.mode_for_profile_directory(directory)
        if mode == cls.ProfileIsolated and not cls._mode_is_stored(directory):
            # no valid mode in the ini: MO2 then follows the profile's own
            # LocalSaves setting, so this does too (Global when it is off)
            try:
                if not profile.localSavesEnabled():
                    return cls.GlobalShared
            except (AttributeError, RuntimeError):
                pass
        return mode

    @classmethod
    def _mode_is_stored(cls, profile_directory: Path) -> bool:
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8-sig")
            mode = config.get("Saves", "mode", fallback="").strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return False
        return mode in cls.Modes

    @classmethod
    def save_profile_for_profile_directory(
        cls, profile_directory: Path, mode: str
    ) -> str:
        if mode == cls.ProfileIsolated:
            key = cls.ProfileSaveProfileKey
        elif mode == cls.InstanceShared:
            key = cls.InstanceSaveProfileKey
        else:
            return ""

        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(profile_directory / cls.ConfigFilename, encoding="utf-8-sig")
            profile_id = config.get("Saves", key, fallback="").strip().lower()
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return ""
        if len(profile_id) != 32 or any(
            character not in "0123456789abcdef" for character in profile_id
        ):
            return ""
        return profile_id

    @classmethod
    def save_profile_is_available(
        cls, profile_directory: Path, mode: str
    ) -> bool:
        profile_id = cls.save_profile_for_profile_directory(profile_directory, mode)
        if not profile_id:
            return True
        if mode == cls.ProfileIsolated:
            root = profile_directory / cls.SaveProfilesDirectoryName
        elif mode == cls.InstanceShared:
            root = cls.instance_shared_directory(profile_directory) / cls.SaveProfilesDirectoryName
        else:
            return False
        save_directory = root / profile_id
        metadata_path = root / f"{profile_id}.json"
        try:
            save_stat = os.lstat(save_directory)
            metadata_stat = os.lstat(metadata_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if (
                not stat.S_ISDIR(save_stat.st_mode)
                or not stat.S_ISREG(metadata_stat.st_mode)
                or stat.S_ISLNK(save_stat.st_mode)
                or stat.S_ISLNK(metadata_stat.st_mode)
                or getattr(save_stat, "st_file_attributes", 0) & reparse_flag
                or getattr(metadata_stat, "st_file_attributes", 0) & reparse_flag
            ):
                return False
            metadata = json.loads(metadata_path.read_text(encoding="utf-8-sig"))
        except (OSError, UnicodeError, json.JSONDecodeError, ValueError):
            return False
        return (
            isinstance(metadata, dict)
            and metadata.get("id") == profile_id
            and isinstance(metadata.get("name"), str)
            and bool(metadata.get("name", "").strip())
        )
    @classmethod
    def active_save_directory(
        cls, profile_directory: Path, mode: str
    ) -> Path | None:
        if mode == cls.GlobalShared:
            return None
        profile_id = cls.save_profile_for_profile_directory(profile_directory, mode)
        if mode == cls.InstanceShared:
            shared_root = cls.instance_shared_directory(profile_directory)
            if profile_id:
                return shared_root / cls.SaveProfilesDirectoryName / profile_id
            return shared_root
        if profile_id:
            return profile_directory / cls.SaveProfilesDirectoryName / profile_id
        return profile_directory / "saves"

    @classmethod
    def instance_shared_directory(cls, profile_path: Path) -> Path:
        # The folder belongs to the MO2 instance. Earlier builds put it beside
        # the profiles folder, which is the same place unless the profiles
        # path was customised (two instances could then share it); such a
        # folder that already holds saves is kept so nothing goes missing.
        legacy = profile_path.parent.parent / cls.SharedSavesFolderName
        try:
            base = Path(cls._path_organizer.basePath()) if cls._path_organizer else None
        except (AttributeError, RuntimeError, TypeError):
            base = None
        if base is None or not str(base):
            return legacy
        anchored = base / cls.SharedSavesFolderName
        if os.path.normcase(os.path.abspath(anchored)) == os.path.normcase(os.path.abspath(legacy)):
            return anchored
        try:
            anchored_used = anchored.is_dir() and any(anchored.iterdir())
            if not anchored_used and legacy.is_dir() and any(legacy.iterdir()):
                return legacy
        except OSError:
            pass
        return anchored

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        mode = self.mode_for_profile(profile)
        local_saves_enabled = bool(profile.localSavesEnabled())
        save_profile_id = self.save_profile_for_profile_directory(
            Path(profile.absolutePath()), mode
        )
        self._prepared_profile_routes[self._profile_key(profile)] = (
            mode,
            local_saves_enabled,
            save_profile_id,
        )
        return mode != self.GlobalShared and local_saves_enabled

    def profile_route_is_current(self, profile: mobase.IProfile) -> bool:
        mode = self.mode_for_profile(profile)
        current_route = (
            mode,
            bool(profile.localSavesEnabled()),
            self.save_profile_for_profile_directory(
                Path(profile.absolutePath()), mode
            ),
        )
        profile_key = self._profile_key(profile)
        prepared_route = self._prepared_profile_routes.get(profile_key)
        if prepared_route == current_route:
            return True

        # Profile-isolated and instance-shared saves both use MO2's local-save
        # mapping. Those routes, including named save profiles, can be refreshed
        # for the next launch without restarting MO2. Crossing the Steam
        # boundary still requires reloading MO2's profile-specific-save setting.
        local_modes = {self.ProfileIsolated, self.InstanceShared}
        if (
            prepared_route is not None
            and prepared_route[0] in local_modes
            and mode in local_modes
            and prepared_route[1]
            and current_route[1]
        ):
            self._prepared_profile_routes[profile_key] = current_route
            return True

        return False

    def mappings(self, profile_save_dir: QDir):
        # MO2 passes the save directory for the profile whose VFS mapping it is
        # currently building. During a profile or instance switch,
        # organizer.profile() can still refer to the previous profile or be
        # temporarily unavailable. Use the supplied path as the source of
        # truth so the mapping cannot silently fall back to the Steam save.
        profile_save_path = Path(profile_save_dir.absolutePath())
        profile_directory = profile_save_path.parent
        mode = self.mode_for_profile_directory(profile_directory)
        if mode == self.GlobalShared:
            qInfo(
                "Elden Ring MO2: save mapping uses the global Steam folder; "
                f"profile='{profile_directory}', game saves='{self._game.savesDirectory().absolutePath()}'."
            )
            return []

        source = self.active_save_directory(profile_directory, mode)
        if source is None:
            return []

        destination = self._game.savesDirectory().absolutePath()
        qInfo(
            "Elden Ring MO2: save mapping prepared; "
            f"mode='{mode}', profile='{profile_directory}', "
            f"source='{source}', game saves='{destination}'."
        )

        return [
            mobase.Mapping(
                source=str(source),
                destination=destination,
                is_directory=True,
                create_target=True,
            )
        ]


class NightreignLocalSavegames(EldenRingLocalSavegames):
    """Nightreign routes its saves the same way, in its own shared folder."""

    SharedSavesFolderName = "Nightreign Shared Saves"
