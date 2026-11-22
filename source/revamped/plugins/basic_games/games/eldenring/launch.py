"""MO2 Revamped: a game launch from MO2: the checks before it, preparing it,
and what happens when the game closes.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import configparser
import os
from pathlib import Path
import shutil
import tempfile
import time


from PyQt6.QtCore import (
    QTimer,
    Qt,
)
from PyQt6.QtWidgets import (
    QMessageBox,
)

from .common import (
    qCritical,
    qInfo,
    qWarning,
    _tr,
    _running_processes,
)
from .checker import EldenRingModDataChecker


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class LaunchMixin:
    def _prepare_profile_launch(
        self, app_path: str, working_directory, args: str
    ) -> bool:
        """A launch that doesn't go ahead leaves off the mod it turned on."""
        self._mod_turned_on_for_launch = None
        allowed = False
        quarantine_before = self._active_save_quarantine
        _game_class()._executable_reads = {}
        _game_class()._mod_root_reads = {}
        try:
            allowed = self._check_profile_launch(app_path, working_directory, args)
            return allowed
        except Exception as error:
            # an error must not reach MO2 with the launch half prepared: the
            # environment is cleared and saves hidden by this launch go back
            qCritical(f"Elden Ring MO2: the launch could not be prepared: {error!r}")
            self._undo_failed_launch(quarantine_before)
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Could not start Elden Ring")),
                _tr("MO2 could not prepare the launch, so the game was not started. "
                "The details are in MO2's log."),
            )
            return False
        finally:
            _game_class()._executable_reads = None
            _game_class()._mod_root_reads = None
            if not allowed and self._mod_turned_on_for_launch:
                try:
                    self._organizer.modList().setActive(self._mod_turned_on_for_launch, False)
                except (AttributeError, RuntimeError) as error:
                    qWarning(f"Elden Ring MO2: could not turn the mod back off: {error}")
                self._mod_turned_on_for_launch = None

    def _undo_failed_launch(self, quarantine_before) -> None:
        """Clears what a launch that failed half-way had set up."""
        try:
            self._restore_profile_config_environment()
        except Exception as error:
            qWarning(f"Elden Ring MO2: could not clear the launch environment: {error!r}")
        session = self._active_save_quarantine
        if session is None or session == quarantine_before:
            return
        try:
            self._restore_global_save_quarantine(
                session, Path(self.savesDirectory().absolutePath())
            )
            self._active_save_quarantine = None
        except Exception as error:
            qCritical(f"Elden Ring MO2: the saves of the failed launch need recovery: {error!r}")
            self._report_displaced_saves()

    def _check_profile_launch(
        self, app_path: str, _working_directory, _args: str
    ) -> bool:
        self._restore_profile_config_environment()
        # every game plugin hears every run; only the managed game's acts
        if not self.isActive():
            return True
        qInfo(
            "Elden Ring MO2: about-to-run save guard received "
            f"'{Path(app_path).name}'."
        )
        if not self._is_game_launch(app_path, _args or ""):
            # tools (save editors and the like) run without touching the
            # hidden Steam saves of a game that may still be running, nor the
            # Overwrite cleanup and save restore that wait for that game
            return True
        # each launch's log says once what its native profile and forced
        # loads are, even when nothing changed since the last refresh
        self.__dict__.get("_logged_notes", {}).clear()
        self._last_forced_load_notes = None
        if not self._reforged_online_mod_ready(app_path):
            return False
        if not self._seamless_password_ready(app_path):
            return False
        if not self._convergence_seamless_ready(app_path):
            return False
        if not self._regulation_conflict_ready(app_path):
            return False
        if not self._file_loader_ready(app_path):
            return False
        if not self._save_extension_ready(app_path):
            return False
        if not self._steam_cloud_swap_ready(app_path):
            return False
        if self._active_save_quarantine is not None:
            starting = time.monotonic() < self._launch_pending_until
            state = self._elden_ring_process_state()
            launcher = self._open_launcher()
            if starting or state is not False or launcher:
                qCritical(
                    "Elden Ring MO2: a previous save quarantine is still active; "
                    "launch was blocked to protect the global save."
                )
                if state is False and not launcher:
                    # nothing to close: only the wait for the game to appear
                    QMessageBox.information(
                        self._message_parent(),
                        _tr("Steam saves still hidden"),
                        _tr("MO2 is still waiting for the last launch to start its game. "
                            "If it doesn't start, MO2 puts the saves back within a minute "
                            "and a half; try again then. The game was not started."),
                    )
                    return False
                # a launcher can stay open for as long as the user leaves it:
                # this says what to close instead of doing nothing
                self._recovery_waiting = True
                self._show_launch_recovery_blocked()
                return False
            try:
                self._restore_global_save_quarantine(
                    self._active_save_quarantine,
                    Path(self.savesDirectory().absolutePath()),
                )
                self._active_save_quarantine = None
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: stale save recovery blocked launch: {error}")
                return False

        profile = self._organizer.profile()
        if profile is None:
            self._accept_game_launch(app_path)
            return True
        if not self._recover_stale_global_save_quarantines():
            self._show_launch_recovery_blocked()
            return False
        self._report_displaced_saves()
        foreign_sessions = self._foreign_pending_sessions()
        if foreign_sessions:
            QMessageBox.critical(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save recovery needed")),
                self._game_text(_tr("Another MO2 Revamped installation has just hidden the Steam "
                "saves for a launch of its own. Wait until that game closes, then "
                "try again: MO2 puts the saves back itself. The game was not "
                "started.\n\n{0}")).format(
                    foreign_sessions[0]
                ),
            )
            qCritical(
                "Elden Ring MO2: blocked launch; another install still holds "
                f"hidden Steam saves in '{foreign_sessions[0]}'."
            )
            return False
        direct_game = Path(app_path).name.casefold() == self.GameProcess
        # read by the save hiding below (how long a launcher may take)
        self._launch_via_launcher = not direct_game
        # a launcher run outside the VFS (the game itself always runs inside,
        # Seamless Co-op included: started bare it would restart through Steam
        # with Easy Anti-Cheat)
        swap_launch = (
            not direct_game and self._launch_method(app_path) == self.LaunchMethodSwap
        )

        # the native profile applies to every game launch: its switched-off
        # entries stay out of the forced loads and its initializers go to
        # the bridge. A profile problem blocks eldenring.exe started
        # directly; a launcher run goes ahead as before without it.
        launch_allowed, revamped_native_profile = self._native_profile_for_launch(
            profile, direct_game
        )
        if not launch_allowed:
            return False

        profile_config = Path(profile.absolutePath()) / self.ProfileLaunchConfig
        profile_save_dir = Path(profile.absolutePath()) / "saves"
        game_save_dir = Path(self.savesDirectory().absolutePath())
        profile_saves_enabled = bool(profile.localSavesEnabled())
        save_mode = self.LocalSavegamesClass.mode_for_profile(profile)
        if not self._local_savegames_feature.profile_route_is_current(profile):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Reload Elden Ring save routing")),
                self._game_text(_tr("The save mode, named save profile, or MO2 profile-specific-save setting changed "
                "after MO2 loaded this profile. The game was not started because "
                "the active save mapping may still point to the previous folder.\n\n"
                "Restart MO2, or switch to another profile and back, before "
                "launching Elden Ring.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the active save mapping "
                f"is stale for profile '{profile.name()}'."
            )
            return False
        if not self.LocalSavegamesClass.save_profile_is_available(
            Path(profile.absolutePath()), save_mode
        ):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Named Elden Ring save profile is missing")),
                self._game_text(_tr("The selected named save profile could not be found or its metadata "
                "is invalid. The game was not started. Open Tools > "
                "Save isolation and choose an available profile, or restore the "
                "missing profile folder.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because the selected named "
                "save profile is unavailable."
            )
            return False
        if save_mode in {
            self.LocalSavegamesClass.ProfileIsolated,
            self.LocalSavegamesClass.InstanceShared,
        } and not profile_saves_enabled:
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save isolation is not active")),
                self._game_text(_tr("This profile is set to use isolated Elden Ring saves, but "
                "MO2's 'Use profile-specific Save Games' option is still off "
                "in this session. The saved route needs that setting. Restart "
                "MO2 to reload the route before launching. The game was not "
                "started, and the global Steam save was left untouched.")),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because profile-specific "
                f"saves are disabled for '{profile.name()}'."
            )
            return False
        if (
            save_mode == self.LocalSavegamesClass.GlobalShared
            and profile_saves_enabled
        ):
            QMessageBox.warning(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save mode does not match MO2")),
                _tr("This profile is set to use the global Steam save, but MO2's "
                "'Use profile-specific Save Games' option is still on in this "
                "session. The global Steam route is saved. Restart MO2 to "
                "reload it before launching. The game was not started."),
            )
            qInfo(
                "Elden Ring MO2: blocked launch because global-save mode "
                f"conflicts with profile-specific saves for '{profile.name()}'."
            )
            return False

        active_save_dir = self.LocalSavegamesClass.active_save_directory(
            Path(profile.absolutePath()), save_mode
        )
        if save_mode == self.LocalSavegamesClass.GlobalShared:
            active_save_dir = game_save_dir
            save_route = "global save shared with Steam and all MO2 instances"
        elif save_mode == self.LocalSavegamesClass.InstanceShared:
            save_route = "instance-shared modded saves"
        else:
            save_route = "profile-isolated saves"
        selected_save_profile = (
            self.LocalSavegamesClass.save_profile_for_profile_directory(
                Path(profile.absolutePath()), save_mode
            )
        )
        if selected_save_profile:
            save_route += f" (named save profile {selected_save_profile})"

        def normalize(path: Path) -> str:
            try:
                return os.path.normcase(str(path.resolve()))
            except (OSError, RuntimeError):
                return os.path.normcase(os.path.abspath(path))

        if save_mode != self.LocalSavegamesClass.GlobalShared and normalize(
            active_save_dir
        ) == normalize(game_save_dir):
            QMessageBox.critical(
                self._message_parent(),
                self._game_text(_tr("Elden Ring save path conflict")),
                self._game_text(_tr("The selected isolated save folder resolves to the same path "
                "as the global Elden Ring save folder. The game was not started "
                "to protect the original save. Check the MO2 profile paths.")),
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
        if active_save_dir is not None and self._elden_ring_process_state() is False:
            self._backup_route_saves(
                active_save_dir,
                self.save_backup_root(
                    active_save_dir,
                    Path(self._organizer.basePath()),
                    save_mode == self.LocalSavegamesClass.GlobalShared,
                ),
                self._save_backups_kept(),
            )

        # Reforged online with its own save: the swap below takes the online
        # folder instead of the route (a global route has none to keep apart:
        # both modes use the Steam saves)
        swap_route = active_save_dir
        if (
            swap_launch
            and active_save_dir is not None
            and save_mode != self.LocalSavegamesClass.GlobalShared
            and Path(app_path).name.casefold() == self.ReforgedOnlineLauncher
            and self.reforged_online_saves_separate(Path(self._organizer.basePath()))
        ):
            try:
                swap_route = self._reforged_online_route(active_save_dir)
            except OSError as error:
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Could not prepare the online saves")),
                    self._game_text(_tr("MO2 could not set up Reforged's online save folder, so the "
                    "game was not started. Your saves were not changed.\n\n{0}")).format(error),
                )
                qCritical(f"Elden Ring MO2: the Reforged online save folder failed: {error}")
                return False
            if self._elden_ring_process_state() is False:
                self._backup_route_saves(
                    swap_route,
                    active_save_dir.parent / f"{active_save_dir.name} Backups" / "online",
                    self._save_backups_kept(),
                )
            qInfo(f"Elden Ring MO2: Reforged online plays with its own saves in '{swap_route}'.")
        (
            start_minimized,
            black_startup_background,
            exclude_cpu0,
            clear_overwrite_after_game,
            clear_overwrite_logs_before_game,
            process_priority,
            bridge_log_history,
        ) = self._read_profile_launch_settings(profile_config)

        if save_mode != self.LocalSavegamesClass.GlobalShared:
            # earlier sessions were recovered at the start of this launch, and
            # the save hiding below checks them once more
            if self._elden_ring_process_state() is not False:
                QMessageBox.warning(
                    self._message_parent(),
                    self._game_text(_tr("Could not safely isolate Elden Ring saves")),
                    self._game_text(_tr("MO2 could not confirm that Elden Ring is closed. Close any "
                    "running Elden Ring process and try again. Existing global "
                    "saves were left in place.")),
                )
                return False
            try:
                self._active_save_quarantine = self._hide_existing_global_save_directories(
                    swap_route if swap_launch else None,
                    # Reforged plays ER0000.err and has no use for the .sl2
                    swap_launch
                    and Path(app_path).name.casefold() == self.ReforgedOnlineLauncher,
                )
            except OSError as error:
                qCritical(f"Elden Ring MO2: save isolation could not begin: {error}")
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Elden Ring saves could not be isolated")),
                    _tr("MO2 could not safely separate the existing Steam saves. "
                    "The game was not started. The original files remain in the "
                    "recovery folder if a partial move had already begun."),
                )
                self._report_displaced_saves()
                return False
            if active_save_dir is not None and not swap_launch:
                self._seed_route_root_files(game_save_dir, active_save_dir)

        settings_file_state = (
            "found" if profile_config.is_file() else "missing; using defaults"
        )
        qInfo(
            "Elden Ring MO2: experimental options for profile "
            f"'{profile.name()}' ({settings_file_state}): "
            f"start minimized={'on' if start_minimized else 'off'}, "
            "black background="
            f"{'on' if black_startup_background else 'off'}, "
            "skip intro logos="
            f"{'on' if self._read_skip_intro_logos(profile_config) else 'off'}, "
            f"exclude CPU 0={'on' if exclude_cpu0 else 'off'}, "
            "process priority="
            f"{'above normal' if process_priority == 1 else 'system default'}, "
            "clear old Overwrite logs="
            f"{'on' if clear_overwrite_logs_before_game else 'off'}, "
            "clear Overwrite after exit="
            f"{'on' if clear_overwrite_after_game else 'off'}, "
            f"previous bridge sessions={bridge_log_history}."
        )

        initializer_entries = (
            [
                entry
                for entry in revamped_native_profile.get("entries", [])
                if entry.get("initializer")
            ]
            if revamped_native_profile is not None
            else []
        )
        if swap_launch:
            return self._run_swap_launch(app_path, _working_directory, _args or "")
        if direct_game:
            # Seamless Co-op with the game run from MO2
            initializer_entries = initializer_entries + self._seamless_initializer()
        self._activate_profile_config_environment(profile_config, initializer_entries)
        # a new run only once nothing above blocked the launch: a blocked
        # attempt must not disown the watch of a game that is still running
        self._game_run += 1
        self._accept_game_launch(app_path)
        if clear_overwrite_logs_before_game and not clear_overwrite_after_game:
            self._clear_overwrite_logs_before_game()
        if clear_overwrite_after_game:
            self._arm_overwrite_cleanup()
            self._overwrite_cleanup_config = profile_config
        self._preserve_previous_bridge_log(self.BridgeLogName, bridge_log_history)
        return True

    def _run_swap_launch(self, app_path: str, working_directory, args: str) -> bool:
        """Starts a launcher outside the VFS and cancels MO2's own run of it.
        The route's saves are already in the Steam folder; they go back when
        the game closes, as for any launcher run."""
        session = self._active_save_quarantine
        self._game_run += 1
        # this run has no bridge: the bridge log left by an earlier run is not
        # this one's, and must not bring back that run's warnings
        self._last_launch_time = time.time()
        try:
            started = self._start_outside_vfs(app_path, working_directory, args)
        except Exception as error:
            # the saves are already swapped: any failure puts them back now
            qCritical(f"Elden Ring MO2: the launch outside the VFS failed: {error}")
            started = False
        if not started:
            if session is not None:
                try:
                    self._restore_global_save_quarantine(
                        session, Path(self.savesDirectory().absolutePath())
                    )
                    self._active_save_quarantine = None
                except (OSError, RuntimeError, ValueError) as error:
                    qCritical(
                        "Elden Ring MO2: the launch outside the VFS did not start "
                        f"and its saves need recovery: {error}"
                    )
            QMessageBox.warning(
                self._message_parent(),
                _tr("Could not start the launcher"),
                _tr("MO2 could not start {0} outside its virtual file system. "
                "The game was not started.").format(Path(app_path).name),
            )
            self._report_displaced_saves()
            return False
        if session is not None:
            self._finish_when_game_closes(True, session, False, None, 0.0, False, self._game_run)
        # it did start: the False below only stops MO2's own run, and the mod
        # it turned on stays on
        self._mod_turned_on_for_launch = None
        # MO2 must not start it a second time under its hooks
        return False

    def _native_profile_for_launch(self, profile, direct_game: bool) -> tuple[bool, dict | None]:
        """Resolves the Revamped native profile for a game launch.

        Returns (launch allowed, profile data). A problem blocks a direct
        launch; a launcher launch goes ahead without the profile's
        initializers (forced loads then fall back to every active DLL)."""
        revamped_native_profile = self._resolve_revamped_native_profile(profile)
        if revamped_native_profile is not None and revamped_native_profile.get("error"):
            message = str(revamped_native_profile["error"])
            if not direct_game:
                qWarning(
                    "Elden Ring MO2: the Revamped native profile could not be "
                    f"prepared for this launcher run: {message}. The launcher "
                    "was started anyway; switched-off entries and initializers "
                    "are not applied."
                )
                return True, None
            QMessageBox.warning(
                self._message_parent(),
                _tr("Revamped native profile could not be prepared"),
                _tr("{0}\n\nThe game was not started.").format(message),
            )
            qCritical(
                "Elden Ring MO2: blocked launch for invalid Revamped native "
                f"profile: {message}"
            )
            return False, None
        if revamped_native_profile is not None and revamped_native_profile.get("entries") and not bool(
            self._organizer.pluginSetting(self.name(), self.NativeDllSetting)
        ):
            if not direct_game:
                qWarning(
                    "Elden Ring MO2: this profile declares native DLLs in its "
                    "Revamped native profile, but native DLL loading is "
                    "disabled in plugin settings. The launcher was started "
                    "anyway; no native DLLs or initializers are passed."
                )
                return True, None
            QMessageBox.warning(
                self._message_parent(),
                _tr("Native DLL loading is disabled"),
                self._game_text(_tr("This MO2 profile declares native DLLs in its Revamped native "
                "profile, but "
                "Elden Ring native DLL loading is disabled in plugin settings. "
                "The game was not started.")),
            )
            return False, None
        return True, revamped_native_profile

    @staticmethod
    def _preserve_previous_bridge_log(
        log_name: str = "EldenRingMO2Bridge", previous_sessions: int = 1
    ) -> None:
        log_directory = Path(tempfile.gettempdir())
        current_log = log_directory / f"{log_name}.log"
        if previous_sessions not in (1, 3, 5):
            previous_sessions = 1

        def previous_log_path(index: int) -> Path:
            suffix = "previous.log" if index == 1 else f"previous-{index}.log"
            return log_directory / f"{log_name}.{suffix}"

        temporary_paths: list[Path] = []
        try:
            if not current_log.is_file():
                qInfo(
                    "Elden Ring MO2: no existing bridge log to preserve before launch."
                )
                return

            for index in range(previous_sessions, 1, -1):
                source = previous_log_path(index - 1)
                if not source.is_file():
                    continue
                destination = previous_log_path(index)
                temporary_log = log_directory / f".{destination.name}.tmp"
                temporary_paths.append(temporary_log)
                shutil.copyfile(source, temporary_log)
                os.replace(temporary_log, destination)

            previous_log = previous_log_path(1)
            temporary_log = log_directory / f".{previous_log.name}.tmp"
            temporary_paths.append(temporary_log)
            shutil.copyfile(current_log, temporary_log)
            os.replace(temporary_log, previous_log)
            qInfo(
                f"Elden Ring MO2: rotated bridge logs, keeping up to "
                f"{previous_sessions} previous session(s)."
            )
        except OSError as error:
            qCritical(
                "Elden Ring MO2: could not preserve the previous bridge log; "
                f"the game will still start: {error}"
            )
        finally:
            for temporary_log in temporary_paths:
                try:
                    temporary_log.unlink(missing_ok=True)
                except OSError:
                    pass

    def _accept_game_launch(self, app_path: str) -> None:
        # the finished-run hook only gets the path: it recognises this run by
        # it, even when other tools ran in between
        self._guarded_launches.add(os.path.normcase(str(app_path)))
        self._last_launch_time = time.time()
        # this launch arms its own Overwrite cleanup below, if any
        self._overwrite_cleanup_armed = False
        self._overwrite_cleanup_path = None

    def _finish_profile_launch(self, app_path: str, _exit_code: int) -> None:
        # every game plugin hears every run; only the managed game's acts
        if not self.isActive():
            return
        qInfo(
            "Elden Ring MO2: finished-run save cleanup received "
            f"'{Path(app_path).name}'."
        )
        launch_key = os.path.normcase(str(app_path))
        guarded = self._is_game_launch(app_path) or launch_key in self._guarded_launches
        if not guarded:
            # a tool that ran meanwhile: the game's restore and cleanup still
            # wait for the game's own run
            return
        self._guarded_launches.discard(launch_key)
        self._restore_profile_config_environment()
        # MO2 reports the run as finished when its lock is off or unlocked, and
        # a launcher finishes while the game it started keeps running; the
        # Steam saves come back and Overwrite is cleared only once the game is
        # closed
        self._finish_when_game_closes(
            Path(app_path).name.casefold() != self.GameProcess,
            self._active_save_quarantine,
            self._overwrite_cleanup_armed,
            self._overwrite_cleanup_path,
            0.0,
            self._game_seen_run == self._game_run,
            self._game_run,
        )
        self._overwrite_cleanup_path = None
        self._overwrite_cleanup_armed = False

    def _warn_if_saves_were_protected(self) -> bool:
        """The bridge closes Reforged when it can't give it its own save."""
        log = Path(tempfile.gettempdir()) / f"{self.BridgeLogName}.log"
        try:
            if log.stat().st_mtime < self._last_launch_time:
                return False
            if "Save extension refused" not in log.read_text("utf-8", "replace"):
                return False
        except OSError:
            return False
        qWarning("Elden Ring MO2: the bridge closed the game to protect the saves.")
        self._early_exit_warning_shown = True
        # not from inside MO2's finished-run hook
        QTimer.singleShot(
            0,
            lambda: QMessageBox.warning(
                self._message_parent(),
                _tr("Saves may mix"),
                _tr("MO2 closed ELDEN RING Reforged to protect your saves. It couldn't "
                "give Reforged its own save file (ER0000.err), and the game would have "
                "written to ER0000.sl2, the save of your other mods. This usually "
                "follows a game update: look for a new version of MO2 Revamped."),
            ),
        )
        return True

    def _warn_if_loose_files_were_skipped(
        self, via_launcher: bool = True, seen: bool = False
    ) -> None:
        """A game update can change the code the bridge looks for; the mods'
        game files then stay out with nothing but a line in its log."""
        if self._loose_files_warning_shown:
            return
        log = Path(tempfile.gettempdir()) / f"{self.BridgeLogName}.log"
        try:
            # the bridge rewrites it when it loads: an older one is another run's
            if log.stat().st_mtime < self._last_launch_time:
                self._warn_if_bridge_did_not_load(via_launcher, seen)
                return
            if "Loose asset hook skipped" not in log.read_text("utf-8", "replace"):
                return
        except OSError:
            self._warn_if_bridge_did_not_load(via_launcher, seen)
            return
        qWarning("Elden Ring MO2: the bridge could not hook the game's file loader.")
        if not self._active_mods_have_game_files():
            return
        self._loose_files_warning_shown = True
        # not from inside MO2's finished-run hook
        QTimer.singleShot(
            0,
            lambda: QMessageBox.warning(
                self._message_parent(),
                _tr("Mods may not have loaded"),
                self._game_text(_tr("Elden Ring started, but MO2 could not connect to its "
                "file loader, so the game files of your mods were not used (their DLLs "
                "were not affected). This usually follows a game update: look for a new "
                "version of MO2 Revamped.")),
            ),
        )

    def _warn_if_bridge_did_not_load(self, via_launcher: bool, seen: bool) -> None:
        """The game ran from MO2 but the bridge never wrote its log: Force
        load libraries is off for the executable (or something stopped the
        injection), and the mods' game files and DLLs were left out."""
        if (
            via_launcher
            or not seen
            or time.time() - self._last_launch_time < self.EarlyExitSeconds
            or not self._active_mods_have_game_files()
        ):
            return
        self._loose_files_warning_shown = True
        qWarning("Elden Ring MO2: the bridge did not load in this run.")
        QTimer.singleShot(
            0,
            lambda: QMessageBox.warning(
                self._message_parent(),
                _tr("Mods did not load"),
                self._game_text(_tr("Elden Ring ran without MO2's bridge, so your mods' "
                "game files and DLLs were not used. Check that Force load libraries "
                "is on for this executable in Modify Executables.")),
            ),
        )

    def _warn_if_game_closed_at_once(self, via_launcher: bool, run: int) -> None:
        """Without the license on the Steam account, or with a mod that stops it,
        the game closes in seconds with no message of its own."""
        if (
            via_launcher
            or run != self._game_run
            or self._early_exit_warning_shown
            or time.time() - self._last_launch_time >= self.EarlyExitSeconds
        ):
            return
        launch_time = self._last_launch_time
        # Steam can restart the game by itself: the notice waits to see that
        QTimer.singleShot(
            self.EarlyExitRecheck, lambda: self._show_early_exit_warning(launch_time)
        )

    def _show_early_exit_warning(self, launch_time: float) -> None:
        if (
            self._early_exit_warning_shown
            or launch_time != self._last_launch_time
            or self._elden_ring_process_state(_running_processes()) is not False
        ):
            return
        self._early_exit_warning_shown = True
        qWarning("Elden Ring MO2: the game closed right after starting.")
        QMessageBox.warning(
            self._message_parent(),
            self._game_text(_tr("Elden Ring closed right after starting")),
            _tr("Check that Steam is open on the account that owns the game, and that "
            "the game starts from Steam. If it does, turn off your mods and add them "
            "back one by one to find the one stopping it."),
        )

    def _active_mods_have_game_files(self) -> bool:
        folders = EldenRingModDataChecker._asset_directories
        for _name, root, active in self._mod_folders():
            if not active:
                continue
            try:
                if any(
                    entry.is_dir() and entry.name.casefold() in folders
                    for entry in root.iterdir()
                ):
                    return True
            except OSError:
                continue
        return False

    def _finish_when_game_closes(
        self,
        via_launcher: bool,
        session_directory: Path | None,
        cleanup_armed: bool,
        overwrite_path: Path | None,
        waited: float,
        seen: bool,
        run: int = 0,
        unknown: float = 0.0,
    ) -> None:
        # one reading per tick for the game and its launchers
        processes = _running_processes()
        state = self._elden_ring_process_state(processes)
        seen = seen or state is True
        # with the Steam saves hidden, they have to be back before Steam
        # Cloud's exit sync, whatever started the game (MO2 reports a direct
        # run finished early when its lock is off or unlocked)
        interval = (
            self.SwapWatchInterval
            if session_directory is not None
            else self.GameWatchInterval
        )
        # seconds in a row the process list could not be read
        unknown = unknown + interval / 1000 if state is None else 0.0
        if unknown >= self.LauncherWaitLimit:
            # the saves stay hidden: the next launch, or the next MO2 start,
            # restores them once the game is known to be closed
            qCritical(
                "Elden Ring MO2: could not tell for ten minutes whether the game "
                "is still running; stopped waiting. The hidden Steam saves stay "
                "in recovery until the next launch or MO2 start."
            )
            return
        # a launcher still open can start the game again, against these saves
        game_closed = state is False and (
            not via_launcher
            or (
                (seen or waited >= self.LauncherStartGrace)
                and not self._launcher_running(processes)
            )
        )
        if via_launcher:
            self._update_launcher_notice(session_directory, state, seen, processes)
        if not game_closed:
            if waited == 0:
                qInfo(
                    "Elden Ring MO2: waiting for Elden Ring to close before "
                    "restoring the Steam saves."
                )
            QTimer.singleShot(
                interval,
                lambda: self._finish_when_game_closes(
                    via_launcher,
                    session_directory,
                    cleanup_armed,
                    overwrite_path,
                    waited + interval / 1000,
                    seen,
                    run,
                    unknown,
                ),
            )
            return

        # the notices must not stop the saves coming back below
        try:
            if not self._warn_if_saves_were_protected():
                self._warn_if_loose_files_were_skipped(via_launcher, seen)
                self._warn_if_game_closed_at_once(via_launcher, run)
        except Exception as error:
            qWarning(f"Elden Ring MO2: a notice after the game closed failed: {error!r}")
        # a later launch may have restored this session and started its own
        if session_directory is not None and self._active_save_quarantine == session_directory:
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, Path(self.savesDirectory().absolutePath())
                )
                self._active_save_quarantine = None
                qInfo(
                    "Elden Ring MO2: the game closed; the Steam saves are back "
                    "in their folder."
                )
                if self._displaced_newer_saves or self._kept_aside_saves:
                    self._report_displaced_saves()
                elif not restored_cleanly:
                    QMessageBox.warning(
                        self._message_parent(),
                        self._game_text(_tr("Elden Ring save recovery folder retained")),
                        _tr("The original Steam saves were restored. Additional "
                        "save output created outside the selected MO2 route was "
                        "preserved in the recovery folder for review."),
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(f"Elden Ring MO2: global save restoration failed: {error}")
                QMessageBox.critical(
                    self._message_parent(),
                    self._game_text(_tr("Elden Ring save recovery needed")),
                    self._game_text(_tr("MO2 could not restore the original Steam saves automatically. "
                    "The files remain in the MO2 Revamped recovery folder. Do not "
                    "launch Elden Ring or Steam until recovery is complete.")),
                )
        # a newer launch owns Overwrite now; its own cleanup runs after it
        if cleanup_armed and overwrite_path is not None and run == self._game_run:
            # the option may have been turned off while the game ran
            config_path = getattr(self, "_overwrite_cleanup_config", None)
            if config_path is not None and not self._read_clear_overwrite_after_game(config_path):
                qInfo(
                    "Elden Ring MO2: Overwrite cleanup was turned off while the "
                    "game ran; Overwrite was left as it is."
                )
            else:
                self._clear_overwrite_contents(overwrite_path)

    def _update_launcher_notice(
        self,
        session_directory: Path | None,
        state: bool | None,
        seen: bool,
        processes: list[tuple[int, str]] | None = None,
    ) -> None:
        """Says so once when the game has closed and its launcher stays open: the
        saves wait for the launcher, and the user may not know it is there."""
        # a watch of an older launch, or one with no saves hidden
        if session_directory is None or session_directory != self._active_save_quarantine:
            return
        launcher = self._open_launcher(processes) if state is False and seen else None
        if launcher is None:
            self._launcher_open_since = None
            self._close_launcher_notice()
            return
        now = time.monotonic()
        if self._launcher_open_since is None:
            self._launcher_open_since = now
        if (
            now - self._launcher_open_since < self.LauncherNoticeDelay
            or self._launcher_notice_session == session_directory
        ):
            return
        self._launcher_notice_session = session_directory
        # not modal, and it doesn't take the focus: the watch goes on behind it
        box = QMessageBox(self._message_parent())
        box.setWindowModality(Qt.WindowModality.NonModal)
        box.setAttribute(Qt.WidgetAttribute.WA_ShowWithoutActivating)
        box.setIcon(QMessageBox.Icon.Information)
        box.setWindowTitle(_tr("Steam saves still hidden"))
        box.setText(
            _tr("The game has closed, but {0} is still open. MO2 puts the Steam saves "
                "back when it closes. Don't start the game from Steam until then.").format(launcher)
        )
        self._launcher_notice = box
        box.show()

    def _close_launcher_notice(self) -> None:
        box = self._launcher_notice
        self._launcher_notice = None
        if box is not None:
            try:
                box.close()
            except RuntimeError:
                pass

    @staticmethod
    def _read_clear_overwrite_after_game(path: Path) -> bool:
        config = configparser.ConfigParser()
        try:
            with path.open("r", encoding="utf-8-sig") as stream:
                config.read_file(stream)
            return config.getboolean("Cleanup", "clear_overwrite_after_game", fallback=False)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            # unreadable: nothing is deleted
            return False

    def _read_boot_boost(self, path: Path) -> bool:
        # on unless the profile turned it off
        default = True
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            return config.getboolean("Startup", "boot_boost", fallback=default)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return default

    def _read_skip_intro_logos(self, path: Path) -> bool:
        # on unless the profile turned it off: the logos only cost time
        default = True
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
            return config.getboolean("Startup", "skip_intro_logos", fallback=default)
        except (OSError, UnicodeError, configparser.Error, ValueError):
            return default

    def _read_profile_launch_settings(
        self, path: Path
    ) -> tuple[bool, bool, bool, bool, bool, int, int]:
        config = configparser.ConfigParser()
        try:
            config.read(path, encoding="utf-8-sig")
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
            process_priority = config.getint(
                "Performance", "process_priority", fallback=0
            )
            if process_priority not in (0, 1):
                process_priority = 0
            bridge_log_history = config.getint(
                "Diagnostics", "previous_bridge_sessions", fallback=1
            )
            if bridge_log_history not in (1, 3, 5):
                bridge_log_history = 1
            return (
                start_minimized,
                black_startup_background,
                exclude_cpu0,
                clear_overwrite_after_game,
                clear_overwrite_logs_before_game,
                process_priority,
                bridge_log_history,
            )
        except (OSError, UnicodeError, configparser.Error, ValueError) as error:
            qCritical(
                f"Elden Ring MO2: could not read profile launch settings "
                f"{path.name}: {error}. Using safe defaults."
            )
            return False, False, False, False, False, 0, 1
