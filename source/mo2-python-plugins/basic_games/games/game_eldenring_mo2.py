"""Elden Ring game support for Mod Organizer 2 without an external loader."""

from __future__ import annotations

import ctypes
import configparser
import os
from pathlib import Path
import shutil
import stat
import tempfile

import mobase
from PyQt6.QtCore import (
    QEvent,
    QFileInfo,
    QDir,
    QObject,
    QTimer,
    Qt,
    qCritical,
    qInfo,
)
from PyQt6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QBoxLayout,
    QComboBox,
    QMessageBox,
    QSizePolicy,
    QPushButton,
    QToolButton,
)

from ..basic_features import BasicModDataChecker, GlobPatterns
from ..basic_features.utils import is_directory
from ..basic_game import BasicGame


class _RunButtonEnabledMirror(QObject):
    """Mirror MO2's launch-button enabled state onto the visible split button."""

    def __init__(self, source: QAbstractButton, replacement: QToolButton) -> None:
        super().__init__(source)
        self._source = source
        self._replacement = replacement

    def eventFilter(self, watched, event) -> bool:
        if watched == self._source and event.type() == QEvent.Type.EnabledChange:
            self._replacement.setEnabled(self._source.isEnabled())
        return False


class EldenRingModDataChecker(BasicModDataChecker):
    """Recognize Elden Ring asset and native-mod archive layouts.

    MO2 stores game assets below ``Game`` and native plugins under
    ``Game/DLLs``. Flat mixed archives therefore
    keep game resource folders in place while DLLs and their configuration
    sidecars are routed together during installation.
    """

    _dll_directory = "DLLs"
    _archive_wrapper_depth_limit = 16
    _legacy_dll_directories = {"external_dlls", "mo2_dlls", "mods"}

    _asset_directories = {
        "action",
        "animation",
        "animations",
        "asset",
        "chr",
        "cutscene",
        "event",
        "facegen",
        "map",
        "material",
        "menu",
        "msg",
        "movie",
        "movie_dlc",
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
        "mods",
        "mo2_dlls",
        "native",
        "natives",
        "plugins",
    }
    _package_directories = {
        "<game>",
        "elden ring",
        "eldenring",
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
    _supported_extensions = {
        ".acb",
        ".anibnd",
        ".bin",
        ".bnd",
        ".chrbnd",
        ".cfg",
        ".dcx",
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
        ".png",
        ".partsbnd",
        ".tga",
        ".toml",
        ".tpf",
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
    ):
        self._organizer = organizer
        self._plugin_name = plugin_name
        self._preserve_extensions = frozenset(
            extension.casefold() for extension in preserve_extensions
        )
        self._saved_archive_layout_signatures: set[tuple[str, ...]] | None = None
        self._saved_archive_layout_cache_epoch = None
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

    def fix(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
        if self._preserve_archive_layout_requested():
            return filetree
        return self.normalize_standard_layout(filetree)

    def normalize_standard_layout(
        self, filetree: mobase.IFileTree
    ) -> mobase.IFileTree:
        """Apply the standard DLL/assets layout directly to an archive tree."""
        # BasicGame's checker handles known Game/<game> wrappers. Repeat it after
        # unwrapping user-named mod folders, since those can contain another one.
        for _ in range(self._archive_wrapper_depth_limit):
            self._fix_known_layouts(filetree)
            unwrapped = False
            for entry in list(filetree):
                if is_directory(entry) and self._is_package_wrapper(entry):
                    filetree.merge(entry)
                    entry.detach()
                    unwrapped = True
            if not unwrapped:
                break

        # Older archives may put native plugins in ``native``/``natives``,
        # ``external_dlls``, ``MO2_DLLs`` or ``mods``. Move DLLs and related
        # sidecars into DLLs, then keep remaining resources at their normal paths.
        for folder_name in ("native", "natives"):
            folder = self._direct_directory(filetree, folder_name)
            if folder is not None:
                self._move_native_files(filetree, folder)
                if len(folder):
                    filetree.merge(folder)
                folder.detach()

        mod_folder = self._direct_directory(filetree, "mod")
        if mod_folder is not None:
            self._move_native_files(filetree, mod_folder)
            if len(mod_folder):
                filetree.merge(mod_folder)
            mod_folder.detach()

        for folder_name in self._legacy_dll_directories:
            legacy_folder = self._direct_directory(filetree, folder_name)
            if legacy_folder is not None:
                self._move_native_files(filetree, legacy_folder)
                if len(legacy_folder):
                    filetree.merge(legacy_folder)
                legacy_folder.detach()

        # Keep all root-level DLLs and their sidecars at the mod root. This
        # supports proxy loaders with nonstandard DLL names and preserves the
        # archive's intended paths for its configuration and log files.
        self._move_root_sidecars_for_nested_dlls(filetree)
        return filetree

    def _preserve_archive_layout_requested(self) -> bool:
        return bool(
            self._organizer.pluginSetting(
                self._plugin_name,
                EldenRingMo2Game.ArchiveLayoutOverrideSetting,
            )
        )

    def _is_saved_archive_layout_tree(self, filetree: mobase.IFileTree) -> bool:
        target_signature = self._file_tree_signature(filetree)
        if not target_signature:
            return False

        cache_epoch = self._archive_layout_cache_epoch()
        if (
            self._saved_archive_layout_signatures is not None
            and cache_epoch == self._saved_archive_layout_cache_epoch
        ):
            return target_signature in self._saved_archive_layout_signatures

        self._saved_archive_layout_signatures = None

        try:
            mod_list = self._organizer.modList()
            mod_names = mod_list.allMods()
        except Exception:
            return False

        signatures: set[tuple[str, ...]] = set()
        complete = True
        for mod_name in mod_names:
            try:
                mod = mod_list.getMod(mod_name)
                if mod is None or mod.isOverwrite():
                    continue
                settings = mod.pluginSettings(
                    EldenRingMo2Game.ArchiveLayoutInstallerName
                )
                if EldenRingMo2Game.PreserveArchiveLayoutSetting not in settings:
                    continue
                signature = self._file_tree_signature(mod.fileTree())
                if signature:
                    signatures.add(signature)
            except Exception:
                # A mod may be changing while MO2 rebuilds its data view. Keep
                # checking other entries and avoid retaining a partial cache.
                complete = False
                continue
        current_epoch = self._archive_layout_cache_epoch()
        if complete and current_epoch == cache_epoch:
            self._saved_archive_layout_signatures = signatures
            self._saved_archive_layout_cache_epoch = current_epoch
        return target_signature in signatures

    def _archive_layout_cache_epoch(self):
        try:
            return self._organizer.pluginSetting(
                self._plugin_name,
                EldenRingMo2Game.ArchiveLayoutCacheEpochSetting,
            )
        except Exception:
            return None

    def _invalidate_saved_archive_layout_cache(self, *_args) -> None:
        self._saved_archive_layout_signatures = None

    @staticmethod
    def _file_tree_signature(filetree: mobase.IFileTree) -> tuple[str, ...]:
        paths: list[str] = []

        def collect(tree: mobase.IFileTree, prefix: str = "") -> None:
            for entry in tree:
                name = entry.name().replace("\\", "/")
                relative_path = f"{prefix}/{name}" if prefix else name
                if is_directory(entry):
                    collect(entry, relative_path)
                elif (
                    name.casefold() != "meta.ini"
                    and Path(name).suffix.casefold() != ".mohidden"
                ):
                    paths.append(relative_path.casefold())

        collect(filetree)
        return tuple(sorted(paths))

    def _fix_known_layouts(self, filetree: mobase.IFileTree, depth: int = 0) -> None:
        if depth > self._archive_wrapper_depth_limit:
            return
        self._fix_basic_patterns(filetree)
        for entry in list(filetree):
            if is_directory(entry):
                self._fix_known_layouts(entry, depth + 1)

    def _fix_basic_patterns(self, filetree: mobase.IFileTree) -> mobase.IFileTree:
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
                assert is_directory(entry)
                filetree.merge(entry)
                entry.detach()
            elif patterns.valid.match(name):
                continue
            elif patterns.delete.match(name):
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
        for entry in filetree:
            name = entry.name().casefold()
            if is_directory(entry):
                # Managed DLL directories are already in a valid destination.
                if name in {
                    self._dll_directory.casefold(),
                    *self._legacy_dll_directories,
                    "plugins",
                }:
                    continue
                if name in {"mod", "native", "natives"} and any(
                    not is_directory(child)
                    and Path(child.name()).suffix.casefold() == ".dll"
                    for child in entry
                ):
                    return True
                if self._has_misplaced_dll(entry, name, depth + 1):
                    return True
            elif (
                Path(name).suffix.casefold() == ".dll"
                and bool(parent_name)
                and parent_name
                not in {
                    self._dll_directory.casefold(),
                    *self._legacy_dll_directories,
                    "plugins",
                }
            ):
                return True
        return False

    def _is_package_wrapper(self, folder: mobase.IFileTree) -> bool:
        name = folder.name().casefold()
        if name in self._content_directories:
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
        for entry in filetree:
            if is_directory(entry) and entry.name().casefold() == name:
                return entry
        return None

    def _move_native_files(
        self, filetree: mobase.IFileTree, source: mobase.IFileTree
    ) -> None:
        dlls = [
            entry
            for entry in source
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() == ".dll"
            and entry.name().casefold() not in self._root_loader_dlls
        ]
        if not dlls:
            return

        stems = {Path(entry.name()).stem.casefold() for entry in dlls}
        config_suffixes = {"config", "settings", "configuration", "options"}
        sidecars = [
            entry
            for entry in source
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._sidecar_extensions
            and self._is_dll_sidecar(
                Path(entry.name()).stem.casefold(), stems, config_suffixes
            )
        ]
        for entry in [*dlls, *sidecars]:
            filetree.move(entry, f"{self._dll_directory}/")

    def _move_root_sidecars_for_nested_dlls(
        self, filetree: mobase.IFileTree
    ) -> None:
        """Move a root-level INI beside a matching DLL found in a payload folder."""
        dll_stems: set[str] = set()

        def collect_dlls(tree: mobase.IFileTree, depth: int = 0) -> None:
            for entry in tree:
                if is_directory(entry):
                    collect_dlls(entry, depth + 1)
                elif (
                    depth > 0
                    and Path(entry.name()).suffix.casefold() == ".dll"
                    and entry.name().casefold() not in self._root_loader_dlls
                ):
                    dll_stems.add(Path(entry.name()).stem.casefold())

        collect_dlls(filetree)
        if not dll_stems:
            return

        config_suffixes = {"config", "settings", "configuration", "options"}
        root_sidecars = [
            entry
            for entry in filetree
            if not is_directory(entry)
            and Path(entry.name()).suffix.casefold() in self._sidecar_extensions
            and self._is_dll_sidecar(
                Path(entry.name()).stem.casefold(), dll_stems, config_suffixes
            )
        ]
        for entry in root_sidecars:
            filetree.move(entry, f"{self._dll_directory}/")

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
                if sidecar_stem.startswith(prefix) and sidecar_stem[
                    len(prefix):
                ] in config_suffixes:
                    return True
        return False


class EldenRingLocalSavegames(mobase.LocalSavegames):
    """Route Elden Ring saves according to the active profile's save mode."""

    ConfigFilename = "eldenring_mo2_saves.ini"
    ProfileIsolated = "profile"
    InstanceShared = "instance_shared"
    GlobalShared = "global"
    Modes = {ProfileIsolated, InstanceShared, GlobalShared}

    def __init__(self, game, organizer: mobase.IOrganizer):
        super().__init__()
        self._game = game
        self._organizer = organizer
        self._prepared_profile_routes: dict[str, tuple[str, bool]] = {}

    @staticmethod
    def _profile_key(profile: mobase.IProfile) -> str:
        path = Path(profile.absolutePath())
        try:
            return os.path.normcase(str(path.resolve()))
        except (OSError, RuntimeError):
            return os.path.normcase(os.path.abspath(path))

    @classmethod
    def mode_for_profile(cls, profile) -> str:
        config_path = Path(profile.absolutePath()) / cls.ConfigFilename
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(config_path, encoding="utf-8")
            mode = config.get("Saves", "mode", fallback=cls.ProfileIsolated)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            mode = cls.ProfileIsolated
        return mode if mode in cls.Modes else cls.ProfileIsolated

    @staticmethod
    def instance_shared_directory(profile_path: Path) -> Path:
        # MO2 keeps profiles below an instance-specific profiles directory.
        # Placing shared modded saves beside that directory keeps them separate
        # from profile saves and from the global Roaming save folder.
        return profile_path.parent.parent / "Elden Ring Shared Saves"

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        mode = self.mode_for_profile(profile)
        local_saves_enabled = bool(profile.localSavesEnabled())
        self._prepared_profile_routes[self._profile_key(profile)] = (
            mode,
            local_saves_enabled,
        )
        return mode != self.GlobalShared and local_saves_enabled

    def profile_route_is_current(self, profile: mobase.IProfile) -> bool:
        prepared_route = self._prepared_profile_routes.get(self._profile_key(profile))
        current_route = (
            self.mode_for_profile(profile),
            bool(profile.localSavesEnabled()),
        )
        return prepared_route == current_route

    def mappings(self, profile_save_dir: QDir):
        profile = self._organizer.profile()
        if profile is None:
            return []

        mode = self.mode_for_profile(profile)
        if mode == self.GlobalShared:
            return []

        if mode == self.InstanceShared:
            source = self.instance_shared_directory(Path(profile.absolutePath()))
        else:
            source = Path(profile_save_dir.absolutePath())

        return [
            mobase.Mapping(
                source=str(source),
                destination=self._game.savesDirectory().absolutePath(),
                is_directory=True,
                create_target=True,
            )
        ]


class EldenRingMo2Game(BasicGame):
    Name = "Elden Ring MO2 Support"
    Author = "ArialSenki"
    Version = "0.5.0-alpha.57"

    GameName = "ELDEN RING"
    GameShortName = "eldenring"
    GameNexusName = "eldenring"
    GameSteamId = 1245620
    GameBinary = "Game/eldenring.exe"
    GameDataPath = "Game"
    GameSaveExtension = "sl2"
    NativeDllSetting = "load_native_dlls"
    ArchiveLayoutOverrideSetting = "_archive_layout_override"
    ArchiveLayoutInstallerName = "Elden Ring Archive Layout Installer"
    PreserveArchiveLayoutSetting = "preserve_archive_layout"
    ArchiveLayoutCacheEpochSetting = "_archive_layout_cache_epoch"
    ProfileLaunchConfig = "eldenring_mo2_startup.ini"
    ProfileLaunchConfigEnvironment = "ELDENRING_MO2_PROFILE_CONFIG"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        if not super().init(organizer):
            return False

        self._refined_ui_filters = []

        self._profile_config_env_active = False
        self._profile_config_env_previous = None
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path: Path | None = None

        self._local_savegames_feature = EldenRingLocalSavegames(self, organizer)
        self._register_feature(self._local_savegames_feature)
        self._register_feature(
            EldenRingModDataChecker(organizer, self.name())
        )
        # The game plugin is discovered for every MO2 install, but its UI work
        # must only run in the instance that actually manages Elden Ring.
        organizer.onUserInterfaceInitialized(self._initialize_elden_ring_ui)
        if not organizer.onAboutToRun(self._prepare_profile_launch):
            qCritical("Elden Ring MO2: could not register the profile launch hook.")
            return False
        if not organizer.onFinishedRun(self._finish_profile_launch):
            qCritical("Elden Ring MO2: could not register the profile cleanup hook.")
            return False
        return True

    def _initialize_elden_ring_ui(self, _main_window) -> None:
        if not self.isActive():
            return

        self._organizer.setPluginSetting(
            self.name(), self.ArchiveLayoutOverrideSetting, False
        )
        self._start_refined_ui_alignment()

    def _start_refined_ui_alignment(self) -> None:
        """Align the executable selector and use a light native title bar."""
        if os.name != "nt":
            return
        application = QApplication.instance()
        if application is None:
            return
        self._refined_ui_attempts = 0
        self._refined_ui_timer = QTimer(application)
        self._refined_ui_timer.setInterval(250)
        self._refined_ui_timer.timeout.connect(self._align_refined_main_window)
        QTimer.singleShot(0, self._align_refined_main_window)
        self._refined_ui_timer.start()

    def _align_refined_main_window(self) -> None:
        self._refined_ui_attempts += 1
        application = QApplication.instance()
        if application is not None:
            for window in application.topLevelWidgets():
                selector = window.findChild(QComboBox, "executablesListBox")
                run_button = window.findChild(QAbstractButton, "startButton")
                if selector is None or run_button is None:
                    continue
                selector_parent = selector.parentWidget()
                layout = selector_parent.layout() if selector_parent is not None else None
                if layout is not None:
                    layout.setAlignment(selector, Qt.AlignmentFlag.AlignTop)
                selector.setMinimumContentsLength(32)
                selector.setSizeAdjustPolicy(
                    QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon
                )
                selector.setMaxVisibleItems(8)
                selector.view().setMinimumWidth(max(320, selector.width()))
                if not self._integrate_shortcut_menu(window, run_button):
                    continue
                self._set_light_native_title_bar(window)
                self._refined_ui_timer.stop()
                return
        if self._refined_ui_attempts >= 40:
            self._refined_ui_timer.stop()

    def _integrate_shortcut_menu(self, window, run_button: QAbstractButton) -> bool:
        # Source builds with the split-button change already have a single
        # QToolButton. Keep that native control and its auto-connected Run slot.
        if isinstance(run_button, QToolButton) and run_button.menu() is not None:
            run_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
            run_button.setMinimumHeight(44)
            return True

        if window.findChild(QToolButton, "refinedRunButton") is not None:
            return True

        shortcut = window.findChild(QPushButton, "linkButton")
        if shortcut is None:
            return False

        # MO2's compiled 2.5.2 UI names this QVBoxLayout verticalLayout_12.
        # The fork source renames it runButtonsLayout; support both layouts.
        layout = window.findChild(QBoxLayout, "runButtonsLayout")
        if layout is None:
            layout = window.findChild(QBoxLayout, "verticalLayout_12")

        if (
            not isinstance(layout, QBoxLayout)
            or layout.indexOf(run_button) < 0
            or layout.indexOf(shortcut) < 0
        ):
            # Find the containing layout by its actual child buttons if the
            # UI uses a different layout object name.
            layout = None
            parent = shortcut.parentWidget()
            while parent is not None:
                candidate = parent.layout()
                if (
                    isinstance(candidate, QBoxLayout)
                    and candidate.indexOf(run_button) >= 0
                    and candidate.indexOf(shortcut) >= 0
                ):
                    layout = candidate
                    break
                parent = parent.parentWidget()

        if not isinstance(layout, QBoxLayout):
            return False

        shortcut_menu = shortcut.menu()
        if shortcut_menu is None:
            return False

        run_index = layout.indexOf(run_button)
        shortcut_index = layout.indexOf(shortcut)
        layout.setDirection(QBoxLayout.Direction.LeftToRight)
        layout.setSpacing(0)
        layout.setContentsMargins(0, 0, 0, 0)
        split_button = QToolButton(layout.parentWidget() or run_button.parentWidget())
        split_button.setObjectName("refinedRunButton")
        split_button.setText(run_button.text())
        split_button.setIcon(run_button.icon())
        split_button.setIconSize(run_button.iconSize())
        split_button.setToolButtonStyle(Qt.ToolButtonStyle.ToolButtonTextBesideIcon)
        split_button.setPopupMode(QToolButton.ToolButtonPopupMode.MenuButtonPopup)
        split_button.setMenu(shortcut_menu)
        split_button.setToolTip("Run the selected program; use the arrow for shortcut options.")
        split_button.setAccessibleName("Run")
        split_button.setAccessibleDescription(
            "Run the selected program. Use the arrow section for shortcut options."
        )
        split_button.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        split_button.setMinimumWidth(max(132, run_button.minimumWidth()))
        split_button.setFixedHeight(44)
        split_button.setEnabled(run_button.isEnabled())
        split_button.setProperty("mo2RefinedSplitRun", True)

        # Route the main button area through MO2's existing auto-connected
        # slot. The menu area still refreshes shortcut state before opening.
        split_button.clicked.connect(
            lambda _checked=False, original=run_button: original.click()
        )
        split_button.pressed.connect(shortcut.pressed.emit)
        enable_mirror = _RunButtonEnabledMirror(run_button, split_button)
        run_button.installEventFilter(enable_mirror)
        self._refined_ui_filters.append(enable_mirror)

        layout.removeWidget(run_button)
        layout.removeWidget(shortcut)
        layout.insertWidget(min(run_index, shortcut_index), split_button, 1)
        layout.setStretch(layout.indexOf(split_button), 1)
        run_button.hide()
        shortcut.hide()
        split_button.show()
        return True

    @staticmethod
    def _set_light_native_title_bar(window) -> None:
        """Ask Windows to draw this window's native title bar in light mode."""
        try:
            dwm = ctypes.WinDLL("dwmapi", use_last_error=True)
            set_attribute = dwm.DwmSetWindowAttribute
            set_attribute.argtypes = [
                ctypes.c_void_p,
                ctypes.c_uint,
                ctypes.c_void_p,
                ctypes.c_uint,
            ]
            set_attribute.restype = ctypes.c_long
            hwnd = ctypes.c_void_p(int(window.winId()))
            use_dark_mode = ctypes.c_int(0)
            for attribute in (20, 19):
                result = set_attribute(
                    hwnd,
                    attribute,
                    ctypes.byref(use_dark_mode),
                    ctypes.sizeof(use_dark_mode),
                )
                if result == 0:
                    break
        except (AttributeError, OSError, TypeError, ValueError):
            # Older Windows versions may not expose either title-bar attribute.
            pass

    def settings(self) -> list[mobase.PluginSetting]:
        settings = super().settings()
        settings.append(
            mobase.PluginSetting(
                self.NativeDllSetting,
                (
                    "Load enabled mod DLLs through MO2. Some mods require an "
                    "extension API that this plugin does not provide."
                ),
                True,
            )
        )
        return settings

    def _prepare_profile_launch(
        self, app_path: str, _working_directory, _args: str
    ) -> bool:
        self._restore_profile_config_environment()
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path = None
        if not self.isActive():
            return True

        profile = self._organizer.profile()
        if profile is None or Path(app_path).name.casefold() != "eldenring.exe":
            return True

        profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
        profile_save_dir = Path(profile.absolutePath()) / "saves"
        game_save_dir = Path(self.savesDirectory().absolutePath())
        profile_saves_enabled = bool(profile.localSavesEnabled())
        save_mode = EldenRingLocalSavegames.mode_for_profile(profile)
        if not self._local_savegames_feature.profile_route_is_current(profile):
            QMessageBox.warning(
                None,
                "Reload Elden Ring save routing",
                "The save mode or MO2's profile-specific-save setting changed "
                "after MO2 loaded this profile. The game was not started because "
                "the active save mapping may still point to the previous folder.\n\n"
                "Restart MO2, or switch to another profile and back, before "
                "launching Elden Ring.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the active save mapping "
                f"is stale for profile '{profile.name()}'."
            )
            return False
        if save_mode in {
            EldenRingLocalSavegames.ProfileIsolated,
            EldenRingLocalSavegames.InstanceShared,
        } and not profile_saves_enabled:
            QMessageBox.warning(
                None,
                "Elden Ring save isolation is not active",
                "This profile is set to use isolated Elden Ring saves, but "
                "MO2's 'Use profile-specific Save Games' option is off.\n\n"
                "Enable that option for this profile in the Profiles dialog, "
                "then launch Elden Ring again. The game was not started, so "
                "the global Steam save was left untouched.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because profile-specific "
                f"saves are disabled for '{profile.name()}'."
            )
            return False
        if (
            save_mode == EldenRingLocalSavegames.GlobalShared
            and profile_saves_enabled
        ):
            QMessageBox.warning(
                None,
                "Elden Ring save mode does not match MO2",
                "This profile is set to use the global Steam save, but MO2's "
                "'Use profile-specific Save Games' option is on.\n\n"
                "Disable that option for this profile in the Profiles dialog "
                "to use the global save. The game was not started.",
            )
            qInfo(
                "Elden Ring MO2: blocked launch because global-save mode "
                f"conflicts with profile-specific saves for '{profile.name()}'."
            )
            return False

        if save_mode == EldenRingLocalSavegames.InstanceShared:
            active_save_dir = EldenRingLocalSavegames.instance_shared_directory(
                Path(profile.absolutePath())
            )
            save_route = "instance-shared modded saves"
        elif save_mode == EldenRingLocalSavegames.GlobalShared:
            active_save_dir = game_save_dir
            save_route = "global save shared with Steam and all MO2 instances"
        else:
            active_save_dir = profile_save_dir
            save_route = "profile-isolated saves"

        def normalize(path: Path) -> str:
            try:
                return os.path.normcase(str(path.resolve()))
            except (OSError, RuntimeError):
                return os.path.normcase(os.path.abspath(path))

        if save_mode != EldenRingLocalSavegames.GlobalShared and normalize(
            active_save_dir
        ) == normalize(game_save_dir):
            QMessageBox.critical(
                None,
                "Elden Ring save path conflict",
                "The selected isolated save folder resolves to the same path "
                "as the global Elden Ring save folder. The game was not started "
                "to protect the original save. Check the MO2 profile paths.",
            )
            qCritical(
                "Elden Ring MO2: blocked launch because isolated and global "
                f"save paths resolve to the same folder '{game_save_dir}'."
            )
            return False

        qInfo(
            "Elden Ring MO2: save routing for profile "
            f"'{profile.name()}': {save_route}; "
            f"active save folder='{active_save_dir}'; "
            f"profile saves='{profile_save_dir}'; "
            f"game save root='{game_save_dir}'."
        )
        (
            start_minimized,
            black_startup_background,
            exclude_cpu0,
            clear_overwrite_after_game,
            clear_overwrite_logs_before_game,
        ) = self._read_profile_launch_settings(profile_config)

        settings_file_state = (
            "found" if profile_config.is_file() else "missing; using defaults"
        )
        qInfo(
            "Elden Ring MO2: experimental options for profile "
            f"'{profile.name()}' ({settings_file_state}): "
            f"start minimized={'on' if start_minimized else 'off'}, "
            "black background="
            f"{'on' if black_startup_background else 'off'}, "
            f"exclude CPU 0={'on' if exclude_cpu0 else 'off'}, "
            "clear old Overwrite logs="
            f"{'on' if clear_overwrite_logs_before_game else 'off'}, "
            "clear Overwrite after exit="
            f"{'on' if clear_overwrite_after_game else 'off'}."
        )

        if start_minimized or black_startup_background or exclude_cpu0:
            self._activate_profile_config_environment(profile_config)
        if clear_overwrite_logs_before_game and not clear_overwrite_after_game:
            self._clear_overwrite_logs_before_game()
        if clear_overwrite_after_game:
            self._arm_overwrite_cleanup()
        self._preserve_previous_bridge_log()
        return True

    @staticmethod
    def _preserve_previous_bridge_log() -> None:
        log_directory = Path(tempfile.gettempdir())
        current_log = log_directory / "EldenRingMO2Bridge.log"
        previous_log = log_directory / "EldenRingMO2Bridge.previous.log"
        temporary_log = log_directory / ".EldenRingMO2Bridge.previous.tmp"
        try:
            if not current_log.is_file():
                qInfo(
                    "Elden Ring MO2: no existing bridge log to preserve before launch."
                )
                return
            shutil.copyfile(current_log, temporary_log)
            os.replace(temporary_log, previous_log)
            qInfo(
                "Elden Ring MO2: preserved the last bridge session as "
                f"'{previous_log}' before launch."
            )
        except OSError as error:
            qCritical(
                "Elden Ring MO2: could not preserve the previous bridge log; "
                f"the game will still start: {error}"
            )
        finally:
            try:
                temporary_log.unlink(missing_ok=True)
            except OSError:
                pass

    def _finish_profile_launch(self, app_path: str, _exit_code: int) -> None:
        if Path(app_path).name.casefold() != "eldenring.exe":
            self._overwrite_cleanup_armed = False
            self._overwrite_cleanup_path = None
            return
        self._restore_profile_config_environment()
        overwrite_path = self._overwrite_cleanup_path
        cleanup_armed = self._overwrite_cleanup_armed
        self._overwrite_cleanup_path = None
        self._overwrite_cleanup_armed = False
        if cleanup_armed and overwrite_path is not None:
            self._clear_overwrite_contents(overwrite_path)

    def _read_profile_launch_settings(
        self, path: Path
    ) -> tuple[bool, bool, bool, bool, bool]:
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8")
            start_minimized = config.getboolean(
                "Startup", "start_minimized", fallback=None
            )
            if start_minimized is None:
                start_minimized = config.getboolean(
                    "Startup", "prevent_focus_steal", fallback=None
                )
            if start_minimized is None:
                # Keep profiles created by earlier releases working.
                start_minimized = config.getboolean(
                    "Startup", "start_in_background", fallback=False
                )
            exclude_cpu0 = config.getboolean(
                "Performance", "exclude_cpu0_after_start", fallback=False
            )
            black_startup_background = config.getboolean(
                "Startup", "black_startup_background", fallback=False
            )
            clear_overwrite_after_game = config.getboolean(
                "Cleanup", "clear_overwrite_after_game", fallback=False
            )
            clear_overwrite_logs_before_game = config.getboolean(
                "Cleanup", "clear_overwrite_logs_before_game", fallback=False
            )
            return (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                clear_overwrite_after_game,
                clear_overwrite_logs_before_game,
            )
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not read profile launch settings "
                f"{path.name}: {error}. Using safe defaults."
            )
            return False, False, False, False, False

    def _clear_overwrite_logs_before_game(self) -> None:
        try:
            overwrite_path = Path(self._organizer.overwritePath())
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError("MO2 returned a path that is not named Overwrite")

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError("the Overwrite directory is a reparse point")
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError("the Overwrite path is not a directory")

            removed = self._remove_overwrite_logs(overwrite_path)
            qInfo(
                "Elden Ring MO2: removed "
                f"{removed} old .log file(s) from Overwrite before launch."
            )
        except FileNotFoundError:
            qInfo(
                "Elden Ring MO2: Overwrite was missing or changed before launch; "
                "no old logs were cleared."
            )
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: could not clear old .log files from Overwrite "
                f"before launch: {error}. The game will still start."
            )

    @staticmethod
    def _remove_overwrite_logs(overwrite_path: Path) -> int:
        removed = 0
        with os.scandir(overwrite_path) as entries:
            for entry in entries:
                path = Path(entry.path)
                entry_stat = os.lstat(path)
                reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
                if getattr(entry_stat, "st_file_attributes", 0) & reparse_flag:
                    continue
                if stat.S_ISDIR(entry_stat.st_mode):
                    removed += EldenRingMo2Game._remove_overwrite_logs(path)
                elif path.suffix.casefold() == ".log":
                    os.unlink(path)
                    removed += 1
        return removed

    def _arm_overwrite_cleanup(self) -> None:
        try:
            overwrite_path = Path(self._organizer.overwritePath())
        except (OSError, TypeError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: automatic Overwrite cleanup could not get "
                f"MO2's Overwrite path: {error}. The game will still start."
            )
            return

        self._overwrite_cleanup_path = overwrite_path
        self._overwrite_cleanup_armed = True
        qInfo("Elden Ring MO2: Overwrite cleanup will run after Elden Ring closes.")

    @staticmethod
    def _remove_overwrite_entry(path: Path) -> int:
        entry_stat = os.lstat(path)
        reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
        is_reparse_point = bool(
            getattr(entry_stat, "st_file_attributes", 0) & reparse_flag
        )

        # Never follow symlinks, junctions, or other Windows reparse points
        # outside MO2's Overwrite directory.
        if is_reparse_point:
            if stat.S_ISDIR(entry_stat.st_mode):
                os.rmdir(path)
            else:
                os.unlink(path)
            return 1

        if stat.S_ISDIR(entry_stat.st_mode):
            removed = 0
            with os.scandir(path) as entries:
                for entry in entries:
                    removed += EldenRingMo2Game._remove_overwrite_entry(
                        Path(entry.path)
                    )
            os.rmdir(path)
            return removed + 1

        os.unlink(path)
        return 1

    def _clear_overwrite_contents(self, overwrite_path: Path) -> None:
        try:
            if overwrite_path.name.casefold() != "overwrite":
                raise ValueError("MO2 returned a path that is not named Overwrite")

            root_stat = os.lstat(overwrite_path)
            reparse_flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
            if getattr(root_stat, "st_file_attributes", 0) & reparse_flag:
                raise ValueError("the Overwrite directory is a reparse point")
            if not stat.S_ISDIR(root_stat.st_mode):
                raise ValueError("the Overwrite path is not a directory")

            removed = 0
            with os.scandir(overwrite_path) as entries:
                for entry in entries:
                    removed += self._remove_overwrite_entry(Path(entry.path))
            qInfo(
                "Elden Ring MO2: cleared "
                f"{removed} file(s) and folder(s) from Overwrite after the game closed."
            )
        except FileNotFoundError:
            qInfo("Elden Ring MO2: Overwrite was already empty after the game closed.")
        except (OSError, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not fully clear Overwrite after the game "
                f"closed: {error}"
            )

    def _activate_profile_config_environment(self, path: Path) -> None:
        if not path.is_file():
            return
        key = self.ProfileLaunchConfigEnvironment
        self._profile_config_env_previous = os.environ.get(key)
        self._profile_config_env_active = True
        os.environ[key] = str(path)
        qInfo("Elden Ring MO2: profile launch options passed to bridge.")

    def _restore_profile_config_environment(self) -> None:
        if not getattr(self, "_profile_config_env_active", False):
            return
        key = self.ProfileLaunchConfigEnvironment
        previous = self._profile_config_env_previous
        if previous is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = previous
        self._profile_config_env_previous = None
        self._profile_config_env_active = False

    def documentsDirectory(self) -> QDir:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            appdata = str(Path.home() / "AppData" / "Roaming")
        return QDir(str(Path(appdata) / "EldenRing"))

    def savesDirectory(self) -> QDir:
        return self.documentsDirectory()

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_exe = QFileInfo(
            self.gameDirectory(),
            self.GameBinary,
        )
        executable = mobase.ExecutableInfo("ELDEN RING (MO2)", game_exe)
        executable = executable.withWorkingDirectory(self.dataDirectory())
        executable = executable.withSteamAppId(str(self.GameSteamId))
        return [executable]

    def executableForcedLoads(self) -> list[mobase.ExecutableForcedLoadSetting]:
        if not self.isActive():
            return []

        mod_list = self._organizer.modList()
        profile_order = list(mod_list.allModsByProfilePriority())
        active_mods = [
            name
            for name in profile_order
            if mod_list.state(name) & mobase.ModState.ACTIVE
        ]
        profile = self._organizer.profile()
        bridge_startup_options = False
        if profile is not None:
            profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
            (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                _clear_overwrite_after_game,
                _clear_overwrite_logs_before_game,
            ) = self._read_profile_launch_settings(profile_config)
            bridge_startup_options = (
                start_minimized or black_startup_background or exclude_cpu0
            )

        if not active_mods and not bridge_startup_options:
            qInfo(
                "Elden Ring MO2: no active mods or bridge-dependent profile "
                "startup options; no native or asset bridge needed."
            )
            return []
        if not active_mods:
            qInfo(
                "Elden Ring MO2: no active mods; loading the native bridge "
                "because profile startup options are enabled."
            )

        native_loading = bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        )
        libraries = self._active_native_dlls() if native_loading else []
        root_layout_libraries = (
            self._active_root_layout_dlls(active_mods) if native_loading else []
        )
        if not native_loading:
            qInfo("Elden Ring MO2: native DLL loading is disabled in plugin settings.")
        if not libraries and not root_layout_libraries and active_mods:
            qInfo(
                "Elden Ring MO2: no active winning DLLs found under "
                "Game/DLLs/, legacy managed folders, or preserved root layouts."
            )

        if libraries:
            qInfo(
                "Elden Ring MO2: requesting MO2 forced loads for "
                f"{len(libraries)} active mod DLL(s)."
            )
            for library in libraries:
                qInfo(f"Elden Ring MO2: queued native DLL {library.name}.")
        mod_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                "eldenring.exe", str(library)
            ).withEnabled(True)
            for library in libraries
        ]
        if root_layout_libraries:
            qInfo(
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
                qInfo(
                    f"Elden Ring MO2: queued {kind} {virtual_name} "
                    f"from active mod {mod_name}."
                )
        root_forced_loads = [
            mobase.ExecutableForcedLoadSetting(
                "eldenring.exe", str(library)
            ).withEnabled(True)
            for _, _, library, _ in root_layout_libraries
        ]
        bridge = Path(__file__).with_name("eldenring_mo2_bridge.dll")
        bridge_load = None
        if bridge.is_file():
            if active_mods:
                qInfo(
                    "Elden Ring MO2: queued native and loose-asset bridge "
                    f"{bridge.name} for {len(active_mods)} active mod(s)."
                )
            else:
                qInfo(
                    "Elden Ring MO2: queued native bridge "
                    f"{bridge.name} for profile startup options."
                )
            bridge_load = mobase.ExecutableForcedLoadSetting(
                Path(self.GameBinary).name, str(bridge)
            ).withEnabled(True)
            qInfo(
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
        return forced_loads

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
                    mod.pluginSettings(self.ArchiveLayoutInstallerName).get(
                        self.PreserveArchiveLayoutSetting, False
                    )
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
            ):
                continue

            try:
                relative_path = virtual_path.absolute().relative_to(data_root)
            except ValueError:
                # MO2 may return a resolved physical path. Preserve any
                # subdirectory below the searched managed root so support
                # DLLs can be recognized and excluded from forced loading.
                if virtual_path.is_absolute():
                    path_parts = virtual_path.parts
                    root_index = next(
                        (
                            index
                            for index, part in enumerate(path_parts)
                            if part.casefold() == search_directory.casefold()
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
                len(relative_path.parts) != 2
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
