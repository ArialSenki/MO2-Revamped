"""MO2 Revamped: the mod data checker of Elden Ring and Nightreign: which
archive and mod layouts are game data, and the standard layout installs give
them.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import re
from pathlib import Path

import mobase

from ...basic_features import BasicModDataChecker, GlobPatterns
from ...basic_features.utils import is_directory

from .common import (
    qInfo,
    _tr,
)


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class EldenRingModDataChecker(BasicModDataChecker):
    """Recognize Elden Ring asset and native-mod archive layouts.

    MO2 stores game assets below ``Game`` and native plugins under
    ``Game/DLLs``. Flat mixed archives therefore keep game resource folders in
    place while DLLs, configuration sidecars, and a mod's companion fonts
    directory are routed together during installation.
    """

    _dll_directory = "DLLs"
    _archive_wrapper_depth_limit = 16
    _legacy_dll_directories = {"external_dlls", "mo2_dlls", "mods"}
    # folders whose DLLs are already where the standard layout puts them
    _managed_dll_directories = frozenset(
        # _reforgedonline: the Reforged release kept for its own launcher
        {_dll_directory.casefold(), *_legacy_dll_directories, "plugins", "_reforgedonline", "seamlesscoop"}
    )

    _asset_directories = {
        "action",
        "animation",
        "animations",
        "asset",
        "assets",
        "chr",
        "common",
        "cutscene",
        "event",
        "expression",
        "facegen",
        "font",
        "map",
        "material",
        "menu",
        "msg",
        "movie",
        "movie_dlc",
        # game files with no folder of their own (ELDEN RING Reforged's
        # autoinvadepoint, decaltex and mapinfotex)
        "other",
        "param",
        "parts",
        "script",
        "sd",
        "sfx",
        "shader",
        "sound",
        "system",
        "texture",
        "textures",
        "time",
        "wp",
    }


    _content_directories = _asset_directories | {
        "config",
        "configs",
        "dlls",
        "external_dlls",
        "fonts",
        "mod",
        "mods",
        "mo2_dlls",
        "native",
        "natives",
        "plugins",
    }
    _package_directories = {
        "<game>",
        "elden ring",
        "elden-ring",
        "eldenring",
        "elden_ring",
        "nightreign",
        "elden ring nightreign",
        "elden-ring-nightreign",
        "eldenring nightreign",
        "elden_ring_nightreign",
        "data",
        "game",
        "dlls",
        "external_dlls",
        "mod",
        "mods",
        "mo2_dlls",
        "native",
        "natives",
    }
    _root_loader_dlls = {
        "d3d11.dll",
        "d3d12.dll",
        "dbghelp.dll",
        "dinput8.dll",
        "dxgi.dll",
        "version.dll",
        "winmm.dll",
        "xinput1_3.dll",
        "xinput9_1_0.dll",
    }
    _sidecar_extensions = {
        ".cfg",
        ".ini",
        ".json",
        ".log",
        ".toml",
        ".xml",
        ".yaml",
        ".yml",
    }
    # Freecam loads its settings and fonts from <DLL folder>/Freecam.
    # Preserve those exact paths when MO2 moves its native DLL into DLLs.
    _dll_companion_directories = {"freecammod": "Freecam"}
    _supported_extensions = {
        ".acb",
        ".anibnd",
        ".bak",
        ".bin",
        ".bnk",
        ".bnd",
        ".chrbnd",
        ".cfg",
        ".dcx",
        ".dsaproj",
        ".dsasbak",
        ".dll",
        ".dds",
        ".emevd",
        ".fdbnd",
        ".flac",
        ".flver",
        ".fmg",
        ".ffxbnd",
        ".gfx",
        ".hkx",
        ".hks",
        ".ini",
        ".json",
        ".matbin",
        ".mp3",
        ".mtd",
        ".msgbnd",
        ".ogg",
        ".lua",
        ".otf",
        ".png",
        ".prev",
        ".partsbnd",
        ".tga",
        ".toml",
        ".tpf",
        ".tpfbdt",
        ".tpfbhd",
        ".wav",
        ".wem",
        ".xml",
        ".yaml",
        ".yml",
    }
    _metadata_patterns = [
        "<game>",
        "Game",
        "ELDEN RING",
        "ELDENRING",
    ]

    def __init__(
        self,
        organizer: mobase.IOrganizer,
        plugin_name: str,
        preserve_extensions: tuple[str, ...] = (),
        watch_mod_list: bool = False,
    ):
        self._organizer = organizer
        self._plugin_name = plugin_name
        # the install window's summary runs the layout on a copy: nothing to log
        self.previewing = False
        self._preserve_extensions = frozenset(
            extension.casefold() for extension in preserve_extensions
        )
        self._saved_archive_layout_signatures: set[tuple[str, ...]] | None = None
        self._saved_archive_layout_roots: set[frozenset[str]] = set()
        self._saved_archive_layout_cache_epoch = None
        # only the game plugin's own checker follows the mod list: MO2 never
        # drops these callbacks, so the short-lived checker the archive
        # installer makes for each install must not add more of them
        if watch_mod_list:
            try:
                mod_list = organizer.modList()
                mod_list.onModInstalled(self._invalidate_saved_archive_layout_cache)
                mod_list.onModRemoved(self._invalidate_saved_archive_layout_cache)
            except Exception:
                # Validation still works without the optional cache notifications.
                pass
        super().__init__(
            GlobPatterns(
                unfold=self._metadata_patterns,
                valid=[
                    *sorted(self._content_directories),
                    *sorted(
                        f"*{extension}" for extension in self._supported_extensions
                    ),
                    "load.txt",
                    "seamlesscoop",
                    self.ReforgedOnlineDirectory,
                ],
                delete=[
                    "*.md",
                    "*.nfo",
                    "*.pdf",
                    "*.url",
                    "CHANGELOG*",
                    "CREDITS*",
                    "LICENSE*",
                    "README*",
                ],
            )
        )

    def dataLooksValid(
        self, filetree: mobase.IFileTree
    ) -> mobase.ModDataChecker.CheckReturn:
        # The archive installer sets this short-lived flag only after the user
        # chooses to keep a package exactly as it appears in the archive. MO2
        # may call the checker with a nested tree during installation, so do
        # not require this tree to have no parent. Validate that it actually
        # contains Elden Ring payload before accepting it.
        if self._preserve_archive_layout_requested():
            return (
                self.VALID
                if self._contains_supported_payload(filetree)
                else self.INVALID
            )

        # MO2 evaluates installed mod trees again after the installer has
        # cleared its temporary choice. Match either saved layout to the
        # installed tree so both supported choices count as valid game data.
        if self._is_saved_archive_layout_tree(filetree):
            return self.VALID

        # Common archive wrappers are normalized during install. The target
        # layout uses the short ``DLLs`` folder and does not expose wrappers.
        if self._has_wrapped_payload(filetree):
            return self.FIXABLE

        # Native DLLs inside archive subfolders are organized below DLLs.
        # Files already at the archive root stay there and are loaded through
        # the game's forced-load list, regardless of their DLL basename.
        if self._has_misplaced_dll(filetree):
            return self.FIXABLE

        status = super().dataLooksValid(filetree)
        if status is self.VALID:
            return status
        # game folders with loose files beside them (notes kept by the install
        # window's choice, a README, Seamless Co-op's ersc_launcher.exe): the
        # game never reads those files, and MO2 flags an installed mod that
        # isn't VALID as having no game data
        if self._valid_apart_from_loose_files(filetree):
            return self.VALID
        if status is not self.INVALID:
            return status

        # Handle a wrapper folder containing the mod payload and its files.
        if any(
            is_directory(entry) and self._is_package_wrapper(entry)
            for entry in filetree
        ):
            return self.FIXABLE

        # A recognized game payload nested in one or more archive wrappers can
        # be repaired; archives without a known payload remain invalid.
        if self._contains_supported_payload(filetree):
            return self.FIXABLE
        return status

    def _valid_apart_from_loose_files(self, filetree: mobase.IFileTree) -> bool:
        """Whether every folder at the top is a game folder MO2 takes as it is,
        with at least one of them or a game file there, and the rest only loose
        files."""
        patterns = self._regex_patterns
        found = False
        for entry in filetree:
            name = entry.name().casefold()
            if patterns.valid.match(name):
                # game data: a game folder, a DLL or regulation.bin, not a
                # loose picture or settings file that also matches a pattern
                if is_directory(entry) or name.endswith(".dll") or name == "regulation.bin":
                    found = True
            elif is_directory(entry) or patterns.unfold.match(name):
                # a wrapper or an option folder: left to the checks below
                return False
        return found

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        if self._preserve_archive_layout_requested():
            return filetree
        return self.normalize_standard_layout(filetree)

    def normalize_standard_layout(
        self,
        filetree: mobase.IFileTree,
        recognize_mod_engine_paths: bool = True,
        keep_reforged_online: bool = False,
    ) -> mobase.IFileTree:
        """Apply the standard DLL/assets layout directly to an archive tree."""
        # ELDEN RING Reforged first: the release it keeps for its own launcher
        # must stay as it ships, notes and licences included
        self._reshape_reforged_layout(filetree, keep_reforged_online)
        self._remove_loader_launchers(filetree)
        self._remove_shipped_logs(filetree)
        self.optional_files_left_out = self._remove_optional_folders(filetree)
        # Apply MO2's standard root patterns before peeling the outer archive
        # folders. The wrapper scan itself stays at this game-root boundary.
        self._fix_known_layouts(filetree)
        self._unwrap_package_folders(filetree)
        # Mod Engine 2 packages place the actual game payload under ``mod/``.
        # Lift that recognized payload before moving DLLs so its assets retain
        # their game-root paths (for example, ``mod/menu`` becomes ``menu``).
        if recognize_mod_engine_paths:
            self._unwrap_mod_engine_paths(filetree)

        # Older archives use these as package containers rather than game
        # asset paths. Peel their inner package folders, route native files,
        # then flatten the remaining assets only when paths do not collide.
        legacy_folders = (
            "native",
            "natives",
            *(("mod",) if recognize_mod_engine_paths else ()),
            *sorted(self._legacy_dll_directories),
        )
        for folder_name in legacy_folders:
            folder = self._direct_directory(filetree, folder_name)
            if folder is not None:
                self._unwrap_package_folders(folder)

        self._move_standard_dlls_and_ini(filetree)
        for folder_name in legacy_folders:
            folder = self._direct_directory(filetree, folder_name)
            if folder is None:
                continue
            if len(folder) == 0:
                folder.detach()
            elif self._package_folders_merge_without_conflicts(
                filetree, [folder]
            ):
                self._merge_package_folder(filetree, folder)
        self._unwrap_package_folders(filetree, allow_safe_multiple=True)
        self._remove_preview_images(filetree)
        return filetree

    def normalize_preserved_layout(
        self,
        filetree: mobase.IFileTree,
    ) -> mobase.IFileTree:
        """Map install-source wrappers and preserve every payload-relative path.

        FRZN Merge calls its game-root payload ``Drag Files Into Here``. That
        folder is an install container, not an in-game path component: lift its
        contents to MO2's virtual game root while leaving their internal paths
        intact. Native roots such as ``natives/`` remain in the archive layout.

        Only folders that cannot be part of the layout are lifted: the
        archive's own top folder, ``Game``/``ELDEN RING`` folders and the
        FRZN staging folder. A level holding game files, a launcher or its
        ``.toml`` is the payload root (Seamless Co-op keeps ``SeamlessCoop/``
        beside ``ersc_launcher.exe``, Mod Engine 2 keeps ``modengine2/bin``).
        """
        game_folders = {
            "<game>", "game", "elden ring", "elden-ring", "eldenring", "elden_ring",
            "nightreign", "elden ring nightreign", "elden-ring-nightreign",
            "eldenring nightreign", "elden_ring_nightreign",
        }
        for _ in range(self._archive_wrapper_depth_limit):
            entries = [
                entry
                for entry in filetree
                if not self._regex_patterns.delete.match(entry.name().casefold())
            ]
            # screenshots or notes beside the top folder do not count
            if any(
                not is_directory(entry)
                and Path(entry.name()).suffix.casefold()
                in {".bdt", ".bhd", ".bin", ".dcx", ".dll", ".exe", ".ini", ".toml"}
                for entry in entries
            ):
                return filetree
            folders = [
                entry
                for entry in entries
                if is_directory(entry) and self._contains_supported_payload(entry)
            ]
            # FRZN's staging folder goes to the root even beside its natives/
            staging = [
                folder for folder in folders if folder.name().casefold() == "drag files into here"
            ]
            if staging:
                folders = staging
            if len(folders) != 1:
                return filetree
            folder = folders[0]
            name = folder.name().casefold()
            if name == "drag files into here" or name in game_folders:
                pass
            elif name in self._content_directories or name in {"seamlesscoop", "modengine2"}:
                return filetree
            if not self._package_folders_merge_without_conflicts(filetree, [folder]):
                return filetree
            if not self._merge_package_folder(filetree, folder):
                return filetree
        return filetree

    # ELDEN RING Reforged ships its own me3 and launcher: its game files are
    # under mod/ and its natives under internals/modengine/dll. MO2 serves the
    # files and loads the natives itself, and hands reforged.dll the attach
    # config me3 would give it (see _game_class()._reforged_attach_config).
    _reforged_native_path = "internals/modengine/dll"
    _reforged_dll = "reforged.dll"
    # what the launcher, me3 and the UI setup tool (Gideon) need, not the game
    _reforged_tool_paths = ("mod/menu/deploy",)
    # the release kept for Reforged's own launcher (its online mode); its DLLs
    # (me3's among them) are never MO2's natives
    ReforgedOnlineDirectory = "_ReforgedOnline"
    ReforgedLauncherPath = "internals/launcher/ReforgedLauncher.exe"

    @classmethod
    def known_package(cls, filetree: mobase.IFileTree, depth: int = 0) -> str | None:
        """Which mod with its own install needs this archive is, if any."""
        if depth == 0 and cls.reforged_root(filetree) is not None:
            return "reforged"
        me3 = cls._direct_directory(filetree, "me3")
        if me3 is not None and any(
            entry.name().casefold() == "convergence.me3" for entry in me3
        ):
            return "convergence"
        seamless = cls._direct_directory(filetree, "seamlesscoop")
        if seamless is not None and any(
            entry.name().casefold() in {"ersc.dll", "nrsc.dll"} for entry in seamless
        ):
            return "seamless"
        if cls._direct_directory(filetree, "drag files into here") is not None:
            return "frzn"
        if depth >= 2:
            return None
        for entry in filetree:
            if is_directory(entry):
                found = cls.known_package(entry, depth + 1)
                if found is not None:
                    return found
        return None

    @classmethod
    def _is_me3_folder(cls, folder: mobase.IFileTree, depth: int = 0) -> bool:
        for entry in folder:
            if is_directory(entry):
                if depth < 3 and cls._is_me3_folder(entry, depth + 1):
                    return True
            elif entry.name().casefold() == "me3.exe" or entry.name().casefold().endswith(".me3"):
                return True
        return False

    def _remove_loader_launchers(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        """A package made for me3 or Mod Engine 2 brings that loader and the
        script that starts it; MO2 loads the package's files and natives itself."""
        loader_files = [
            entry
            for entry in filetree
            if not is_directory(entry)
            and (
                entry.name().casefold().endswith(".me3")
                or entry.name().casefold() in {"me3.exe", "modengine2_launcher.exe"}
                or re.fullmatch(r"launchmod_.*\.(bat|cmd)", entry.name().casefold())
            )
        ]
        me3 = self._direct_directory(filetree, "me3")
        me3 = me3 if me3 is not None and self._is_me3_folder(me3) else None
        modengine = self._direct_directory(filetree, "modengine2")
        if loader_files or me3 is not None or modengine is not None:
            for folder in (me3, modengine):
                if folder is not None:
                    folder.detach()
            for entry in list(filetree):
                name = entry.name().casefold()
                if not is_directory(entry) and (
                    Path(name).suffix in {".bat", ".cmd", ".sh", ".lnk", ".me3"}
                    or name in {"me3.exe", "modengine2_launcher.exe"}
                    or re.fullmatch(r"config_[a-z0-9_]+\.toml", name)
                ):
                    entry.detach()
            return
        if depth < 2:
            for entry in list(filetree):
                if is_directory(entry) and entry.name() != self.ReforgedOnlineDirectory:
                    self._remove_loader_launchers(entry, depth + 1)

    _optional_folder_names = re.compile(
        r"(^|[\s_\-.])optionals?($|[\s_\-.])|^(extras?|add-?ons?)$", re.IGNORECASE
    )

    def _remove_optional_folders(self, filetree: mobase.IFileTree, depth: int = 0) -> int:
        """An archive's optional extras aren't the mod: left out, and counted so
        the window can say so. A folder that is all the payload there is stays."""
        removed = 0
        for entry in list(filetree):
            if (
                is_directory(entry)
                and entry.name() != self.ReforgedOnlineDirectory
                and self._optional_folder_names.search(entry.name())
                and any(
                    other is not entry
                    and other.name() != entry.name()
                    and (
                        self._contains_supported_payload(other)
                        if is_directory(other)
                        else Path(other.name()).suffix.casefold() in self._supported_extensions
                    )
                    for other in filetree
                )
            ):
                removed += self._count_files(entry)
                entry.detach()
        if depth < 2:
            for entry in list(filetree):
                if is_directory(entry) and entry.name() != self.ReforgedOnlineDirectory:
                    removed += self._remove_optional_folders(entry, depth + 1)
        return removed

    @classmethod
    def _count_files(cls, tree: mobase.IFileTree) -> int:
        return sum(cls._count_files(entry) if is_directory(entry) else 1 for entry in tree)

    _preview_image_suffixes = {".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"}

    def _remove_preview_images(self, filetree: mobase.IFileTree) -> None:
        """Screenshots beside the mod would end up in the game's folder. Only at
        the root: a DLL's own icons stay with it in DLLs."""
        for entry in list(filetree):
            if is_directory(entry):
                if entry.name().casefold() in {
                    "images", "image", "screenshots", "screenshot", "previews", "preview", "pictures",
                } and all(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() in self._preview_image_suffixes
                    for child in entry
                ):
                    entry.detach()
            elif Path(entry.name()).suffix.casefold() in self._preview_image_suffixes:
                entry.detach()

    def _dll_companion_folders(self, tree: mobase.IFileTree) -> set[str]:
        """Folders beside a DLL holding only its data (settings, icons, text):
        they stay with it, whole, instead of being taken for package wrappers."""
        if not any(
            not is_directory(entry) and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
            for entry in tree
        ):
            return set()
        names = set()
        for entry in tree:
            name = entry.name().casefold()
            if (
                is_directory(entry)
                and len(entry) > 0
                and name not in self._content_directories
                and name not in self._package_directories
                and name not in {"seamlesscoop", "logs", self.ReforgedOnlineDirectory.casefold()}
                and not self._has_game_payload(entry)
            ):
                names.add(name)
        return names

    @classmethod
    def _has_game_payload(cls, tree: mobase.IFileTree, depth: int = 0) -> bool:
        if depth > cls._archive_wrapper_depth_limit:
            return True
        for entry in tree:
            if is_directory(entry):
                if entry.name().casefold() in cls._asset_directories or cls._has_game_payload(
                    entry, depth + 1
                ):
                    return True
            elif Path(entry.name()).suffix.casefold() in {".dll", ".exe", ".dcx", ".bin", ".gfx", ".bdt", ".bhd"}:
                return True
        return False

    def _remove_shipped_logs(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        """A logs folder that came with the archive is its author's, not the mod."""
        if depth > self._archive_wrapper_depth_limit:
            return
        for entry in list(filetree):
            if not is_directory(entry) or entry.name() == self.ReforgedOnlineDirectory:
                continue
            if entry.name().casefold() == "logs" and len(entry) > 0 and all(
                not is_directory(child) and child.name().casefold().endswith(".log")
                for child in entry
            ):
                entry.detach()
            else:
                self._remove_shipped_logs(entry, depth + 1)

    @classmethod
    def reforged_root(
        cls, filetree: mobase.IFileTree, depth: int = 0
    ) -> mobase.IFileTree | None:
        """The folder holding an ELDEN RING Reforged release, if any."""
        natives = cls._directory_at_path(filetree, cls._reforged_native_path)
        if natives is not None and any(
            not is_directory(entry) and entry.name().casefold() == cls._reforged_dll
            for entry in natives
        ):
            return filetree
        if depth >= 2:
            return None
        folders = [entry for entry in filetree if is_directory(entry)]
        for folder in folders:
            found = cls.reforged_root(folder, depth + 1)
            if found is not None:
                return found
        return None

    def _reshape_reforged_layout(
        self, filetree: mobase.IFileTree, keep_online: bool = False
    ) -> bool:
        """Lay out ELDEN RING Reforged as MO2 runs it: mod/ at the game root,
        its natives in DLLs, and nothing of its launcher or me3.

        With ``keep_online`` the rest of the release (launcher, me3, its tools
        and notes) moves whole into ``_ReforgedOnline``; the archive installer
        then hard-links the game files and natives back to their release paths
        there, so Reforged's own launcher finds the release it checks for its
        online mode without a second copy on disk. ``self.reforged_layout``
        records what goes where.
        """
        self.reforged_layout = None
        root = self.reforged_root(filetree)
        if root is None:
            return False
        online = self.ReforgedOnlineDirectory
        natives = self._directory_at_path(root, self._reforged_native_path)
        mod_folder = self._direct_directory(root, "mod")
        for path in self._reforged_tool_paths:
            tool = self._directory_at_path(root, path)
            if tool is None:
                continue
            if keep_online:
                filetree.move(tool, f"{online}/{Path(path).parent.as_posix()}/")
            else:
                tool.detach()
        native_names = []
        for entry in list(natives):
            if not is_directory(entry) and Path(entry.name()).suffix.casefold() in {
                ".dll",
                ".ini",
            }:
                native_names.append(entry.name())
                filetree.move(entry, f"{self._dll_directory}/")
        mod_entries = []
        if mod_folder is not None:
            mod_entries = [entry.name() for entry in mod_folder]
            if mod_folder.detach():
                filetree.merge(mod_folder)
        if keep_online:
            # everything else of the release, as it ships
            release = [entry for entry in root] if root is not filetree else [
                entry
                for entry in filetree
                if entry.name() not in mod_entries
                and entry.name().casefold() != self._dll_directory.casefold()
                and entry.name() != online
            ]
            for entry in release:
                filetree.move(entry, f"{online}/")
            if root is not filetree:
                root.detach()
            self.reforged_layout = {
                "online": online,
                "mod_entries": mod_entries,
                "natives": native_names,
            }
            if not self.previewing:
                qInfo(
                    "Elden Ring MO2: laid out ELDEN RING Reforged for MO2 and kept its "
                    f"release for its online mode in {online}."
                )
            return True
        # a release at the archive root keeps its notes (README, CHANGELOG)
        leftovers = (
            [root]
            if root is not filetree
            else [
                entry
                for entry in filetree
                if (
                    is_directory(entry)
                    and entry.name().casefold()
                    in {"internals", "dll", "addons", "extras", "troubleshooting"}
                )
                or (
                    not is_directory(entry)
                    and Path(entry.name()).suffix.casefold() in {".bat", ".sh", ".lnk"}
                )
            ]
        )
        for entry in leftovers:
            entry.detach()
        if not self.previewing:
            qInfo(
                "Elden Ring MO2: laid out ELDEN RING Reforged for MO2: its game files "
                "at the game root and its natives in DLLs; its launcher and me3 are "
                "not installed."
            )
        return True

    def _unwrap_mod_engine_paths(self, filetree: mobase.IFileTree) -> None:
        """Map a recognized Mod Engine ``mod/`` payload to the game root."""
        mod_folder = self._direct_directory(filetree, "mod")
        if mod_folder is None:
            return

        # Archives may add a named package directory inside ``mod`` too.
        self._unwrap_package_folders(mod_folder, allow_safe_multiple=True)
        recognized_directories = (
            self._asset_directories
            | self._legacy_dll_directories
            | {"config", "configs", "dlls", "fonts", "mod", "native", "natives", "plugins"}
        )
        has_known_route = any(
            is_directory(entry)
            and entry.name().casefold() in recognized_directories
            for entry in mod_folder
        )
        has_root_payload = any(
            not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._supported_extensions
            for entry in mod_folder
        )
        if not (has_known_route or has_root_payload):
            return

        # Do not flatten when the archive already contains a conflicting path.
        # This preserves both copies instead of replacing one silently.
        if not self._package_folders_merge_without_conflicts(
            filetree, [mod_folder]
        ):
            return

        if self._merge_package_folder(filetree, mod_folder):
            # an archive's top folder can itself be called mod: mod/mod/parts
            self._unwrap_mod_engine_paths(filetree)

    @staticmethod
    def _merge_package_folder(
        destination: mobase.IFileTree, folder: mobase.IFileTree
    ) -> bool:
        """Flatten a preflighted wrapper even if detach is unavailable."""
        if folder.detach():
            destination.merge(folder)
            return True

        # Some archive-backed trees reject detach. Merge only when this cannot
        # recurse into the still-attached wrapper with a same-named child.
        # The caller's path-conflict preflight prevents replacement of payload.
        if any(
            entry.name().casefold() == folder.name().casefold()
            for entry in folder
        ):
            return False
        destination.merge(folder)
        if len(folder) == 0:
            folder.detach()
        return True

    def _unwrap_package_folders(
        self,
        filetree: mobase.IFileTree,
        allow_safe_multiple: bool = False,
    ) -> None:
        """Peel redundant wrappers at this tree boundary, not inside game assets."""
        for _ in range(self._archive_wrapper_depth_limit):
            companions = self._dll_companion_folders(filetree)
            candidates = [
                entry
                for entry in filetree
                if is_directory(entry)
                and entry.name().casefold() not in companions
                and self._is_package_wrapper(entry)
            ]
            # Several top-level folders can each contain valid game files.
            # Preserve them unless standard-layout processing has already
            # routed DLLs and a path scan proves their remaining assets do not
            # collide when merged.
            if len(candidates) > 1 and not allow_safe_multiple:
                return
            if not self._package_folders_merge_without_conflicts(filetree, candidates):
                return
            if not candidates:
                return

            for entry in candidates:
                if not self._merge_package_folder(filetree, entry):
                    return

    @staticmethod
    def _package_folders_merge_without_conflicts(
        destination: mobase.IFileTree,
        folders: list[mobase.IFileTree],
    ) -> bool:
        """Allow shared directories but reject conflicts with existing paths."""
        paths: dict[str, bool] = {}

        def add_entry(entry: mobase.IFileTree, prefix: str = "") -> bool:
            path = f"{prefix}/{entry.name()}" if prefix else entry.name()
            is_dir = is_directory(entry)
            key = path.casefold()
            previous_is_dir = paths.get(key)
            if previous_is_dir is not None and (
                not previous_is_dir or not is_dir
            ):
                return False
            paths[key] = is_dir
            if is_dir:
                return all(add_entry(child, path) for child in entry)
            return True

        folder_names = {folder.name().casefold() for folder in folders}
        if not all(
            add_entry(entry)
            for entry in destination
            if entry.name().casefold() not in folder_names
        ):
            return False
        return all(
            add_entry(entry)
            for folder in folders
            for entry in folder
        )

    def _move_standard_dlls_and_ini(self, filetree: mobase.IFileTree) -> None:
        """Place native DLLs and their companion files under Game/DLLs safely."""
        records: list[tuple[mobase.IFileTree, str]] = []
        font_directories: list[tuple[mobase.IFileTree, str]] = []
        asset_directories: list[tuple[mobase.IFileTree, str]] = []
        companion_directories: list[tuple[mobase.IFileTree, str]] = []

        def collect(tree: mobase.IFileTree, parent_path: str = "") -> None:
            companions = self._dll_companion_folders(tree)
            for entry in tree:
                relative_path = (
                    f"{parent_path}/{entry.name()}" if parent_path else entry.name()
                )
                if is_directory(entry):
                    # Seamless Co-op reads its settings, locale and crash
                    # reporter beside its DLL: its folder stays whole, as
                    # does the Reforged release kept for its own launcher
                    if entry.name().casefold() in {
                        "seamlesscoop",
                        self.ReforgedOnlineDirectory.casefold(),
                    }:
                        continue
                    if entry.name().casefold() in companions:
                        companion_directories.append((entry, parent_path))
                        continue
                    if entry.name().casefold() == "fonts":
                        font_directories.append((entry, parent_path))
                    if entry.name().casefold() == "assets":
                        asset_directories.append((entry, parent_path))
                    collect(entry, relative_path)
                else:
                    records.append((entry, parent_path))

        collect(filetree)
        dlls = [
            (entry, parent_path)
            for entry, parent_path in records
            if Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
        ]
        root_loader_stems = {
            Path(entry.name()).stem.casefold()
            for entry, parent_path in records
            if not parent_path
            and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() in self._root_loader_dlls
        }
        dll_groups: dict[str, list[tuple[mobase.IFileTree, str]]] = {}
        for entry, parent_path in dlls:
            dll_groups.setdefault(entry.name().casefold(), []).append(
                (entry, parent_path)
            )

        # two DLL folders with a config.ini each, or an assets folder each, can't
        # share DLLs/: those DLLs keep a folder of their own there
        dll_parents = {parent_path.casefold() for _entry, parent_path in dlls}
        sidecar_parents: dict[str, set[str]] = {}
        for entry, parent_path in records:
            if (
                parent_path.casefold() in dll_parents
                and Path(entry.name()).suffix.casefold() == ".ini"
            ):
                sidecar_parents.setdefault(entry.name().casefold(), set()).add(parent_path.casefold())
        for folder, parent_path in asset_directories:
            if parent_path.casefold() in dll_parents:
                sidecar_parents.setdefault(f"{folder.name().casefold()}/", set()).add(parent_path.casefold())
        shared_sidecar_parents = {
            parent
            for parents in sidecar_parents.values()
            if len(parents) > 1
            for parent in parents
            if parent and parent != self._dll_directory.casefold()
        }

        target_directories: dict[int, str] = {}
        for group in dll_groups.values():
            if len(group) == 1:
                entry, parent_path = group[0]
                if parent_path.casefold() in shared_sidecar_parents:
                    target_directories[id(entry)] = f"{self._dll_directory}/{parent_path}"
                    continue
                target_directories[id(entry)] = (
                    parent_path
                    if parent_path.casefold() == self._dll_directory.casefold()
                    else self._dll_directory
                )
                continue

            # Keep one already-direct DLL in place, then retain source folders
            # for same-named copies so none is silently overwritten.
            direct = next(
                (
                    (entry, parent_path)
                    for entry, parent_path in group
                    if parent_path.casefold() == self._dll_directory.casefold()
                ),
                None,
            )
            used_targets: set[str] = set()
            for index, (entry, parent_path) in enumerate(group, start=1):
                if direct is not None and entry is direct[0]:
                    destination = self._dll_directory
                elif parent_path.casefold().startswith(
                    self._dll_directory.casefold() + "/"
                ):
                    destination = parent_path
                elif parent_path:
                    destination = f"{self._dll_directory}/{parent_path}"
                else:
                    destination = f"{self._dll_directory}/_archive_conflicts/{index}"

                target_key = f"{destination}/{entry.name()}".casefold()
                while target_key in used_targets:
                    destination = f"{destination}/_duplicate_{index}"
                    target_key = f"{destination}/{entry.name()}".casefold()
                used_targets.add(target_key)
                target_directories[id(entry)] = destination

        for entry, parent_path in dlls:
            destination = target_directories[id(entry)]
            if parent_path.casefold() == destination.casefold():
                continue
            if not filetree.move(entry, f"{destination}/"):
                raise RuntimeError(
                    _tr("Could not move {0} into Game/{1}.").format(entry.name(), destination)
                )

        # a DLL's own data folders go with it, whole
        for folder, parent_path in companion_directories:
            destinations = {
                target_directories[id(dll)].casefold(): target_directories[id(dll)]
                for dll, dll_parent in dlls
                if dll_parent.casefold() == parent_path.casefold()
            }
            if len(destinations) != 1:
                continue
            destination = next(iter(destinations.values()))
            target = self._directory_at_path(filetree, destination)
            if parent_path.casefold() != destination.casefold() and (
                target is None or self._direct_directory(target, folder.name()) is None
            ):
                filetree.move(folder, f"{destination}/")

        # Keep sidecar assets beside native DLLs that came from a Revamped
        # ``native(s)/<mod>/`` tree. In particular, Nightreign Movement looks
        # for ``assets`` beside its DLL. MO2 layout moves that DLL under DLLs;
        # leaving assets in the original container would silently break it.
        for asset_directory, parent_path in asset_directories:
            container = parent_path.split("/", 1)[0].casefold()
            if container not in {"native", "natives"}:
                continue
            sibling_dlls = [
                dll
                for dll, dll_parent in dlls
                if dll_parent.casefold() == parent_path.casefold()
            ]
            destinations = {
                target_directories[id(dll)].casefold(): target_directories[id(dll)]
                for dll in sibling_dlls
            }
            if len(destinations) != 1:
                continue
            destination = next(iter(destinations.values()))
            target_parent = self._directory_at_path(filetree, destination)
            if target_parent is None:
                raise RuntimeError(
                    _tr("Could not preserve {0} beside its native DLL.").format(asset_directory.name())
                )
            existing_assets = next(
                (
                    entry
                    for entry in target_parent
                    if is_directory(entry)
                    and entry.name().casefold() == asset_directory.name().casefold()
                ),
                None,
            )
            if existing_assets is not None:
                if len(existing_assets) == 0:
                    existing_assets.detach()
                else:
                    raise RuntimeError(
                        _tr("MO2 layout found a conflicting assets directory beside "
                        "a native DLL. Choose Install as it comes in the archive to "
                        "preserve both folders without merging them.")
                    )
            if not filetree.move(asset_directory, f"{destination}/"):
                raise RuntimeError(
                    _tr("Could not move {0} beside its native DLL.").format(asset_directory.name())
                )

        # INI files are native-mod configuration files in the standard layout.
        # Keep a root loader's matching INI beside that proxy DLL; MO2 forces
        # those Windows loader proxies from the game root before managed DLLs.
        sidecar_records = [
            (entry, parent_path)
            for entry, parent_path in records
            if Path(entry.name()).suffix.casefold() in self._sidecar_extensions
        ]
        for entry, parent_path in sidecar_records:
            suffix = Path(entry.name()).suffix.casefold()
            stem = Path(entry.name()).stem.casefold()
            if not parent_path and (
                stem in root_loader_stems
                or self._is_dll_sidecar(
                    stem,
                    root_loader_stems,
                    {"config", "settings", "configuration", "options"},
                )
                or (stem in {"config", "configuration", "options", "settings"}
                    and len(root_loader_stems) == 1)
                or (stem == "mod_loader_config" and root_loader_stems)
            ):
                continue

            if suffix == ".ini":
                candidate_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if dll_parent.casefold() == parent_path.casefold()
                ]
                matched = [
                    (dll, dll_parent)
                    for dll, dll_parent in candidate_dlls
                    if Path(dll.name()).stem.casefold() == stem
                ]
                if not matched and stem in {
                    "config",
                    "configuration",
                    "options",
                    "settings",
                } and len(candidate_dlls) == 1:
                    matched = candidate_dlls
                if not matched and not parent_path and len(dlls) == 1:
                    matched = dlls

                if len(matched) == 1:
                    destination = self._dll_resource_directory(
                        matched[0][0], target_directories[id(matched[0][0])]
                    )
                elif not candidate_dlls:
                    # no DLL beside it moves (a ReShade preset, a loader's
                    # settings): it stays where the archive puts it
                    continue
                else:
                    destination = self._sidecar_destination(
                        entry, parent_path, records
                    )
            else:
                matching_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if dll_parent.casefold() == parent_path.casefold()
                    and self._is_dll_sidecar(
                        stem,
                        {Path(dll.name()).stem.casefold()},
                        {"config", "settings", "configuration", "options"},
                    )
                ]
                if len(matching_dlls) != 1:
                    continue
                destination = self._dll_resource_directory(
                    matching_dlls[0][0],
                    target_directories[id(matching_dlls[0][0])],
                )

            if parent_path.casefold() != destination.casefold():
                if not filetree.move(entry, f"{destination}/"):
                    raise RuntimeError(
                        _tr("Could not move {0} into Game/{1}.").format(entry.name(), destination)
                    )

        # Some native mods load fonts relative to the DLL's own directory.
        # Keep the conventional fonts folder beside the DLL after moving it
        # into MO2's DLLs folder. Do this only for a single, unambiguous font
        # folder so mixed archives cannot silently combine unrelated assets.
        if len(font_directories) == 1 and dlls:
            font_directory, font_parent = font_directories[0]
            sibling_dlls = [
                (dll, dll_parent)
                for dll, dll_parent in dlls
                if dll_parent.casefold() == font_parent.casefold()
            ]
            matching_dlls = sibling_dlls
            if not matching_dlls and font_parent.casefold() in {"", "dlls"}:
                matching_dlls = dlls
            if not matching_dlls:
                matching_dlls = [
                    (dll, dll_parent)
                    for dll, dll_parent in dlls
                    if self._dll_companion_directory_name(dll)
                    and font_parent.casefold()
                    == self._dll_companion_directory_name(dll).casefold()
                ]

            target_options = {
                self._dll_resource_directory(
                    dll, target_directories[id(dll)]
                ).casefold(): self._dll_resource_directory(
                    dll, target_directories[id(dll)]
                )
                for dll, _ in matching_dlls
            }
            if len(target_options) == 1:
                target_directory = next(iter(target_options.values()))
                source_font_path = (
                    f"{font_parent}/fonts" if font_parent else "fonts"
                )
                target_font_path = f"{target_directory}/fonts"
                target_parent = self._directory_at_path(filetree, target_directory)
                target_font_entry = (
                    next(
                        (
                            entry
                            for entry in target_parent
                            if entry.name().casefold() == "fonts"
                        ),
                        None,
                    )
                    if target_parent is not None
                    else None
                )
                if (
                    source_font_path.casefold() != target_font_path.casefold()
                    and target_font_entry is None
                    and not filetree.move(font_directory, f"{target_directory}/")
                ):
                    raise RuntimeError(
                        _tr("Could not move the mod fonts directory beside its DLL.")
                    )

        self._remove_empty_directories(filetree)

    @classmethod
    def _remove_empty_directories(cls, filetree: mobase.IFileTree) -> None:
        for entry in list(filetree):
            if is_directory(entry):
                cls._remove_empty_directories(entry)
                if len(entry) == 0:
                    entry.detach()

    def _sidecar_destination(
        self,
        entry: mobase.IFileTree,
        parent_path: str,
        records: list[tuple[mobase.IFileTree, str]],
    ) -> str:
        """Keep duplicate INI names in their original subfolders under DLLs."""
        count = sum(
            1
            for candidate, _ in records
            if Path(candidate.name()).suffix.casefold() == ".ini"
            and candidate.name().casefold() == entry.name().casefold()
        )
        if count > 1 and parent_path:
            if parent_path.casefold() == self._dll_directory.casefold():
                return self._dll_directory
            if parent_path.casefold().startswith(self._dll_directory.casefold() + "/"):
                return parent_path
            return f"{self._dll_directory}/{parent_path}"
        if count > 1:
            return f"{self._dll_directory}/_archive_conflicts/root"
        return self._dll_directory

    def _dll_companion_directory_name(
        self, dll: mobase.IFileTree
    ) -> str | None:
        return self._dll_companion_directories.get(
            Path(dll.name()).stem.casefold()
        )

    def _dll_resource_directory(
        self, dll: mobase.IFileTree, target_directory: str
    ) -> str:
        companion_directory = self._dll_companion_directory_name(dll)
        if companion_directory is None:
            return target_directory
        return f"{target_directory}/{companion_directory}"

    def _preserve_archive_layout_requested(self) -> bool:
        return bool(
            self._organizer.pluginSetting(
                self._plugin_name,
                _game_class().ArchiveLayoutOverrideSetting,
            )
        )

    def _is_saved_archive_layout_tree(self, filetree: mobase.IFileTree) -> bool:
        # MO2 asks for every mod at startup and on refreshes, on its GUI
        # thread. Most setups keep no archive layout, so answer those without
        # walking the tree; otherwise compare the root names before the full
        # path signature. Both shortcuts only skip trees that cannot match.
        signatures, roots = self._saved_archive_layout_signature_set()
        if not signatures:
            return False
        if not self._root_names_may_match(filetree, roots):
            return False
        target_signature = self._file_tree_signature(filetree)
        return bool(target_signature) and target_signature in signatures

    @classmethod
    def _signature_root_names(
        cls, filetree: mobase.IFileTree
    ) -> tuple[set[str], set[str]]:
        """First path parts of the root files a signature counts, and of the
        root folders (which count only when they hold such a file)."""
        files: set[str] = set()
        folders: set[str] = set()
        for entry in filetree:
            name = entry.name().replace("\\", "/")
            key = name.casefold().split("/", 1)[0]
            if is_directory(entry):
                folders.add(key)
            elif cls._signature_counts_file(name):
                files.add(key)
        return files, folders

    @classmethod
    def _root_names_may_match(
        cls, filetree: mobase.IFileTree, roots: set[frozenset[str]]
    ) -> bool:
        files, folders = cls._signature_root_names(filetree)
        present = files | folders
        return any(files <= root and root <= present for root in roots)

    def _saved_archive_layout_signature_set(
        self,
    ) -> tuple[set[tuple[str, ...]], set[frozenset[str]]]:
        """Path signatures of the mods installed with their archive layout
        kept, and the first path parts of each (cached until a mod is
        installed or removed, or the installer bumps the cache epoch)."""
        cache_epoch = self._archive_layout_cache_epoch()
        if (
            self._saved_archive_layout_signatures is not None
            and cache_epoch == self._saved_archive_layout_cache_epoch
        ):
            return (
                self._saved_archive_layout_signatures,
                self._saved_archive_layout_roots,
            )

        self._saved_archive_layout_signatures = None

        try:
            mod_list = self._organizer.modList()
            mod_names = mod_list.allMods()
        except Exception:
            return set(), set()

        signatures: set[tuple[str, ...]] = set()
        complete = True
        for mod_name in mod_names:
            try:
                mod = mod_list.getMod(mod_name)
                if mod is None or mod.isOverwrite():
                    continue
                settings = mod.pluginSettings(
                    _game_class().ArchiveLayoutInstallerName
                )
                if _game_class().PreserveArchiveLayoutSetting not in settings:
                    continue
                signature = self._file_tree_signature(mod.fileTree())
                if signature:
                    signatures.add(signature)
            except Exception:
                # A mod may be changing while MO2 rebuilds its data view. Keep
                # checking other entries and avoid retaining a partial cache.
                complete = False
                continue
        roots = {
            frozenset(path.split("/", 1)[0] for path in signature)
            for signature in signatures
        }
        current_epoch = self._archive_layout_cache_epoch()
        if complete and current_epoch == cache_epoch:
            self._saved_archive_layout_signatures = signatures
            self._saved_archive_layout_roots = roots
            self._saved_archive_layout_cache_epoch = current_epoch
        return signatures, roots

    def _archive_layout_cache_epoch(self):
        try:
            return self._organizer.pluginSetting(
                self._plugin_name,
                _game_class().ArchiveLayoutCacheEpochSetting,
            )
        except Exception:
            return None

    def _invalidate_saved_archive_layout_cache(self, *_args) -> None:
        self._saved_archive_layout_signatures = None

    @staticmethod
    def _name_suffix(name: str) -> str:
        """``Path(name).suffix`` for one entry name, without building a Path
        (this runs for every file of every mod)."""
        if "/" in name or "\\" in name or ":" in name:
            return Path(name).suffix
        index = name.rfind(".")
        return name[index:] if 0 < index < len(name) - 1 else ""

    @classmethod
    def _signature_counts_file(cls, name: str) -> bool:
        return (
            name.casefold() != "meta.ini"
            and cls._name_suffix(name).casefold() != ".mohidden"
        )

    @classmethod
    def _file_tree_signature(cls, filetree: mobase.IFileTree) -> tuple[str, ...]:
        paths: list[str] = []

        def collect(tree: mobase.IFileTree, prefix: str = "") -> None:
            for entry in tree:
                name = entry.name().replace("\\", "/")
                relative_path = f"{prefix}/{name}" if prefix else name
                if is_directory(entry):
                    collect(entry, relative_path)
                elif cls._signature_counts_file(name):
                    paths.append(relative_path.casefold())

        collect(filetree)
        return tuple(sorted(paths))

    def _fix_known_layouts(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        if depth > self._archive_wrapper_depth_limit:
            return
        self._fix_basic_patterns(filetree, depth)
        for entry in list(filetree):
            if is_directory(entry) and not (
                depth == 0 and entry.name() == self.ReforgedOnlineDirectory
            ):
                self._fix_known_layouts(entry, depth + 1)

    def _fix_basic_patterns(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> mobase.IFileTree:
        """Apply MO2's basic patterns while retaining user-selected text files."""
        patterns = self._regex_patterns
        for entry in list(filetree):
            name = entry.name()
            if (
                not is_directory(entry)
                and Path(name).suffix.casefold() in self._preserve_extensions
            ):
                continue

            if patterns.unfold.match(name):
                # only a folder at the top, and never over a file already there:
                # chr/game is a game path, and a file can have that name too
                if (
                    is_directory(entry)
                    and depth == 0
                    and self._package_folders_merge_without_conflicts(filetree, [entry])
                ):
                    # merged before it is detached: a detached folder merges
                    # nothing, and its files were lost
                    filetree.merge(entry)
                    entry.detach()
            elif patterns.valid.match(name):
                continue
            elif patterns.delete.match(name):
                # the patterns mean notes: a folder called "Credits Skip" that
                # holds a mod isn't one
                if not is_directory(entry) or not self._contains_supported_payload(entry):
                    entry.detach()
            elif (move_key := patterns.move_match(name)) is not None:
                filetree.move(entry, self._file_patterns.move[move_key])

        return filetree

    def _has_wrapped_payload(self, filetree: mobase.IFileTree) -> bool:
        for entry in filetree:
            if (
                is_directory(entry)
                and entry.name().casefold() in {"mod", "native", "natives"}
                and self._contains_supported_payload(entry)
            ):
                return True
            if (
                is_directory(entry)
                and entry.name().casefold() in self._legacy_dll_directories
                and any(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() == ".dll"
                    for child in entry
                )
            ):
                return True
        return False

    def _has_misplaced_dll(
        self, filetree: mobase.IFileTree, parent_name: str = "", depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        managed = self._managed_dll_directories
        # the game's own folders (chr, parts, map, sd...) hold its files, often
        # thousands of them, and never a DLL anything loads: MO2 asks this for
        # every mod at each start and refresh, on its interface thread, so
        # they are not walked (a stray DLL there was never loaded either way;
        # installing still moves it into DLLs)
        skipped = managed | self._asset_directories
        # a DLL in this folder is misplaced unless the folder is a managed one
        dll_misplaced_here = bool(parent_name) and parent_name not in managed
        name_suffix = self._name_suffix
        for entry in filetree:
            name = entry.name().casefold()
            if is_directory(entry):
                # Managed DLL directories are already in a valid destination.
                if name in skipped:
                    continue
                if name in {"mod", "native", "natives"} and any(
                    not is_directory(child)
                    and name_suffix(child.name()).casefold() == ".dll"
                    for child in entry
                ):
                    return True
                if self._has_misplaced_dll(entry, name, depth + 1):
                    return True
            elif dll_misplaced_here and name_suffix(name).casefold() == ".dll":
                return True
        return False

    def _is_package_wrapper(self, folder: mobase.IFileTree) -> bool:
        name = folder.name().casefold()
        # FRZN Merge and similar archives use this name for files intended to
        # land at the game root. Preserve every child path, but do not expose
        # the staging folder itself as a virtual game directory.
        if name == "drag files into here":
            return self._contains_supported_payload(folder)

        # Seamless Co-op reads its settings and locale beside its DLL
        if (
            name in self._content_directories
            or name == "seamlesscoop"
            or name == self.ReforgedOnlineDirectory.casefold()
        ):
            return False

        entries = list(folder)
        entries = [
            entry
            for entry in entries
            if not self._regex_patterns.delete.match(entry.name().casefold())
        ]
        if not entries or not self._contains_supported_payload(folder):
            return False

        # A named archive folder containing only known game-root containers is
        # a redundant package wrapper. Also handle a named DLL package with
        # files directly inside it.
        if all(
            is_directory(entry)
            and entry.name().casefold() in self._package_directories
            for entry in entries
        ):
            return True

        # A named folder may contain game-root directories mixed with root
        # files. Treat those known children as the payload boundary so MO2
        # unwraps the name rather than preserving it above game paths.
        if any(
            (
                is_directory(entry)
                and entry.name().casefold() in self._content_directories
            )
            or (
                not is_directory(entry)
                and Path(entry.name()).suffix.casefold() in self._supported_extensions
            )
            for entry in entries
        ):
            return True

        # Archive files sometimes add one or more named folders around the
        # actual payload, alongside notes or other non-game files. Unwrap a
        # folder when exactly one child directory contains supported game
        # content; sibling entries remain in the tree when it is merged.
        payload_directories = [
            entry
            for entry in entries
            if is_directory(entry) and self._contains_supported_payload(entry)
        ]
        if len(payload_directories) == 1:
            return True

        # A single named folder can simply wrap a complete game-root payload.
        # Unwrap it, but keep recognized game directories such as ``parts``
        # and ``menu`` intact via the content-directory guard above.
        if len(entries) == 1 and is_directory(entries[0]):
            return self._contains_supported_payload(entries[0])

        return all(not is_directory(entry) for entry in entries) and any(
            Path(entry.name()).suffix.casefold() == ".dll" for entry in entries
        )

    def _contains_supported_payload(
        self, filetree: mobase.IFileTree, depth: int = 0
    ) -> bool:
        if depth > self._archive_wrapper_depth_limit:
            return False
        for entry in filetree:
            if is_directory(entry):
                if self._contains_supported_payload(entry, depth + 1):
                    return True
            elif Path(entry.name()).suffix.casefold() in self._supported_extensions:
                return True
        return False

    @staticmethod
    def _direct_directory(
        filetree: mobase.IFileTree, name: str
    ) -> mobase.IFileTree | None:
        normalized_name = name.casefold()
        for entry in filetree:
            if is_directory(entry) and entry.name().casefold() == normalized_name:
                return entry
        return None

    @staticmethod
    def _directory_at_path(
        filetree: mobase.IFileTree, path: str
    ) -> mobase.IFileTree | None:
        current = filetree
        for part in (part for part in path.replace("\\", "/").split("/") if part):
            current = EldenRingModDataChecker._direct_directory(current, part)
            if current is None:
                return None
        return current

    @staticmethod
    def _is_dll_sidecar(
        sidecar_stem: str, dll_stems: set[str], config_suffixes: set[str]
    ) -> bool:
        if sidecar_stem in dll_stems:
            return True

        # Some mods use names like ``AdaptiveBarScaling_config.ini`` rather
        # than an exact DLL stem. Only recognized config labels are associated
        # so unrelated game files are not pulled into DLLs.
        for dll_stem in dll_stems:
            for separator in ("_", "-", ".", " "):
                prefix = f"{dll_stem}{separator}"
                if (
                    sidecar_stem.startswith(prefix)
                    and sidecar_stem[len(prefix):] in config_suffixes
                ):
                    return True
        return False
