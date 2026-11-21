"""Elden Ring game support for Mod Organizer 2 without an external loader.

The plugin of ELDEN RING and ELDEN RING NIGHTREIGN: its identity (the names,
settings keys and constants instances and other plugins know it by) is here,
and its work is in eldenring/, one module per part, which EldenRingMo2Game
takes as mixins:

- checker: the mod data checker and the standard layout installs give mods
- saves: the save route of a profile and its VFS mapping
- recovery: Steam's save folders set aside for a launch, and put back
- swap: launches outside the VFS, with the route's saves swapped in
- interface: what the plugin shows in MO2's window
- backups: the save route's automatic backups
- launch: a launch from MO2, from its checks to the game closing
- native_profile: the Native DLL profile resolved against the active mods
- overwrite: the Overwrite cleanup
- environment: the environment a game run gets (the bridge's settings)
- programs: ELDEN RING Reforged's online launcher as an MO2 program
- forced_loads: the libraries MO2 loads into the game
- checks: the game version and the mods checked before a launch
- common: the log functions, translation and the process list
- recovery_probe: whether hidden saves wait, for the plugin's stub in other
  games' instances (basic_games' lazy_games)
"""

from __future__ import annotations

import os
import re
from pathlib import Path

import mobase
from PyQt6.QtCore import (
    QFileInfo,
    QDir,
)

from ..basic_features.basic_save_game_info import BasicGameSaveGame
from ..basic_game import BasicGame

from .eldenring.common import (
    qCritical,
    _with_true_case,
    _TR_CONTEXT,
    _tr,
)
from .eldenring.checker import EldenRingModDataChecker
from .eldenring.saves import EldenRingLocalSavegames, NightreignLocalSavegames
from .eldenring.recovery import SaveRecoveryMixin
from .eldenring.swap import SaveSwapMixin
from .eldenring.interface import InterfaceMixin
from .eldenring.backups import BackupsMixin
from .eldenring.launch import LaunchMixin
from .eldenring.native_profile import NativeProfileMixin
from .eldenring.overwrite import OverwriteMixin
from .eldenring.environment import EnvironmentMixin
from .eldenring.programs import OnlineProgramsMixin
from .eldenring.forced_loads import ForcedLoadsMixin
from .eldenring.checks import LaunchChecksMixin


