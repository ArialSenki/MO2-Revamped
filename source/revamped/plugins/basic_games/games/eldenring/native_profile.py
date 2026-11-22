"""MO2 Revamped: the profile's Native DLL profile (eldenring_mo2_startup.ini):
its entries resolved against the active mods, and the load order it gives.

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
    _module_qinfo,
    _module_qwarning,
    _tr,
)
from .checker import EldenRingModDataChecker


class NativeProfileMixin:
    def _installed_native_routes_for_mod(
        self, mapped_mod_info, mod_root: Path, mapped_mod: str
    ) -> tuple[dict[str, str], list[str]]:
        """Read and validate one mod's installed native-route index."""
        route_index_by_source: dict[str, str] = {}
        installed_native_routes: list[str] = []
        try:
            stored_route_index = mapped_mod_info.pluginSettings(
                self.ArchiveLayoutInstallerName
            ).get(self.InstalledNativeRouteIndexSetting, "")
            if isinstance(stored_route_index, str) and stored_route_index.strip():
                stored_route_index = json.loads(stored_route_index)
            if isinstance(stored_route_index, dict):
                stored_routes = stored_route_index.get("dlls", [])
                if isinstance(stored_routes, list):
                    for installed in stored_routes:
                        if not isinstance(installed, str):
                            continue
                        installed_path = Path(installed.replace("\\", "/"))
                        if (
                            installed_path.is_absolute()
                            or not installed_path.parts
                            or any(part in {"", ".", ".."} for part in installed_path.parts)
                            or ":" in installed_path.parts[0]
                            or installed_path.suffix.casefold() != ".dll"
                        ):
                            continue
                        try:
                            resolved_installed_path = mod_root.joinpath(
                                *installed_path.parts
                            ).resolve(strict=True)
                            resolved_installed_path.relative_to(mod_root)
                        except (OSError, ValueError):
                            continue
                        if resolved_installed_path.is_file():
                            installed_native_routes.append(installed_path.as_posix())
                stored_mapping = stored_route_index.get("source_to_installed", {})
                if isinstance(stored_mapping, dict):
                    for source, installed in stored_mapping.items():
                        if not isinstance(source, str) or not isinstance(installed, str):
                            continue
                        source_key = source.replace("\\", "/").strip("/").casefold()
                        installed_path = Path(installed.replace("\\", "/"))
                        if (
                            not source_key
                            or installed_path.is_absolute()
                            or not installed_path.parts
                            or any(part in {"", ".", ".."} for part in installed_path.parts)
                            or ":" in installed_path.parts[0]
                            or installed_path.suffix.casefold() != ".dll"
                        ):
                            continue
                        route_index_by_source[source_key] = installed_path.as_posix()
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
            qInfo(
                "Elden Ring MO2: ignored an unavailable installed native-route "
                f"index for '{mapped_mod}': {error}."
            )

        return route_index_by_source, installed_native_routes

    def _resolve_revamped_native_profile(self, profile) -> dict | None:
        """Resolve Revamped's ordered native list against installed mod routes.
        MO2 asks for the forced loads on every refresh and a launch resolves the
        profile several times: its log lines are written when they change."""
        notes: list = []
        try:
            return self._read_revamped_native_profile(profile, notes)
        finally:
            self._log_when_changed("native_profile", notes)

    def _log_when_changed(self, key: str, notes: list) -> None:
        """Writes (log function, text) lines unless they are the ones last
        written under this key."""
        logged = self.__dict__.setdefault("_logged_notes", {})
        if logged.get(key) == notes:
            return
        logged[key] = list(notes)
        for log, text in notes:
            log(text)

    def _read_revamped_native_profile(self, profile, notes: list) -> dict | None:
        def qInfo(text):  # noqa: N802 - collected for _log_when_changed
            notes.append((_module_qinfo, text))

        def qWarning(text):  # noqa: N802
            notes.append((_module_qwarning, text))

        mo2_mod_name = ""
        try:
            profile_root = Path(profile.absolutePath()).resolve()
            profile_config = profile_root / self.ProfileLaunchConfig
            config = configparser.ConfigParser(interpolation=None)
            # a file that exists but cannot be read is reported, not taken
            # as "no native profile"
            try:
                with profile_config.open("r", encoding="utf-8-sig") as stream:
                    config.read_file(stream)
            except FileNotFoundError:
                return None
            section = "RevampedNativeProfile"
            if not config.has_section(section):
                return None
            mo2_mod_name = config.get(section, "mod", fallback="").strip()

            natives_by_index: dict[int, dict] = {}
            for key, value in config.items(section):
                if key in {"mod", "version"}:
                    continue
                prefix, separator, raw_index = key.partition("_")
                if (
                    not separator
                    or not raw_index.isdecimal()
                    or prefix not in {"native", "optional", "enabled", "initializer", "mod"}
                ):
                    raise ValueError(
                        _tr("Unsupported Revamped native profile setting '{0}'.").format(key)
                    )
                index = int(raw_index)
                if index <= 0:
                    raise ValueError(_tr("Native entry numbers must be greater than zero."))
                entry = natives_by_index.setdefault(index, {})
                if prefix == "native":
                    if "path" in entry:
                        raise ValueError(_tr("Native entry {0} is declared more than once.").format(index))
                    entry["path"] = value.strip()
                elif prefix in {"optional", "enabled"}:
                    if value.strip().casefold() not in {"true", "false"}:
                        raise ValueError(
                            _tr("'{0}' must be set to true or false.").format(key)
                        )
                    # optional_1 and optional_01 name the same entry
                    if prefix in entry:
                        raise ValueError(
                            _tr("Native entry {0} is declared more than once.").format(index)
                        )
                    entry[prefix] = value.strip().casefold() == "true"
                elif prefix == "mod":
                    if "mod_name" in entry:
                        raise ValueError(_tr("Native entry {0} has more than one mod mapping.").format(index))
                    entry["mod_name"] = value.strip()
                else:
                    if "initializer" in entry:
                        raise ValueError(
                            _tr("Native entry {0} has more than one initializer.").format(index)
                        )
                    # an empty value (a hand edit) means no initializer
                    if value.strip():
                        entry["initializer"] = {"function": value.strip()}

            natives = [natives_by_index[index] for index in sorted(natives_by_index)]
            if not natives:
                raise ValueError(
                    _tr("The [RevampedNativeProfile] section does not declare any native DLLs.")
                )

            mod_list = self._organizer.modList()
            active_mods = [
                name
                for name in mod_list.allModsByProfilePriority()
                if mod_list.state(name) & mobase.ModState.ACTIVE
            ]
            active_mod_by_name: dict[str, str] = {}
            active_mod_priority: dict[str, int] = {}
            for priority, name in enumerate(active_mods):
                folded_name = name.casefold()
                active_mod_by_name.setdefault(folded_name, name)
                active_mod_priority.setdefault(name, priority)
            entries = []
            declared_names: set[str] = set()
            declared_libraries: set[str] = set()
            # (mod, DLL name) of entries switched off in the tool: the
            # general DLL scan must not load them either
            disabled_libraries: set[tuple[str, str]] = set()
            seen_routes: set[tuple[str, str]] = set()
            seen_virtual_paths: dict[str, dict] = {}
            # for each game path, the copy its kept initializer belongs to
            initializer_sources: dict[str, Path] = {}
            scope_priorities: set[int] = set()
            native_route_index_cache: dict[
                str, tuple[dict[str, str], list[str]]
            ] = {}
            legacy_scope_root = None
            legacy_scope_mod = None
            legacy_profile = not any("mod_name" in native for native in natives)
            if legacy_profile:
                if not mo2_mod_name:
                    raise ValueError(
                        _tr("The [RevampedNativeProfile] section needs a mod mapping.")
                    )
                legacy_scope_mod = active_mod_by_name.get(mo2_mod_name.casefold())
                if legacy_scope_mod is None:
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is not enabled in this profile.").format(mo2_mod_name)
                    )
                legacy_mod_info = mod_list.getMod(legacy_scope_mod)
                if legacy_mod_info is None:
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is unavailable.").format(legacy_scope_mod)
                    )
                legacy_scope_root = Path(legacy_mod_info.absolutePath()).resolve()
                if not legacy_scope_root.is_dir():
                    raise ValueError(
                        _tr("The mapped MO2 mod folder '{0}' is unavailable.").format(legacy_scope_root)
                    )
            # known before any entry resolves: an entry of an overridden copy can
            # come before the winner's switched-off one
            for native in natives:
                if isinstance(native, dict) and native.get("enabled", True) is False:
                    disabled_mod = str(native.get("mod_name") or mo2_mod_name).strip()
                    disabled_route = str(native.get("path") or "").replace("\\", "/")
                    if disabled_mod and disabled_route:
                        disabled_libraries.add(
                            (disabled_mod.casefold(), disabled_route.rsplit("/", 1)[-1].casefold())
                        )
            for native in natives:
                if not isinstance(native, dict):
                    raise ValueError(_tr("A Revamped native profile entry is invalid."))
                enabled = native.get("enabled", True)
                if not isinstance(enabled, bool):
                    raise ValueError(_tr("A native profile entry's enabled value must be true or false."))
                if not enabled:
                    continue

                native_mod_name = str(native.get("mod_name") or mo2_mod_name).strip()
                if not native_mod_name:
                    raise ValueError(
                        _tr("Every native entry needs a mod mapping, either in 'mod' "
                        "or in its indexed 'mod_N' value.")
                    )
                mapped_mod = active_mod_by_name.get(native_mod_name.casefold())
                if mapped_mod is None:
                    if "mod_name" in native:
                        qInfo(
                            "Elden Ring MO2: skipped native profile entry for "
                            f"inactive mod '{native_mod_name}'."
                        )
                        continue
                    raise ValueError(
                        _tr("The mapped MO2 mod '{0}' is not enabled in this profile.").format(native_mod_name)
                    )
                mapped_mod_info = mod_list.getMod(mapped_mod)
                if mapped_mod_info is None:
                    raise ValueError(_tr("The mapped MO2 mod '{0}' is unavailable.").format(mapped_mod))
                mod_root = Path(mapped_mod_info.absolutePath()).resolve()
                if not mod_root.is_dir():
                    raise ValueError(_tr("The mapped MO2 mod folder '{0}' is unavailable.").format(mod_root))

                route_index_cache_key = os.path.normcase(str(mod_root))
                if route_index_cache_key not in native_route_index_cache:
                    native_route_index_cache[route_index_cache_key] = (
                        self._installed_native_routes_for_mod(
                            mapped_mod_info, mod_root, mapped_mod
                        )
                    )
                route_index_by_source, installed_native_routes = (
                    native_route_index_cache[route_index_cache_key]
                )

                raw_path = native.get("path")
                if not isinstance(raw_path, str) or not raw_path.strip():
                    raise ValueError(_tr("An enabled Revamped native entry is missing its path."))
                normalized_path = raw_path.strip().replace("\\", "/")
                relative_path = Path(normalized_path)
                if (
                    relative_path.is_absolute()
                    or not relative_path.parts
                    or any(part in {"", ".", ".."} for part in relative_path.parts)
                    or ":" in relative_path.parts[0]
                    or relative_path.suffix.casefold() != ".dll"
                ):
                    raise ValueError(
                        _tr("Unsupported Revamped native path '{0}'. Use a relative DLL path.").format(raw_path)
                    )

                route = relative_path.as_posix()
                # a profile saved before these stayed out of the list
                if (
                    self._is_game_runtime_dll(relative_path.name)
                    or relative_path.name.casefold() in EldenRingModDataChecker._root_loader_dlls
                ):
                    qInfo(f"Elden Ring MO2: skipped '{route}' from the native profile: never forced.")
                    continue
                if self._is_other_game_seamless(relative_path.name):
                    qWarning(
                        f"Elden Ring MO2: skipped '{route}' of '{mapped_mod}' from the "
                        f"native profile: it is the other game's Seamless Co-op, not "
                        f"{self.SeamlessDll}."
                    )
                    continue
                route_key = (mapped_mod.casefold(), route.casefold())
                if route_key in seen_routes:
                    raise ValueError(
                        _tr("The Revamped native profile lists '{0}' more than once.").format(route)
                    )
                seen_routes.add(route_key)

                optional = native.get("optional", False)
                if not isinstance(optional, bool):
                    raise ValueError(_tr("A native profile entry's optional value must be true or false."))

                # The installer records the final mod-relative route for every
                # archive DLL it can identify. Use that first, then support
                # older or manually maintained mods through known MO2 paths.
                route_candidates = []
                # the index is keyed by the archive's own path, without the mod
                indexed_route = route_index_by_source.get(route.casefold())
                if indexed_route:
                    route_candidates.append(indexed_route)
                route_candidates.append(route)
                route_parts = relative_path.parts
                if route_parts[0].casefold() in {"native", "natives"} and len(route_parts) > 1:
                    native_tail = Path(*route_parts[1:]).as_posix()
                    route_candidates.extend((
                        f"DLLs/{route}",
                        f"DLLs/{native_tail}",
                        f"DLLs/{relative_path.name}",
                    ))
                else:
                    route_candidates.extend((f"DLLs/{route}", f"DLLs/{relative_path.name}"))

                resolved_candidates: list[tuple[str, Path]] = []
                checked_routes: set[str] = set()
                for candidate_route in route_candidates:
                    candidate_key = candidate_route.casefold()
                    if candidate_key in checked_routes:
                        continue
                    checked_routes.add(candidate_key)
                    candidate_path = mod_root.joinpath(*Path(candidate_route).parts)
                    try:
                        candidate_path = candidate_path.resolve(strict=True)
                        candidate_path.relative_to(mod_root)
                    except (OSError, ValueError):
                        continue
                    if candidate_path.is_file():
                        resolved_candidates.append(
                            (candidate_route.replace("\\", "/"), candidate_path)
                        )
                        break

                if not resolved_candidates and installed_native_routes:
                    route_aliases = [route.casefold()]
                    if (
                        route_parts[0].casefold() in {"native", "natives"}
                        and len(route_parts) > 1
                    ):
                        route_aliases.append(native_tail.casefold())
                    indexed_matches = {
                        installed
                        for installed in installed_native_routes
                        if any(
                            installed.casefold() == alias
                            or installed.casefold().endswith("/" + alias)
                            for alias in route_aliases
                        )
                    }
                    if not indexed_matches:
                        indexed_matches = {
                            installed
                            for installed in installed_native_routes
                            if Path(installed).name.casefold()
                            == relative_path.name.casefold()
                        }
                    if len(indexed_matches) > 1:
                        raise ValueError(
                            _tr("The installed route index contains multiple matches for '{0}'. Keep the DLL at a distinct relative path or remove the duplicate.").format(route)
                        )
                    if indexed_matches:
                        indexed_route = next(iter(indexed_matches))
                        indexed_path = mod_root.joinpath(*Path(indexed_route).parts)
                        try:
                            indexed_path = indexed_path.resolve(strict=True)
                            indexed_path.relative_to(mod_root)
                        except (OSError, ValueError):
                            indexed_path = None
                        if indexed_path is not None and indexed_path.is_file():
                            resolved_candidates.append(
                                (indexed_route, indexed_path)
                            )

                if not resolved_candidates:
                    dll_root = mod_root / "DLLs"
                    if dll_root.is_dir():
                        try:
                            matches = []
                            for candidate_path in dll_root.rglob(relative_path.name):
                                try:
                                    resolved = candidate_path.resolve(strict=True)
                                    resolved.relative_to(mod_root)
                                except (OSError, ValueError):
                                    continue
                                if resolved.is_file():
                                    matches.append((resolved.relative_to(mod_root).as_posix(), resolved))
                        except OSError as error:
                            raise ValueError(
                                _tr("Could not search MO2 layout DLLs for '{0}': {1}").format(route, error)
                            ) from error
                        unique_matches = {
                            str(path).casefold(): (virtual, path)
                            for virtual, path in matches
                        }
                        if len(unique_matches) > 1:
                            raise ValueError(
                                _tr("MO2 layout contains multiple DLLs named '{0}'. Keep the DLL at its declared folder or remove the duplicate.").format(relative_path.name)
                            )
                        if unique_matches:
                            resolved_candidates.append(next(iter(unique_matches.values())))

                if not resolved_candidates:
                    if optional:
                        qInfo(f"Elden Ring MO2: skipped optional Revamped native '{route}'.")
                        continue
                    # a mod's update can rename or remove it: the launch goes on
                    # with the DLLs the mods have now
                    qWarning(f"Elden Ring MO2: Revamped native '{route}' is no longer in its mod; skipped.")
                    continue

                virtual_path, _source_path = resolved_candidates[0]
                virtual_key = virtual_path.casefold()
                try:
                    origins = self._organizer.getFileOrigins(virtual_path)
                except Exception as error:
                    raise ValueError(
                        _tr("MO2 could not resolve the winning origin for '{0}': {1}").format(virtual_path, error)
                    ) from error
                # the highest priority wins the file in MO2, as in the
                # general DLL scan
                winner = max(
                    (
                        name
                        for name in origins
                        if name in active_mod_priority
                    ),
                    key=lambda name: active_mod_priority[name],
                    default=None,
                )
                if winner is None:
                    if not optional:
                        qWarning(f"Elden Ring MO2: Revamped native '{virtual_path}' has no active file origin; skipped.")
                    continue
                if (winner.casefold(), Path(virtual_path).name.casefold()) in disabled_libraries:
                    qInfo(
                        f"Elden Ring MO2: not loading '{virtual_path}' from '{winner}'; "
                        "it is switched off in the native profile."
                    )
                    continue
                winning_mod = mod_list.getMod(winner)
                if winning_mod is None:
                    raise ValueError(
                        _tr("MO2's winning mod '{0}' for '{1}' is unavailable.").format(winner, virtual_path)
                    )
                winning_root = Path(winning_mod.absolutePath()).resolve()
                try:
                    library = winning_root.joinpath(*Path(virtual_path).parts).resolve(strict=True)
                except OSError:
                    # MO2's file list is older than the disk (the file went
                    # since its last refresh): the launch goes on without it
                    qWarning(
                        f"Elden Ring MO2: Revamped native '{virtual_path}' is missing from "
                        f"'{winner}'; skipped."
                    )
                    continue
                try:
                    library.relative_to(winning_root)
                except ValueError as error:
                    raise ValueError(
                        _tr("The winning path for '{0}' leaves its MO2 mod.").format(virtual_path)
                    ) from error
                if not library.is_file():
                    raise ValueError(_tr("The winning file for '{0}' is not a DLL.").format(virtual_path))
                # loaded by its path inside MO2's mods folder, not the resolved
                # one: through a linked mods folder the resolved path loses the
                # folder names the bridge looks for (SeamlessCoop's DLL is known
                # by "mods"), and Seamless Co-op's own load names it this way
                load_path = Path(winning_mod.absolutePath()).joinpath(*Path(virtual_path).parts)

                initializer = native.get("initializer")
                if initializer is not None and not isinstance(initializer, dict):
                    raise ValueError(
                        _tr("Revamped native '{0}' has an unsupported initializer condition.").format(route)
                    )
                initializer_function = initializer.get("function") if initializer else None
                if initializer and initializer_function is None:
                    raise ValueError(
                        _tr("Revamped native '{0}' uses an initializer condition this plugin does not support yet.").format(route)
                    )
                if initializer_function is not None and (
                    not isinstance(initializer_function, str)
                    or not initializer_function.isascii()
                    or not initializer_function.isidentifier()
                ):
                    raise ValueError(_tr("Revamped native '{0}' has an unsupported initializer name.").format(route))
                if initializer_function == "NrmInitialize":
                    assets_directory = next(
                        (
                            child
                            for child in library.parent.iterdir()
                            if child.is_dir() and child.name.casefold() == "assets"
                        ),
                        None,
                    )
                    if assets_directory is None:
                        raise ValueError(
                            _tr("NightreignMovement.dll needs its adjacent 'assets' folder. "
                            "Keep original structure retains it; MO2 layout must keep it beside the DLL under DLLs/.")
                        )

                if virtual_path.casefold() != route.casefold():
                    qInfo(
                        f"Elden Ring MO2: resolved Revamped native '{route}' via "
                        f"MO2 layout path '{virtual_path}'."
                    )
                # one game path is one file, the winner's, however many entries
                # name it (a profile saved when another mod won it): it loads
                # once, at the first entry's place
                earlier = seen_virtual_paths.get(virtual_key)
                if earlier is not None:
                    qInfo(
                        f"Elden Ring MO2: '{route}' of '{mapped_mod}' is the same game "
                        f"file as an earlier native profile entry; it loads once, from '{winner}'."
                    )
                    # an initializer names an export of its own copy of the DLL:
                    # the first entry's goes when its copy is not the one that loads
                    loads = os.path.normcase(str(earlier["real_library"]))
                    if (
                        earlier["initializer"]
                        and os.path.normcase(str(initializer_sources[virtual_key])) != loads
                    ):
                        qInfo(
                            f"Elden Ring MO2: dropped initializer '{earlier['initializer']}' of "
                            f"'{earlier['route']}': the copy that loads is from '{winner}'."
                        )
                        earlier["initializer"] = None
                    if initializer_function and not earlier["initializer"]:
                        if os.path.normcase(str(_source_path)) == loads:
                            earlier["initializer"] = initializer_function
                            initializer_sources[virtual_key] = _source_path
                        else:
                            qInfo(
                                f"Elden Ring MO2: dropped initializer '{initializer_function}' of "
                                f"'{mapped_mod}': the copy that loads is from '{winner}'."
                            )
                    continue
                entry = {
                    "route": route,
                    "virtual_path": virtual_path,
                    "mod_name": winner,
                    "library": load_path,
                    "real_library": library,
                    "initializer": initializer_function,
                }
                seen_virtual_paths[virtual_key] = entry
                initializer_sources[virtual_key] = _source_path
                entries.append(entry)
                declared_libraries.add(os.path.normcase(str(library.resolve())))
                scope_priorities.add(active_mod_priority[winner])
                if legacy_profile:
                    legacy_scope_root = mod_root
                    legacy_scope_mod = mapped_mod
                declared_names.add(relative_path.name.casefold())

            return {
                "error": None,
                "configuration": profile_config,
                "scope_mod": legacy_scope_mod or mo2_mod_name,
                "scope_root": legacy_scope_root,
                "scope_priority": (
                    active_mods.index(legacy_scope_mod)
                    if legacy_profile and legacy_scope_mod in active_mods
                    else min(scope_priorities, default=len(active_mods))
                ),
                "entries": entries,
                "declared_names": declared_names,
                "declared_libraries": declared_libraries,
                "disabled_libraries": disabled_libraries,
            }
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            return {
                "error": str(error),
                "scope_mod": mo2_mod_name,
                "entries": [],
                "declared_names": set(),
            }

    def _apply_revamped_native_load_order(
        self, libraries: list[Path], profile_data: dict, notes: list | None = None
    ) -> list[Path]:
        """Apply the Revamped profile's declared native DLL order. With notes,
        its log lines join the forced loads' summary instead."""
        note = notes.append if notes is not None else _module_qinfo
        scope_root = profile_data.get("scope_root")
        declared_libraries = profile_data.get("declared_libraries", set())
        disabled_libraries = profile_data.get("disabled_libraries", set())
        if scope_root is None and not declared_libraries and not disabled_libraries:
            return libraries
        scope_root = Path(scope_root) if scope_root is not None else None
        declared_names = profile_data.get("declared_names", set())
        mod_list = self._organizer.modList()
        active_mods = [
            name
            for name in mod_list.allModsByProfilePriority()
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]

        # each mod folder is resolved once (once per launch while one is
        # prepared), not once per library and mod: resolve() opens a handle
        # on Windows, and this runs for every forced-load request
        mod_root_priority: dict[str, int] = {}
        for index, mod_name in enumerate(active_mods):
            mod = mod_list.getMod(mod_name)
            if mod is None:
                continue
            root = self._resolved_mod_root(mod.absolutePath())
            if root is not None:
                # the first (lowest) priority owns a folder, as before
                mod_root_priority.setdefault(os.path.normcase(str(root)), index)
        owner_cache: dict[str, int] = {}

        def library_priority(library: Path) -> int:
            key = os.path.normcase(str(library))
            if key not in owner_cache:
                owner_cache[key] = next(
                    (
                        mod_root_priority[os.path.normcase(str(parent))]
                        for parent in Path(library).parents
                        if os.path.normcase(str(parent)) in mod_root_priority
                    ),
                    len(active_mods),
                )
            return owner_cache[key]

        retained: list[Path] = []
        for library in libraries:
            library_path = Path(library)
            if scope_root is not None:
                try:
                    library_path.relative_to(scope_root)
                    continue
                except ValueError:
                    pass
                if library_path.name.casefold() in declared_names:
                    continue
            try:
                library_key = os.path.normcase(str(library_path.resolve()))
            except (OSError, RuntimeError):
                library_key = os.path.normcase(os.path.abspath(str(library_path)))
            if library_key in declared_libraries:
                continue
            if disabled_libraries:
                owner = library_priority(library_path)
                if owner < len(active_mods) and (
                    active_mods[owner].casefold(),
                    library_path.name.casefold(),
                ) in disabled_libraries:
                    note(
                        f"Elden Ring MO2: not loading '{library_path.name}' from "
                        f"'{active_mods[owner]}'; it is switched off in the native profile."
                    )
                    continue
            retained.append(library_path)

        scope_priority = int(profile_data.get("scope_priority", len(active_mods)))

        insertion = next(
            (
                index
                for index, library in enumerate(retained)
                if library_priority(library) > scope_priority
            ),
            len(retained),
        )
        ordered = [Path(entry["library"]) for entry in profile_data.get("entries", [])]
        for entry in profile_data.get("entries", []):
            note(
                f"Elden Ring MO2: Revamped native load order queued '{entry['route']}' "
                f"as '{entry['virtual_path']}' from '{entry['mod_name']}'."
            )
        return [*retained[:insertion], *ordered, *retained[insertion:]]
