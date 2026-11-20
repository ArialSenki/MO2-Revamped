"""MO2 Revamped: the libraries MO2 loads into the game: the bridge, the me3
compatibility DLL and the active mods' native DLLs.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import os
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
        seamless = self._active_seamless_dll() if native_loading else None
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
        seamless = self._active_seamless_dll()
        if seamless is None:
            return []
        return [{"library": seamless, "initializer": "modengine_ext_init"}]

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