class EldenRingMo2Game(
    SaveRecoveryMixin, SaveSwapMixin, InterfaceMixin, BackupsMixin, LaunchMixin, NativeProfileMixin, OverwriteMixin, EnvironmentMixin, OnlineProgramsMixin, ForcedLoadsMixin, LaunchChecksMixin,
    BasicGame,
):
    Name = "Elden Ring MO2 Support"

    Author = "MO2 Revamped"

    Description = (
        "Elden Ring support for MO2 Revamped: mod archive layouts, the native mod "
        "bridge, per-profile saves and startup options."
    )

    DescriptionContext = _TR_CONTEXT

    Version = "0.5.0-alpha.97"

    NativeBridgeVersion = "0.5.0-alpha.74"

    GameName = "ELDEN RING"

    GameShortName = "eldenring"

    # the bridge names its log after the game it runs in
    BridgeLogName = "EldenRingMO2Bridge"

    GameNexusName = "eldenring"

    GameSteamId = 1245620

    GameBinary = "Game/eldenring.exe"

    GameDataPath = "Game"

    GameSaveExtension = "sl2"

    # the names that differ between Elden Ring and Nightreign
    GameProcess = "eldenring.exe"

    # the code around a call to the game's file loader, Elden Ring then Nightreign:
    # install_loose_asset_hook in the bridge looks for the same, change both together
    # the arguments set up for that call, when the code after it changed:
    # find_resolver_by_argument_setup in the bridge, change both together
    ResolverArgumentSetups = (
        re.compile(rb"(?s)\x4C\x8B[\xC0-\xC7]\x48\xC7\x44\x24.\x00\x00\x00\x00\x33\xD2\x49\x8B[\xC8-\xCF]\xE8"),
        re.compile(rb"(?s)\x4C\x8B[\xC0-\xC7]\x33\xD2\x48\x89\x44\x24.\x49\x8B[\xC8-\xCF]\xE8"),
    )

    ResolverCallSites = (
        (b"", bytes.fromhex("48837B2008488D4B087203488B094C8B4B1841B8050000004D3BC8")),
        (bytes.fromhex("33D24889442420498BCD"), bytes.fromhex("48837F18087203488B3F48837B1805")),
    )

    SaveFolderName = "EldenRing"  # in %APPDATA%

    SavePrefix = "er0000."  # the save files: ER0000.sl2, .co2, .bak...

    GlobalBackupsFolderName = "Elden Ring Global Save Backups"

    LocalSavegamesClass = EldenRingLocalSavegames

    # Elden Ring keeps the recovery folder name it always had; other games
    # get their own, so a session is never restored into another game
    RecoveryScope = ""

    NativeDllSetting = "load_native_dlls"

    SaveBackupsSetting = "save_backups_kept"

    SaveBackupsDefault = 5

    ArchiveLayoutOverrideSetting = "_archive_layout_override"

    ArchiveLayoutInstallerName = "Elden Ring Archive Layout Installer"

    PreserveArchiveLayoutSetting = "preserve_archive_layout"

    ArchiveLayoutCacheEpochSetting = "_archive_layout_cache_epoch"

    InstalledNativeRouteIndexSetting = "installed_native_route_index"

    ProfileLaunchConfig = "eldenring_mo2_startup.ini"

    ProfileLaunchConfigEnvironment = "ELDENRING_MO2_PROFILE_CONFIG"

    NativeInitializerEnvironment = "ELDENRING_MO2_NATIVE_INITIALIZERS"

    SaveRecoveryDirectoryName = ".MO2RevampedSaveRecovery"

    SaveRecoveryManifestName = "session.json"

    # held open by the MO2 process that hid the saves, so a second MO2 of the
    # same install leaves its session alone
    SaveRecoveryLockName = ".owner.lock"

    UnroutedSaveDirectoryName = "unrouted-global"

    # the game's name in messages that name Elden Ring
    DisplayName = "Elden Ring"

    # Seamless Co-op's DLL, run from its MO2 mod with the game
    SeamlessDll = "ersc.dll"

    # both games' Seamless Co-op DLLs: the other game's is never loaded
    SeamlessDlls = frozenset({"ersc.dll", "nrsc.dll"})

    # Programs that start eldenring.exe themselves (Reforged's launcher) get
    # the same save guard as the game, and the guard waits for the game they
    # start instead of for the launcher.
    GameLaunchers = frozenset(
        {
            "eldenring.exe",
            "start_protected_game.exe",
            # ELDEN RING Reforged's own launcher (its online mode)
            "reforgedlauncher.exe",
        }
    )

    # Launchers that fail under MO2's virtual file system: MO2 starts them
    # itself, outside the VFS, and swaps the route's saves into the Steam
    # folder for the run. Each executable can be switched either way in Save
    # Isolation.
    AutoSwapLaunchers = frozenset(
        {
            # its online mode checks a clean release: no MO2 files or DLLs
            "reforgedlauncher.exe",
        }
    )

    # Reforged's online launcher, always run outside the VFS (inside it, MO2's
    # mods and natives would join the release its online mode checks)
    ReforgedOnlineLauncher = "reforgedlauncher.exe"

    # Online play keeps its own save, in this folder inside the profile's
    # route, apart from the offline save played with other MO2 mods (the
    # first online run starts from a copy of it). Save Isolation's switch,
    # kept in the launch methods file, can share one save between both.
    ReforgedOnlineSavesFolder = "reforged_online"

    ReforgedOnlineSavesKey = "reforged_online_saves"

    LaunchMethodsFilename = "eldenring_mo2_launch_methods.json"

    LaunchMethodVfs = "vfs"

    LaunchMethodSwap = "swap"

    # in a swap session's recovery folder: the Steam folder's own loose
    # files (GraphicsConfig.xml) while the route's copies stand in for them
    SwapRootFilesDirectoryName = "steam-root-files"

    # Easy Anti-Cheat's launcher has the same name in every game, so only the
    # one in this game's folder counts
    AntiCheatLauncher = "start_protected_game.exe"

    LauncherStartGrace = 90  # seconds a launcher may take to start the game

    # seconds without a readable process list before a watch gives up; a launcher
    # that is open is waited for without limit: it can start the game at any time
    LauncherWaitLimit = 600

    # seconds a launcher stays open after its game closed before MO2 says so
    LauncherNoticeDelay = 15

    GameWatchInterval = 5000  # milliseconds between checks for the game process

    EarlyExitSeconds = 30  # a game that closes sooner did not really start

    EarlyExitRecheck = 3000  # milliseconds to see that it did not start again

    # while a launch waits for the game to appear: a game that crashes at
    # start lives about two seconds, and once seen its close restores the
    # saves without the launcher grace. The Steam saves are hidden for the
    # whole wait, so it polls as fast as the swap watch (Steam Cloud, below)
    LaunchWatchInterval = 500

    # Steam Cloud looks at its save a second or two after the game closes: saves
    # of a launch outside the VFS have to be back by then
    SwapWatchInterval = 500

    # what Steam Cloud syncs of this game, and the game's own copy of it
    SteamCloudSaves = ("ER0000.sl2", "ER0000.sl2.bak")

    # in other games' instances basic_games gives MO2 a stub of this plugin
    # (lazy_games.py): it runs this check, and init() below when the check
    # finds hidden Steam saves to put back
    ManifestInitProbe = ".games.eldenring.recovery_probe:pending"

    def init(self, organizer: mobase.IOrganizer) -> bool:
        if not super().init(organizer):
            return False

        self._refined_ui_filters = []

        self._profile_config_env_active = False
        self._profile_config_env_previous = None
        self._native_initializer_env_active = False
        self._native_initializer_env_previous = None
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path: Path | None = None
        self._active_save_quarantine: Path | None = None
        self._launch_via_launcher = False
        # each game launch gets a number; watchers of an older launch leave the
        # newer one alone
        self._game_run = 0
        self._game_seen_run = -1
        self._overwrite_cleanup_config: Path | None = None
        # a launcher may take a while to start the game; until then a new
        # launch must not treat the game as closed
        self._launch_pending_until = 0.0
        # the paths of accepted game launches, for the finished-run hook
        self._guarded_launches: set[str] = set()
        # recovery sessions this process holds (see SaveRecoveryLockName)
        self._session_locks: dict[str, object] = {}
        # set when recovery waits for the game, a launcher or another MO2
        # window rather than failing
        self._recovery_waiting = False
        # set when that wait is for the game or a launcher of it: the Steam
        # saves are hidden meanwhile, so the retry polls fast (Steam Cloud)
        self._recovery_waiting_for_game = False
        self._recovered_sessions = 0
        self._recovery_retry_pending = False
        # folders with newer saves that a restore had to move aside
        self._displaced_newer_saves: list[Path] = []
        # saves a swap's return kept in the recovery folder instead of replacing them
        self._kept_aside_saves: list[Path] = []
        # (route, files changed) of swaps returned by recovery, with no MO2 there
        # when their game closed
        self._returned_saves: list[tuple[Path, int]] = []
        self._recovering_earlier_session = False
        self._main_window = None
        self._launcher_notice = None
        self._launcher_notice_session: Path | None = None
        self._launcher_open_since: float | None = None
        self._loose_files_warning_shown = False
        self._early_exit_warning_shown = False
        self._file_loader_scan: tuple[str, bool | None] = ("", None)
        self._save_extension_scan: tuple[str, bool | None] = ("", None)
        # the user was told Reforged can't have its own save and started anyway
        self._save_extension_accepted = False
        self._online_programs_selector = None
        self._online_program_locked = False
        self._online_programs_sync_queued = False
        self._last_launch_time = 0.0
        self._mod_turned_on_for_launch: str | None = None

        self._local_savegames_feature = self.LocalSavegamesClass(self, organizer)
        self._register_feature(self._local_savegames_feature)
        self._register_feature(
            EldenRingModDataChecker(organizer, self.name(), watch_mod_list=True)
        )
        if not self._recover_stale_global_save_quarantines():
            qCritical(
                "Elden Ring MO2: a previous save-isolation session needs "
                "recovery before Elden Ring can be launched."
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

    def setGamePath(self, path) -> None:
        super().setGamePath(_with_true_case(path))

    # the waits of save recovery are logged once each (_log_when_changed)
    RecoveryWaitLogKey = "save-recovery-wait"

    def settings(self) -> list[mobase.PluginSetting]:
        settings = super().settings()
        settings.append(
            mobase.PluginSetting(
                self.NativeDllSetting,
                (
                    _tr("Load enabled mod DLLs through MO2. Some mods require an "
                    "extension API that this plugin does not provide.")
                ),
                True,
            )
        )
        settings.append(
            mobase.PluginSetting(
                self.SaveBackupsSetting,
                self._game_text(_tr("How many automatic backups of the active Elden Ring save "
                "route to keep. A copy is made before each launch when the saves "
                "changed since the last one; 0 turns the backups off.")),
                self.SaveBackupsDefault,
            )
        )
        return settings

    BackupNamePattern = re.compile(r"^\d{8}-\d{6}(-\d+)?$")

    # "0" when the profile turned BootBoost off
    BootBoostEnvironment = "ELDENRING_MO2_BOOT_BOOST"

    # the bridge opens mods' sound files from these folders, highest priority first
    SoundFoldersEnvironment = "ELDENRING_MO2_SOUND_FOLDERS"

    # reforged.dll reads me3's attach config (JSON, the AttachConfig of me3's
    # launcher-attach-protocol crate) from this variable and stops the game
    # when it is missing
    ReforgedAttachEnvironment = "ME3_ATTACH_CONFIG"

    # the bridge replaces the game's .sl2 with these three letters
    SaveExtensionEnvironment = "ELDENRING_MO2_SAVE_EXTENSION"

    SaveExtensionAcceptedEnvironment = "ELDENRING_MO2_SAVE_EXTENSION_ACCEPTED"

    # ELDEN RING Reforged's me3 profile saves to ER0000.err
    ReforgedSaveExtension = "err"

    # The Convergence's me3 profile (convergence.me3, savefile = "ER0000.cnv")
    # saves to ER0000.cnv; MO2 doesn't use that profile, so without the bridge
    # renaming the save it played and overwrote ER0000.sl2, the save of
    # the game without mods
    ConvergenceSaveExtension = "cnv"

    # "1" when an active mod brings its own regulation.bin: the compatibility
    # DLL then keeps the raw regulation out of the saves, as me3 does for every
    # mod (its oversized_regulation_fix). A regulation larger than the game's
    # (The Convergence's) no longer fits in a save, and the game reported
    # "Failed to save game. Save data is corrupted" and saved nothing.
    RegulationFixEnvironment = "ELDENRING_MO2_REGULATION_FIX"
    ConvergenceNexusId = "3419"

    # me3's BootBoost for every Elden Ring start (the archive headers the game
    # decrypts on each start are cached decrypted), and what me3 does for
    # ELDEN RING Reforged beside loading it: Arxan neutralized (disable_arxan),
    # the raw regulation kept out of the save (its oversized_regulation_fix:
    # Reforged's regulation.bin no longer fits in a save, so every save after
    # the first failed with "run Reforged through its dedicated launcher") and
    # the game's heaps replaced by mimalloc (mem_patch: without it Reforged's
    # sound bank ran out of room and music and voices stopped on every dodge
    # or hit). The Reforged part only runs when ME3_ATTACH_CONFIG is set for
    # Reforged's natives. Source in me3_compat/.
    Me3Compat = "eldenring_mo2_me3_compat.dll"

    def documentsDirectory(self) -> QDir:
        appdata = os.environ.get("APPDATA")
        if not appdata:
            appdata = str(Path.home() / "AppData" / "Roaming")
        return QDir(str(Path(appdata) / self.SaveFolderName))

    def savesDirectory(self) -> QDir:
        return self.documentsDirectory()

    def listSaves(self, folder: QDir) -> list[mobase.ISaveGame]:
        # only the saves the game reads: <account>/ER0000.sl2, a Seamless
        # Co-op .co2, a Convergence .cnv or an ELDEN RING Reforged .err. A recursive search would also
        # list the named save profiles and backups kept inside the shared
        # route.
        return [
            BasicGameSaveGame(file)
            for file in self._route_save_files(Path(folder.absolutePath()))
            if file.suffix.casefold() in {".sl2", ".co2", ".cnv", ".err"}
        ]

    def executables(self) -> list[mobase.ExecutableInfo]:
        game_exe = QFileInfo(
            self.gameDirectory(),
            self.GameBinary,
        )
        executable = mobase.ExecutableInfo(f"{self.GameName} (MO2)", game_exe)
        executable = executable.withWorkingDirectory(self.dataDirectory())
        executable = executable.withSteamAppId(str(self.GameSteamId))
        # MO2 asks when it builds its program list, not on every mod toggle
        self._online_launchers_found = None
        return [executable, *self._reforged_online_executables()]

    # Reforged's launchers found in the mods folder, and the mods it held then
    _online_launchers_found: list[Path] | None = None

    _online_launchers_mods: frozenset[str] | None = None

    # the checks of one launch share their read of the game's executable (it
    # is large); None outside a launch, so its bytes aren't kept in memory
    _executable_reads: dict | None = None

    # the mod folders one launch has resolved (see _resolved_mod_root); None
    # outside a launch, so a moved or relinked mod is seen at the next one
    _mod_root_reads: dict | None = None


class EldenRingNightreignMo2Game(EldenRingMo2Game):
    """ELDEN RING NIGHTREIGN: the Elden Ring support with the game's own names.

    The saves are %APPDATA%/Nightreign/<SteamID64>/NR0000.sl2 (NR0000.co2
    with Seamless Co-op), the game is Game/nightreign.exe."""

    Name = "Elden Ring Nightreign MO2 Support"
    Description = (
        "Elden Ring Nightreign support for MO2 Revamped: mod archive layouts, the "
        "native mod bridge, per-profile saves and startup options."
    )
    GameName = "ELDEN RING NIGHTREIGN"
    GameShortName = "nightreign"
    BridgeLogName = "NightreignMO2Bridge"
    GameNexusName = "eldenringnightreign"
    # Nexus download links name it this way
    GameValidShortNames = ["eldenringnightreign"]
    GameSteamId = 2622380
    GameBinary = "Game/nightreign.exe"

    GameProcess = "nightreign.exe"
    SaveFolderName = "Nightreign"
    SavePrefix = "nr0000."
    SteamCloudSaves = ("NR0000.sl2", "NR0000.sl2.bak")
    GlobalBackupsFolderName = "Nightreign Global Save Backups"
    LocalSavegamesClass = NightreignLocalSavegames
    RecoveryScope = "|nightreign"
    DisplayName = "Nightreign"
    SeamlessDll = "nrsc.dll"
    GameLaunchers = frozenset(
        {
            "nightreign.exe",
            # Easy Anti-Cheat's launcher, the program Steam starts
            "start_protected_game.exe",
        }
    )
    # Reforged is an Elden Ring mod: Nightreign has no launcher to start
    # outside the VFS
    AutoSwapLaunchers = frozenset()
