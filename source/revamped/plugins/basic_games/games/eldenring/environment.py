"""MO2 Revamped: the environment a game run gets from MO2: the bridge's
settings, the native initializers, Reforged's attach config, sound folders
and BootBoost.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import configparser
import json
import os
from pathlib import Path

import mobase

from .common import (
    qInfo,
    qWarning,
    GAMES_DIR,
)
from .checker import EldenRingModDataChecker


def _bridge_readable_copy(path: str) -> str | None:
    """A copy of the profile's settings at a path the bridge can read (it
    reads at most 259 characters, and so do Windows' profile functions), in
    the temporary folder; None when that fails too."""
    try:
        import hashlib
        import shutil
        import tempfile

        digest = hashlib.sha1(path.casefold().encode("utf-8")).hexdigest()[:12]
        copy = os.path.join(tempfile.gettempdir(), f"EldenRingMO2Profile-{digest}.ini")
        if len(copy) >= 260:
            return None
        shutil.copyfile("\\\\?\\" + path, copy)
        return copy
    except OSError:
        return None


class EnvironmentMixin:
    def _activate_profile_config_environment(self, path: Path, initializers: list[dict] | None = None) -> None:
        config_key = self.ProfileLaunchConfigEnvironment
        self._profile_config_env_previous = os.environ.get(config_key)
        self._profile_config_env_active = True
        if path.is_file():
            value = str(path)
            if len(value) >= 260:
                # the bridge reads this path into a MAX_PATH buffer, and Windows'
                # profile functions don't take a longer one: it gets a copy
                value = _bridge_readable_copy(value) or value
            os.environ[config_key] = value
            if len(value) >= 260:
                qWarning(
                    f"Elden Ring MO2: the profile's settings path is {len(value)} "
                    "characters long; the bridge reads at most 259, so it ignores this "
                    "profile's startup options (minimized start, black background, "
                    "CPU 0, priority) and skips the intro logos whatever the "
                    "profile says. A shorter instance or profile path fixes it."
                )
        else:
            os.environ.pop(config_key, None)

        initializer_key = self.NativeInitializerEnvironment
        self._native_initializer_env_previous = os.environ.get(initializer_key)
        self._native_initializer_env_active = True
        initializer_values = [
            # the path as its forced load names it: the bridge compares the
            # two exactly, so a linked mods folder must not be resolved here
            f"{os.path.abspath(entry['library'])}|{entry['initializer']}"
            for entry in (initializers or [])
            if entry.get("initializer")
        ]
        if initializer_values and self._set_bridge_environment(
            initializer_key, "\n".join(initializer_values)
        ):
            qInfo(
                "Elden Ring MO2: Revamped native initializer(s) passed to bridge: "
                + ", ".join(
                    entry["initializer"]
                    for entry in (initializers or [])
                    if entry.get("initializer")
                )
                + "."
            )
        else:
            os.environ.pop(initializer_key, None)

        self._reforged_env_previous = {
            key: os.environ.get(key)
            for key in (
                self.ReforgedAttachEnvironment,
                self.SaveExtensionEnvironment,
                self.SaveExtensionAcceptedEnvironment,
                self.SoundFoldersEnvironment,
                self.BootBoostEnvironment,
            )
        }
        # the compatibility DLL also runs with Reforged: it skips BootBoost
        # when the profile turned it off
        os.environ[self.BootBoostEnvironment] = "1" if self._read_boot_boost(path) else "0"
        self._reforged_env_active = True
        reforged_natives = self._reforged_natives()
        own_save = self._mod_save_extension()
        save_extension = own_save[0] if own_save is not None else None
        if reforged_natives is not None and self._set_bridge_environment(
            self.ReforgedAttachEnvironment,
            self._reforged_attach_config(
                reforged_natives,
                save_extension if save_extension == self.ReforgedSaveExtension else None,
            ),
        ):
            qInfo(
                "Elden Ring MO2: ELDEN RING Reforged is active; its natives get "
                "the me3 attach config of this MO2 run."
            )
        else:
            os.environ.pop(self.ReforgedAttachEnvironment, None)
        if save_extension is not None and self._save_extension_accepted:
            os.environ[self.SaveExtensionAcceptedEnvironment] = "1"
        else:
            os.environ.pop(self.SaveExtensionAcceptedEnvironment, None)
        if save_extension is not None:
            os.environ[self.SaveExtensionEnvironment] = save_extension
            qInfo(
                f"Elden Ring MO2: the bridge names the saves ER0000.{save_extension}, "
                f"as {own_save[1]} does with its own launcher."
            )
        else:
            os.environ.pop(self.SaveExtensionEnvironment, None)
        sound_folders = self._active_sound_folders()
        if sound_folders:
            os.environ[self.SoundFoldersEnvironment] = ";".join(sound_folders)
        else:
            os.environ.pop(self.SoundFoldersEnvironment, None)
        qInfo("Elden Ring MO2: profile launch options passed to bridge.")

    @staticmethod
    def _set_bridge_environment(key: str, value: str) -> bool:
        """Sets a variable for the game, unless Windows' limit of 32767
        characters refuses it; then it is left out with a log line."""
        if len(key) + len(value) >= 32000:
            os.environ.pop(key, None)
            qWarning(
                f"Elden Ring MO2: {key} is too long for Windows "
                f"({len(value)} characters); the game starts without it."
            )
            return False
        os.environ[key] = value
        return True

    def _active_sound_folders(self) -> list[str]:
        mod_list = self._organizer.modList()
        folders = []
        for name in reversed(list(mod_list.allModsByProfilePriority())):
            if not mod_list.state(name) & mobase.ModState.ACTIVE:
                continue
            mod = mod_list.getMod(name)
            if mod is None:
                continue
            folder = Path(mod.absolutePath())
            if not (folder / "sd").is_dir():
                continue
            # the bridge reads the folders as one ";"-separated variable
            if ";" in str(folder):
                qWarning(
                    f"Elden Ring MO2: the sound files of '{name}' are not used: "
                    "its folder path has a ';'."
                )
                continue
            folders.append(str(folder))
        # Overwrite wins over every mod in MO2, so its sound files come first
        # (the bridge takes the first folder that has a file)
        try:
            overwrite = Path(self._organizer.overwritePath())
        except (AttributeError, RuntimeError):
            overwrite = None
        if overwrite is not None and (overwrite / "sd").is_dir() and ";" not in str(overwrite):
            folders.insert(0, str(overwrite))
        if len(";".join(folders)) >= 32000:
            qWarning(
                "Elden Ring MO2: too many mods with sound files for the bridge to "
                "read; mods' sound files are not used this run."
            )
            return []
        return folders

    def _me3_compat_load(
        self, forced_loads: list[mobase.ExecutableForcedLoadSetting]
    ) -> mobase.ExecutableForcedLoadSetting | None:
        """The me3 compatibility DLL's forced load: on every Elden Ring start
        for BootBoost, and what Reforged needs when it is among the loads."""
        # Nightreign too, for BootBoost (Reforged is an Elden Ring mod)
        reforged = any(
            Path(load.library()).name.casefold() == EldenRingModDataChecker._reforged_dll
            for load in forced_loads
        )
        profile = self._organizer.profile()
        boot_boost = (
            self._read_boot_boost(Path(profile.absolutePath()) / self.ProfileLaunchConfig)
            if profile is not None
            else True
        )
        if not reforged and not boot_boost:
            # nothing else for it to do
            return None
        library = (GAMES_DIR / self.Me3Compat)
        if not library.is_file():
            qWarning(
                f"Elden Ring MO2: {self.Me3Compat} is missing; "
                + (
                    "ELDEN RING Reforged runs with Arxan active and cannot save."
                    if reforged
                    else "the game starts without BootBoost."
                )
            )
            return None
        return mobase.ExecutableForcedLoadSetting(
            self.GameProcess, str(library)
        ).withEnabled(True).withForced(True)

    def _mod_save_extension(self) -> tuple[str, str] | None:
        """The save extension an active mod's own launcher gives it, with the
        mod's name: without it the mod plays ER0000.sl2, the save of the game
        without mods or of other mods. Seamless Co-op names the saves itself
        (its save_file_extension), so then there is none."""
        if self.GameShortName != "eldenring" or self._active_seamless_dll() is not None:
            return None
        if self._reforged_natives() is not None:
            return self.ReforgedSaveExtension, "ELDEN RING Reforged"
        if self._active_convergence():
            return self.ConvergenceSaveExtension, "The Convergence"
        return None

    def _active_convergence(self) -> bool:
        """Whether an active mod is The Convergence: installed from Nexus
        (its meta.ini names its Nexus page), or by hand with its me3 profile."""
        for _name, root, active in self._mod_folders():
            if not active:
                continue
            if (root / "convergence.me3").is_file() or (root / "me3" / "convergence.me3").is_file():
                return True
            meta = configparser.ConfigParser(interpolation=None, strict=False)
            try:
                meta.read(root / "meta.ini", encoding="utf-8-sig")
            except (OSError, UnicodeError, configparser.Error):
                continue
            general = meta["General"] if meta.has_section("General") else {}
            game = str(general.get("gameName", "")).strip().casefold()
            if (
                str(general.get("modid", "")).strip() == self.ConvergenceNexusId
                and game in {"eldenring", "elden ring"}
            ):
                return True
        return False

    def _reforged_natives(self) -> list[Path] | None:
        """This run's native DLLs when ELDEN RING Reforged is among them."""
        own = {
            os.path.normcase(str((GAMES_DIR / name)))
            for name in ("eldenring_mo2_bridge.dll", self.Me3Compat)
        }
        # executableForcedLoads lists them in load order
        libraries = [
            Path(load.library())
            for load in self.executableForcedLoads()
            if load.enabled() and os.path.normcase(str(Path(load.library()))) not in own
        ]
        if not any(
            library.name.casefold() == EldenRingModDataChecker._reforged_dll
            for library in libraries
        ):
            return None
        return libraries

    def _reforged_attach_config(
        self, libraries: list[Path], save_extension: str | None
    ) -> str:
        """me3's attach config for ELDEN RING Reforged run from MO2.

        MO2 has already done me3's part: the game files come from the VFS, the
        natives are this run's forced loads and the bridge names the saves, so
        the config lists those, offline (Reforged's online server checks a me3
        launch).
        """
        return json.dumps(
            {
                "game": "eldenring",
                "natives": [{"path": str(library)} for library in libraries],
                "packages": [
                    {"id": "mo2", "path": str(Path(self.dataDirectory().absolutePath()))}
                ],
                "savefile": f"ER0000.{save_extension}" if save_extension else None,
                "cache_path": None,
                "suspend": False,
                "boot_boost": False,
                "skip_logos": False,
                "start_online": False,
                "disable_arxan": (GAMES_DIR / self.Me3Compat).is_file(),
                "skip_steam_init": False,
            }
        )

    def _restore_profile_config_environment(self) -> None:
        if getattr(self, "_profile_config_env_active", False):
            key = self.ProfileLaunchConfigEnvironment
            previous = self._profile_config_env_previous
            if previous is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = previous
            self._profile_config_env_previous = None
            self._profile_config_env_active = False

        if getattr(self, "_native_initializer_env_active", False):
            key = self.NativeInitializerEnvironment
            previous = self._native_initializer_env_previous
            if previous is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = previous
            self._native_initializer_env_previous = None
            self._native_initializer_env_active = False

        if getattr(self, "_reforged_env_active", False):
            for key, previous in self._reforged_env_previous.items():
                if previous is None:
                    os.environ.pop(key, None)
                else:
                    os.environ[key] = previous
            self._reforged_env_previous = {}
            self._reforged_env_active = False
