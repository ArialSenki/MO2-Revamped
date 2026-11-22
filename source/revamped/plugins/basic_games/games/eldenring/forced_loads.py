"""MO2 Revamped: the libraries MO2 loads into the game: the bridge, the me3
compatibility DLL and the active mods' native DLLs.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import codecs
import configparser
import os
import re
import shutil
import stat
from pathlib import Path

import mobase

from ...basic_features.utils import is_directory

from .common import (
    qCritical,
    qInfo,
    qWarning,
    _module_qinfo,
    GAMES_DIR,
)
from .checker import EldenRingModDataChecker


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


# the reparse points that lead elsewhere: symbolic links and junctions (mount
# points). OneDrive placeholders, deduplicated files and the like are files
# where they are
_LINK_REPARSE_TAGS = frozenset({0xA000000C, 0xA0000003})


class ForcedLoadsMixin:
    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isActive():
            return []
        # MO2 asks again for every executable and refresh; the summary is
        # logged only when it changes
        notes: list[str] = []

        mod_list = self._organizer.modList()
        profile_order = list(mod_list.allModsByProfilePriority())
        active_mods = [
            name
            for name in profile_order
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        profile = self._organizer.profile()
        revamped_native_profile = (
            self._resolve_revamped_native_profile(profile)
            if profile is not None
            else None
        )
        bridge_startup_options = False
        if profile is not None:
            profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
            (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                _clear_overwrite_after_game,
                _clear_overwrite_logs_before_game,
                _process_priority,
                bridge_log_history,
            ) = self._read_profile_launch_settings(profile_config)
            bridge_startup_options = (
                start_minimized
                or black_startup_background
                or exclude_cpu0
                or _process_priority == 1
                or bridge_log_history > 1
                or self._read_skip_intro_logos(profile_config)
            )

        if not active_mods and not bridge_startup_options:
            notes.append(
                "Elden Ring MO2: no active mods or bridge-dependent profile "
                "options; no native or asset bridge needed."
            )
            # BootBoost still speeds up a start without mods (with the intro
            # logos left on, nothing here needs the bridge, in either game)
            compat = self._me3_compat_load([])
            if compat is not None:
                notes.append(f"Elden Ring MO2: queued the me3 compatibility DLL {self.Me3Compat}.")
            self._log_forced_load_notes(notes)
            return [compat] if compat is not None else []
        if not active_mods:
            notes.append(
                "Elden Ring MO2: no active mods; loading the native bridge "
                "because bridge-dependent profile options are enabled."
            )

        native_loading = bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        )
        libraries = self._active_native_dlls() if native_loading else []
        if native_loading and revamped_native_profile is not None and revamped_native_profile.get("error"):
            # a direct eldenring.exe launch is then blocked in onAboutToRun;
            # a launcher run keeps every active DLL, as before the native profile
            qWarning(
                "Elden Ring MO2: the Revamped native profile could not be "
                f"read ({revamped_native_profile['error']}); its load order and "
                "switched-off entries are not applied to these forced loads."
            )
        if (
            native_loading
            and revamped_native_profile is not None
            and not revamped_native_profile.get("error")
        ):
            libraries = self._apply_revamped_native_load_order(
                libraries, revamped_native_profile, notes
            )
        root_layout_libraries = (
            self._active_root_layout_dlls(active_mods) if native_loading else []
        )
        if (
            root_layout_libraries
            and revamped_native_profile is not None
            and not revamped_native_profile.get("error")
        ):
            # a root DLL listed in the native profile loads once, in its order
            declared = revamped_native_profile.get("declared_libraries", set())
            disabled = revamped_native_profile.get("disabled_libraries", set())

            def queued_elsewhere(mod_name: str, library: Path) -> bool:
                return (
                    os.path.normcase(str(library)) in declared
                    or (mod_name.casefold(), library.name.casefold()) in disabled
                )

            root_layout_libraries = [
                item
                for item in root_layout_libraries
                if not queued_elsewhere(item[0], Path(item[2]))
            ]
        # the other game's Seamless Co-op, wherever a mod put it, never loads
        for library in [item for item in libraries if self._is_other_game_seamless(Path(item).name)]:
            notes.append(
                f"Elden Ring MO2: not loading '{library}': it is the other game's "
                f"Seamless Co-op, not {self.SeamlessDll}."
            )
            libraries.remove(library)
        for item in [item for item in root_layout_libraries if self._is_other_game_seamless(Path(item[2]).name)]:
            notes.append(
                f"Elden Ring MO2: not loading '{item[1]}' from '{item[0]}': it is the "
                f"other game's Seamless Co-op, not {self.SeamlessDll}."
            )
            root_layout_libraries.remove(item)
        if not native_loading:
            notes.append("Elden Ring MO2: native DLL loading is disabled in plugin settings.")
        if not libraries and not root_layout_libraries and active_mods:
            notes.append(
                "Elden Ring MO2: no active winning DLLs found under "
                "Game/DLLs/, legacy managed folders, or preserved root layouts."
            )

        if libraries:
            notes.append(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(libraries)} active mod DLL(s)."
            )
            for library in libraries:
                notes.append(f"Elden Ring MO2: queued native DLL {library.name}.")
        mod_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True).withForced(True)
            for library in libraries
        ]
        if root_layout_libraries:
            notes.append(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(root_layout_libraries)} active root-layout DLL(s)."
            )
            for mod_name, virtual_name, library, preserved in root_layout_libraries:
                if preserved:
                    kind = "preserved root DLL"
                elif Path(virtual_name).name.casefold() in (
                    EldenRingModDataChecker._root_loader_dlls
                ):
                    kind = "root loader DLL"
                else:
                    kind = "standard root DLL"
                notes.append(
                    f"Elden Ring MO2: queued {kind} {virtual_name} "
                    f"from active mod {mod_name}."
                )
        root_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                self.GameProcess, str(library)
            ).withEnabled(True).withForced(True)
            for _, _, library, _ in root_layout_libraries
        ]
        bridge = (GAMES_DIR / "eldenring_mo2_bridge.dll")
        bridge_load = None
        if bridge.is_file():
            if active_mods:
                notes.append(
                    "Elden Ring MO2: queued native and loose-asset bridge "
                    f"{bridge.name} for {len(active_mods)} active mod(s)."
                )
            else:
                notes.append(
                    "Elden Ring MO2: queued native bridge "
                    f"{bridge.name} for profile startup options."
                )
            bridge_load = mobase.ExecutableForcedLoadSetting(
                Path(self.GameBinary).name, str(bridge)
            ).withEnabled(True).withForced(True)
            notes.append(
                "Elden Ring MO2: placing the bridge before mod DLLs so its "
                "startup window hooks are installed as early as possible."
            )
        else:
            qCritical(
                "Elden Ring MO2: native and loose-asset bridge is missing; "
                "loose-file overrides and bridge diagnostics will not be available. "
                "MO2 can still load enabled native mod DLLs."
            )
        forced_loads = []
        if bridge_load is not None:
            forced_loads.append(bridge_load)
        # Load root-level proxy DLLs from their winning MO2 mod directory.
        # This avoids relying on the Windows loader finding the virtual root
        # file before MO2's VFS hooks are ready. Managed DLLs follow the proxy
        # so proxy loaders can discover their root-level companion files.
        forced_loads.extend(root_forced_loads)
        forced_loads.extend(mod_forced_loads)
        # Seamless Co-op from its active mod: loaded with the others, and
        # started by the bridge through its Mod Engine 2 entry point once
        # the game runs (see _seamless_initializer); launchers such as Mod
        # Engine 2 get no initializer, so there it stays inactive
        seamless = self._seamless_load_dll() if native_loading else None
        own_seamless = self._active_seamless_dll() if seamless is not None else None
        if own_seamless is not None and os.path.normcase(str(own_seamless)) != os.path.normcase(str(seamless)):
            # its copy for The Convergence loads in the mod DLL's place (the
            # native profile lists that one): never both
            for index, load in enumerate(forced_loads):
                if os.path.normcase(str(Path(load.library()))) == os.path.normcase(str(own_seamless)):
                    forced_loads[index] = mobase.ExecutableForcedLoadSetting(
                        self.GameProcess, str(seamless)
                    ).withEnabled(True).withForced(True)
        if seamless is not None and all(
            os.path.normcase(str(Path(load.library()))) != os.path.normcase(str(seamless))
            for load in forced_loads
        ):
            notes.append(f"Elden Ring MO2: queued Seamless Co-op ({seamless.name}) from its MO2 mod.")
            forced_loads.append(
                mobase.ExecutableForcedLoadSetting(self.GameProcess, str(seamless)).withEnabled(True).withForced(True)
            )
        arxan = self._me3_compat_load(forced_loads)
        if arxan is not None:
            # first: it hooks the entry point before anything else runs
            notes.append(f"Elden Ring MO2: queued the me3 compatibility DLL {self.Me3Compat}.")
            forced_loads.insert(0, arxan)
        self._log_forced_load_notes(notes)
        # in load order: MO2 hands them to usvfs so that they load as listed
        # (UsvfsConnector::updateForcedLibraries), the me3 compatibility DLL
        # first and the natives in the Native DLL Profile's order. Marked as
        # the game plugin's own, so Configure libraries can switch them off
        # but not edit or remove them.
        return forced_loads

    def _log_forced_load_notes(self, notes: list[str]) -> None:
        if notes == getattr(self, "_last_forced_load_notes", None):
            return
        self._last_forced_load_notes = list(notes)
        for note in notes:
            qInfo(note)

    def _runs_outside_vfs(self, app_path: str) -> bool:
        return (
            Path(app_path).name.casefold() != self.GameProcess
            and self._launch_method(app_path) == self.LaunchMethodSwap
        )

    @staticmethod
    def _resolved_mod_root(path: str) -> Path | None:
        """A mod folder with its links resolved; None when it can't be."""
        reads = _game_class()._mod_root_reads
        if reads is not None and path in reads:
            return reads[path]
        try:
            root = Path(path).resolve()
        except (OSError, RuntimeError):
            root = None
        if reads is not None:
            reads[path] = root
        return root

    def _active_seamless_dll(self) -> Path | None:
        """Seamless Co-op's DLL from the highest-priority active mod, unless
        the native profile switches it off (as it can any other DLL)."""
        found = self.seamless_dll_in_mods([mod for mod in self._mod_folders() if mod[2]])
        if found is None:
            return None
        mod_name, dll = found
        profile = self._organizer.profile()
        native_profile = self._resolve_revamped_native_profile(profile) if profile is not None else None
        if (
            native_profile is not None
            and not native_profile.get("error")
            and (mod_name.casefold(), dll.name.casefold()) in native_profile.get("disabled_libraries", set())
        ):
            # asked several times per launch and on every refresh
            self._log_when_changed("seamless", [(
                _module_qinfo,
                f"Elden Ring MO2: not loading '{dll.name}' from '{mod_name}'; "
                "it is switched off in the native profile.",
            )])
            return None
        self._log_when_changed("seamless", [])
        return dll

    def _seamless_initializer(self) -> list[dict]:
        """The bridge entry that starts Seamless Co-op's DLL: it exports Mod
        Engine 2's modengine_ext_init, which the bridge calls once the game
        runs its own code, as Mod Engine 2 and me3 do."""
        if not bool(self._organizer.pluginSetting(self.name(), self.NativeDllSetting)):
            return []
        seamless = self._seamless_load_dll()
        if seamless is None:
            return []
        return [{"library": seamless, "initializer": "modengine_ext_init"}]

    # where Seamless Co-op runs from with The Convergence, in the instance
    ConvergenceSeamlessFolder = "Elden Ring MO2 - Seamless for The Convergence"

    # a co-op save extension a mod's settings may give Seamless's copy: a short
    # plain word that is none of the game's other saves (sl2, Reforged's err,
    # Convergence's cnv, Seamless's shared co2) nor a file type of their folders
    _CoopExtensionPattern = re.compile(r"[a-z0-9]{2,8}")
    _ReservedSaveExtensions = frozenset(
        # mod: alt-saves' default extension
        {"sl2", "co2", "err", "cnv", "mod", "bak", "ini", "dll", "exe", "txt", "json", "tmp"}
    )

    @classmethod
    def _usable_coop_extension(cls, value: str | None) -> bool:
        folded = (value or "").casefold()
        return (
            value is not None and value.isascii()
            and cls._CoopExtensionPattern.fullmatch(folded) is not None
            and folded not in cls._ReservedSaveExtensions
        )

    def _seamless_load_dll(self) -> Path | None:
        """The Seamless Co-op DLL this run loads. With The Convergence it is a
        copy of Seamless's folder in the instance whose settings save to
        Convergence's extension (cnvco2, from the ersc_settings.ini it ships
        for its own launcher), so Convergence's co-op saves don't share
        ER0000.co2 with Seamless's without Convergence. Seamless reads its
        settings next to its DLL, a real file the VFS doesn't change; the copy
        keeps everything else of the user's settings (the password). Without
        Convergence, or when the copy can't be made, the mod's own DLL."""
        self._convergence_seamless_error = None
        seamless = self._active_seamless_dll()
        if seamless is None or self.GameShortName != "eldenring":
            return seamless
        extension = self._convergence_coop_extension()
        if extension is None:
            return seamless
        try:
            copy = self._convergence_seamless_copy(seamless, extension)
        except (OSError, UnicodeError) as error:
            # MO2 Revamped: not the mod's own DLL then: it would write
            # Convergence's characters into ER0000.co2, Seamless's save without
            # Convergence. The launch check says why (_convergence_seamless_ready)
            self._convergence_seamless_error = str(error)
            self._log_when_changed("convergence-seamless", [(
                qWarning,
                f"Elden Ring MO2: Seamless Co-op can't get The Convergence's save "
                f"extension ({error}); it isn't loaded, so it can't write into the "
                "save of Seamless without Convergence.",
            )])
            return None
        self._log_when_changed("convergence-seamless", [(
            _module_qinfo,
            f"Elden Ring MO2: The Convergence is active; Seamless Co-op runs from "
            f"'{copy.parent}' and saves to ER0000.{extension}, as with "
            "Convergence's own launcher.",
        )])
        return copy

    def _convergence_coop_extension(self) -> str | None:
        """The co-op save extension of the active The Convergence: the
        save_file_extension of the ersc_settings.ini it ships (DLLs/), else
        cnvco2. None when Convergence isn't active."""
        root = self._active_convergence_root()
        if root is None:
            return None
        for settings in (root / "DLLs" / "ersc_settings.ini", root / "ersc_settings.ini"):
            if not settings.is_file():
                continue
            value = self._ini_value(settings, "SAVE", "save_file_extension")
            # read as the save tool reads it (_settings_extension): without a
            # "; note", quotes or a leading dot
            if value:
                value = value.split(";", 1)[0].strip().strip("\"'").lstrip(".") or None
            # MO2 Revamped: "sl2" (or Reforged's err) would aim Convergence's
            # co-op saves at another game save
            if self._usable_coop_extension(value):
                return value
            if value:
                qWarning(f"Elden Ring MO2: save_file_extension '{value}' in '{settings}' "
                         f"isn't one Seamless Co-op can use with The Convergence; "
                         f"{self.ConvergenceCoopSaveExtension} instead.")
        return self.ConvergenceCoopSaveExtension

    @staticmethod
    def _ini_value(path: Path, section: str, key: str) -> str | None:
        config = configparser.ConfigParser(interpolation=None, strict=False)
        try:
            config.read(path, encoding="utf-8-sig")
            return config.get(section, key, fallback="").strip() or None
        except (OSError, UnicodeError, configparser.Error):
            return None

    def _convergence_seamless_copy(self, seamless: Path, extension: str) -> Path:
        """Seamless's folder copied into the instance (only what changed), with
        its settings file saving to the given extension."""
        source_dir = seamless.parent
        target_dir = Path(self._organizer.basePath()) / self.ConvergenceSeamlessFolder / source_dir.name
        settings_name = f"{seamless.stem}_settings.ini".casefold()
        copied: set[str] = set()
        for source in self._files_without_links(source_dir):
            relative = source.relative_to(source_dir)
            target = target_dir / relative
            if source.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            if str(relative).casefold() == settings_name:
                continue
            copied.add(relative.as_posix())
            info = source.stat()
            try:
                current = target.stat()
                if current.st_size == info.st_size and int(current.st_mtime) == int(info.st_mtime):
                    continue
            except OSError:
                pass
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
        # files an earlier copy took from the mod that it no longer has go;
        # any other file only in the copy stays (Seamless may write its own)
        self._prune_seamless_copy(target_dir, copied)
        settings_source = seamless.with_name(f"{seamless.stem}_settings.ini")
        # bytes that aren't UTF-8 (a password saved in the system's code
        # page) go back as they were
        raw = settings_source.read_bytes() if settings_source.is_file() else b""
        if raw.startswith((codecs.BOM_UTF16_LE, codecs.BOM_UTF16_BE)):
            # saved as UTF-16 by an editor: written back the same way
            text = raw.decode("utf-16")
            encode = lambda value: value.encode("utf-16")  # noqa: E731
        else:
            text = raw.removeprefix(codecs.BOM_UTF8).decode("utf-8", errors="surrogateescape")
            encode = lambda value: value.encode("utf-8", errors="surrogateescape")  # noqa: E731
        settings_target = target_dir / settings_source.name
        new_data = encode(self._with_save_extension(text, extension))
        try:
            unchanged = settings_target.read_bytes() == new_data
        except OSError:
            unchanged = False
        if not unchanged:
            target_dir.mkdir(parents=True, exist_ok=True)
            # whole or not at all: a half-written file would lose the password,
            # and Seamless closes the game without one
            partial = settings_target.with_name(settings_target.name + ".mo2r-tmp")
            partial.write_bytes(new_data)
            os.replace(partial, settings_target)
        copy = target_dir / seamless.name
        if not copy.is_file():
            # its own DLL a link (left out of the copy above) or not copied
            raise OSError(f"{copy} wasn't copied")
        return copy

    @staticmethod
    def _files_without_links(folder: Path):
        """Everything below folder, never through a junction or symbolic link
        (one in a mod's SeamlessCoop folder would copy files from elsewhere on
        the disk into the instance, or loop). Only links: OneDrive's files and
        deduplicated ones are reparse points too, and a Seamless in a mods
        folder under OneDrive was refused, stopping every Convergence launch."""

        def is_link(path: str) -> bool:
            info = os.lstat(path)
            if not info.st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT:
                return False
            return getattr(info, "st_reparse_tag", 0) in _LINK_REPARSE_TAGS

        for root, dirs, files in os.walk(folder, followlinks=False):
            kept = []
            for name in dirs:
                path = os.path.join(root, name)
                try:
                    if is_link(path):
                        continue
                except (OSError, AttributeError):
                    continue
                kept.append(name)
                yield Path(path)
            dirs[:] = kept
            for name in files:
                path = os.path.join(root, name)
                try:
                    if is_link(path):
                        continue
                except (OSError, AttributeError):
                    continue
                yield Path(path)

    @staticmethod
    def _seamless_copy_list(target_dir: Path) -> Path:
        """The list of the files the copy took from the mod, next to it."""
        return target_dir.parent / f".{target_dir.name}.copied.txt"

    @classmethod
    def _prune_seamless_copy(cls, target_dir: Path, copied: set[str]) -> None:
        """Removes from the copy the files the last copy took from the mod
        that the mod no longer has (an update that dropped or renamed one).
        A file that can't be removed now (the game still has it open) stays
        in the list, for the next run."""
        listing = cls._seamless_copy_list(target_dir)
        try:
            earlier = set(listing.read_text(encoding="utf-8").splitlines())
        except (OSError, UnicodeError):
            earlier = set()
        keep = set(copied)
        current = {name.casefold() for name in copied}
        root = target_dir.resolve()
        for name in sorted(earlier):
            if not name or name.casefold() in current:
                continue
            stale = (target_dir / name).resolve()
            # only inside the copy, whatever the list says
            if stale == root or root not in stale.parents:
                continue
            try:
                stale.unlink(missing_ok=True)
            except OSError:
                keep.add(name)
                continue
            parent = stale.parent
            while parent != root and root in parent.parents:
                try:
                    parent.rmdir()
                except OSError:
                    break
                parent = parent.parent
        new_listing = "\n".join(sorted(keep)) + "\n"
        try:
            if listing.read_text(encoding="utf-8") == new_listing:
                return
        except (OSError, UnicodeError):
            pass
        listing.parent.mkdir(parents=True, exist_ok=True)
        listing.write_text(new_listing, encoding="utf-8")

    @staticmethod
    def _with_save_extension(text: str, extension: str) -> str:
        """Seamless's settings with save_file_extension set in [SAVE], the
        rest as the user wrote it."""
        # only line breaks a settings file has: str.splitlines() also splits
        # on characters a password may hold (\x0b, \x1c, U+2028...)
        lines = re.split(r"\r\n|\n|\r", text)
        if lines and lines[-1] == "":
            lines.pop()
        section = None
        save_section = None
        for index, line in enumerate(lines):
            stripped = line.strip()
            if stripped.startswith("[") and stripped.endswith("]"):
                section = stripped[1:-1].strip().casefold()
                if section == "save":
                    save_section = index
                continue
            if section == "save" and stripped.split("=", 1)[0].strip().casefold() == "save_file_extension":
                lines[index] = f"save_file_extension = {extension}"
                return "\n".join(lines) + "\n"
        if save_section is None:
            lines += ["[SAVE]", f"save_file_extension = {extension}"]
        else:
            lines.insert(save_section + 1, f"save_file_extension = {extension}")
        return "\n".join(lines) + "\n"

    @classmethod
    def _is_other_game_seamless(cls, name: str) -> bool:
        """Whether this DLL is the other game's Seamless Co-op (ersc.dll in
        Nightreign, nrsc.dll in Elden Ring): its code would run against the
        wrong game."""
        folded = name.casefold()
        return folded in cls.SeamlessDlls and folded != cls.SeamlessDll.casefold()

    @staticmethod
    def _is_game_runtime_dll(name: str) -> bool:
        # libraries the game itself imports: a copy in a mod is never
        # force-loaded ahead of the game's own
        name = name.casefold()
        return (
            name == "steam_api64.dll"
            or (name.startswith("oo2core_") and name.endswith("_win64.dll"))
            # a mod loader's own host and the tools' libraries, shipped by
            # releases that bundle me3 (ELDEN RING Reforged): never a mod
            or name in {"me3_mod_host.dll", "libzstd.dll"}
        )

    def _active_root_layout_dlls(
        self, active_mods: list[str]
    ) -> list[tuple[str, str, Path, bool]]:
        """Return winning root DLLs from every active mod layout.

        Root-level DLLs keep their archive paths in either install mode,
        including proxy loaders with unfamiliar basenames. Only files whose
        winning origin is an enabled mod are queued, so lower-priority copies
        cannot override MO2's file conflict order.
        """
        if not active_mods:
            return []

        organizer = self._organizer
        mod_list = organizer.modList()
        active_priority = {name: index for index, name in enumerate(active_mods)}
        root_loader_names = EldenRingModDataChecker._root_loader_dlls
        selected: list[tuple[int, int, str, str, str, Path, bool]] = []
        seen_virtual: set[str] = set()
        seen_physical: set[str] = set()

        for mod_name in active_mods:
            mod = mod_list.getMod(mod_name)
            if mod is None:
                continue

            try:
                preserve_layout = bool(
                    str(
                        mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                            self.PreserveArchiveLayoutSetting, False
                        )
                    ).strip().casefold()
                    not in {"", "false", "0"}
                )
                file_tree = mod.fileTree()
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not inspect root DLL layout for "
                    f"active mod {mod_name}: {error}"
                )
                continue

            for entry in file_tree:
                if is_directory(entry):
                    continue
                file_name = entry.name()
                relative_path = Path(file_name)
                if (
                    len(relative_path.parts) != 1
                    or relative_path.suffix.casefold() != ".dll"
                    or self._is_game_runtime_dll(file_name)
                ):
                    continue

                is_root_loader = file_name.casefold() in root_loader_names
                virtual_name = relative_path.as_posix()
                virtual_key = virtual_name.casefold()
                if virtual_key in seen_virtual:
                    continue

                try:
                    origins = organizer.getFileOrigins(virtual_name)
                except Exception as error:
                    qCritical(
                        "Elden Ring MO2: could not resolve the winning mod for "
                        f"root DLL {virtual_name}: {error}"
                    )
                    continue

                winner = next(
                    (origin for origin in origins if origin in active_priority), None
                )
                if winner != mod_name:
                    continue

                library = Path(mod.absolutePath()).joinpath(file_name)
                try:
                    library = library.resolve(strict=True)
                except OSError as error:
                    qCritical(
                        f"Elden Ring MO2: cannot access root DLL {virtual_name} "
                        f"from active mod {mod_name}: {error}"
                    )
                    continue

                if not library.is_file():
                    continue

                physical_key = os.path.normcase(str(library))
                if physical_key in seen_physical:
                    continue
                seen_virtual.add(virtual_key)
                seen_physical.add(physical_key)
                selected.append(
                    (
                        0 if is_root_loader else 1,
                        active_priority[mod_name],
                        virtual_key,
                        mod_name,
                        virtual_name,
                        library,
                        preserve_layout,
                    )
                )

        selected.sort(key=lambda item: item[:3])
        return [
            (mod_name, virtual_name, library, preserve_layout)
            for _, _, _, mod_name, virtual_name, library, preserve_layout in selected
        ]

    def _active_native_dlls(self) -> list[Path]:
        """Return winning DLLs in managed directories from active profile mods.

        MO2's virtual file list decides which mod wins a file conflict. The
        physical DLL must come from that winning mod because forced-load
        libraries are resolved before the game process can read its VFS. The
        ``MO2_DLLs`` and ``mods`` paths remain supported for older installs.
        """
        organizer = self._organizer
        mod_list = organizer.modList()
        profile_order = list(mod_list.allModsByProfilePriority())
        active_order = [
            name
            for name in profile_order
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        active_priority = {name: index for index, name in enumerate(active_order)}
        if not active_priority:
            return []

        data_root = Path(self.dataDirectory().absolutePath()).absolute()
        selected: list[tuple[int, str, Path]] = []
        seen_virtual: set[str] = set()
        seen_physical: set[str] = set()

        dll_directories = (
            EldenRingModDataChecker._dll_directory,
            "native",
            "natives",
            "external_dlls",
            "MO2_DLLs",
            "mods",
        )
        dll_roots = {directory.casefold() for directory in dll_directories}
        virtual_files: list[tuple[str, str]] = []
        for directory in dll_directories:
            try:
                virtual_files.extend(
                    (directory, path)
                    for path in organizer.findFiles(directory, "*")
                )
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not enumerate DLLs under "
                    f"Game/{directory}/: {error}"
                )

        for search_directory, virtual_file in virtual_files:
            virtual_path = Path(virtual_file)
            if (
                virtual_path.suffix.casefold() != ".dll"
                or virtual_path.name.casefold()
                in EldenRingModDataChecker._root_loader_dlls
                or self._is_game_runtime_dll(virtual_path.name)
            ):
                continue

            try:
                relative_path = virtual_path.absolute().relative_to(data_root)
            except ValueError:
                # MO2 may return a resolved physical path. Preserve any
                # subdirectory below the searched managed root so support
                # DLLs can be recognized and excluded from forced loading.
                if virtual_path.is_absolute():
                    # the last match: the physical path starts with MO2's
                    # own "mods" folder (or a mod named like a DLL folder)
                    path_parts = virtual_path.parts
                    root_index = next(
                        (
                            index
                            for index in range(len(path_parts) - 2, -1, -1)
                            if path_parts[index].casefold() == search_directory.casefold()
                        ),
                        None,
                    )
                    if root_index is None:
                        relative_path = Path(search_directory) / virtual_path.name
                    else:
                        relative_path = Path(*path_parts[root_index:])
                elif (
                    virtual_path.parts
                    and virtual_path.parts[0].casefold() in dll_roots
                ):
                    relative_path = virtual_path
                else:
                    relative_path = Path(search_directory) / virtual_path

            if (
                len(relative_path.parts) < 2
                or relative_path.parts[0].casefold()
                not in dll_roots
                or relative_path.suffix.casefold() != ".dll"
            ):
                continue

            virtual_name = relative_path.as_posix()
            virtual_key = virtual_name.casefold()
            if virtual_key in seen_virtual:
                continue
            seen_virtual.add(virtual_key)

            try:
                origins = organizer.getFileOrigins(virtual_name)
            except Exception as error:
                qCritical(
                    "Elden Ring MO2: could not resolve the winning mod for "
                    f"{virtual_name}: {error}"
                )
                continue

            if not origins:
                continue

            winner = next(
                (origin for origin in origins if origin in active_priority), None
            )
            if winner is None:
                # Ignore unmanaged game files and files with no active origin.
                continue

            mod = mod_list.getMod(winner)
            if mod is None:
                continue

            library = Path(mod.absolutePath()).joinpath(*relative_path.parts)
            try:
                library = library.resolve(strict=True)
            except OSError as error:
                qCritical(
                    f"Elden Ring MO2: cannot access {virtual_name} "
                    f"from active mod {winner}: {error}"
                )
                continue

            if not library.is_file():
                continue

            physical_key = os.path.normcase(str(library))
            if physical_key in seen_physical:
                continue
            seen_physical.add(physical_key)
            selected.append(
                (active_priority[winner], virtual_key, library)
            )

        selected.sort(key=lambda item: (item[0], item[1]))
        return [library for _, _, library in selected]
