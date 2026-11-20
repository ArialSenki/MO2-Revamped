"""MO2 Revamped: steam's save folders set aside for a launch, and put back: the
recovery sessions, their records and locks, and the watch until the game
starts.

Part of the Elden Ring and Nightreign game plugin (game_eldenring_mo2.py),
whose EldenRingMo2Game takes it as a mixin.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

try:
    import msvcrt
except ImportError:  # not Windows
    msvcrt = None

from PyQt6.QtCore import (
    QTimer,
)

from .common import (
    qCritical,
    qInfo,
    qWarning,
    _tr,
    _running_process_names,
    _running_processes,
    _process_image_path,
    GAMES_DIR,
)


def _game_class():
    """EldenRingMo2Game, looked up when called (it is built from this mixin)."""
    from ..game_eldenring_mo2 import EldenRingMo2Game

    return EldenRingMo2Game


class SaveRecoveryMixin:
    def _install_root(self) -> Path:
        try:
            return GAMES_DIR.resolve().parents[2]
        except (IndexError, OSError, RuntimeError):
            return Path(self._organizer.basePath())

    def _save_quarantine_parent(self) -> Path:
        """The folder that holds every install's recovery folder: beside the
        Steam save folder, or beside its target when that folder is a link to
        another drive (a folder can't be moved across drives in one step)."""
        global_save_root = Path(self.savesDirectory().absolutePath())
        parent = global_save_root.parent
        try:
            target = global_save_root.resolve()
            if (
                os.path.splitdrive(str(target))[0].casefold()
                != os.path.splitdrive(str(parent.resolve()))[0].casefold()
            ):
                parent = target.parent
        except (OSError, RuntimeError):
            pass
        return parent / self.SaveRecoveryDirectoryName

    def _save_quarantine_root(self) -> Path:
        install_root = self._install_root()
        try:
            install_identity = os.path.normcase(str(install_root.resolve()))
        except (OSError, RuntimeError):
            install_identity = os.path.normcase(os.path.abspath(install_root))
        instance_token = hashlib.sha256(
            (install_identity + self.RecoveryScope).encode("utf-8", errors="replace")
        ).hexdigest()[:24]
        return self._save_quarantine_parent() / instance_token

    def _session_is_pending(self, session: Path) -> bool:
        # a session that only keeps unrouted output has nothing left to restore
        children = [child for child in session.iterdir() if child.is_dir()]
        return (
            not children
            or any(child.name != self.UnroutedSaveDirectoryName for child in children)
            or self._swap_not_returned(session)
        )

    def _swap_not_returned(self, session: Path) -> bool:
        """Whether the route's saves of this session may still be in the Steam
        folder. Such a session can hold no folder at all (a Steam folder that was
        empty)."""
        try:
            manifest = json.loads(
                (session / self.SaveRecoveryManifestName).read_text(encoding="utf-8")
            )
            swap = manifest.get("swap")
        except (OSError, UnicodeError, ValueError, AttributeError):
            return False
        return isinstance(swap, dict) and not swap.get("returned")

    def _other_install_sessions(self) -> list[tuple[Path, bool]]:
        """Sessions of other MO2 Revamped installs that still hide Steam
        account folders of this game, each with whether the install that made
        it still exists."""
        own = self._save_quarantine_root()
        own_saves = self.savesDirectory().absolutePath()
        sessions: list[tuple[Path, bool]] = []
        try:
            for install in own.parent.iterdir():
                if not install.is_dir() or os.path.normcase(str(install)) == os.path.normcase(str(own)):
                    continue
                for session in install.iterdir():
                    if not session.is_dir() or not (
                        any(
                            child.is_dir() and child.name != self.UnroutedSaveDirectoryName
                            for child in session.iterdir()
                        )
                        or self._swap_not_returned(session)
                    ):
                        continue
                    try:
                        manifest = json.loads(
                            (session / self.SaveRecoveryManifestName).read_text(encoding="utf-8")
                        )
                        owner = manifest.get("install_root", "")
                        hidden_from = str(manifest.get("global_save_root", ""))
                    except (OSError, UnicodeError, ValueError, AttributeError):
                        continue
                    if hidden_from and os.path.normcase(
                        os.path.abspath(hidden_from)
                    ) != os.path.normcase(os.path.abspath(own_saves)):
                        continue
                    # a record without its install (older releases), or of an
                    # install that was moved or removed: nothing else will
                    # ever put these saves back
                    owner_exists = (
                        isinstance(owner, str)
                        and bool(owner.strip())
                        and Path(owner).is_dir()
                    )
                    sessions.append((session, owner_exists))
        except OSError:
            pass
        return sessions

    def _foreign_pending_sessions(self) -> list[Path]:
        """Sessions of other MO2 Revamped installs that still hide Steam
        account folders and that this install may not restore yet (see
        _stale_foreign_sessions); a launch from here would play against a
        Steam folder that is not complete."""
        return [session for session, owner_exists in self._other_install_sessions() if owner_exists]

    def _stale_foreign_sessions(self) -> list[Path]:
        """Sessions of other installs old enough that their launch is over:
        recovery restores them here too, once their lock is free (no MO2 of
        that install waits for its game) and the game and its launchers are
        closed. Their record says where everything goes back, so the other
        install no longer has to be started just to put the Steam saves back.
        A newer one may still belong to a launch that is starting."""
        stale = []
        now = time.time()
        for session in self._foreign_pending_sessions():
            try:
                written = (session / self.SaveRecoveryManifestName).stat().st_mtime
            except OSError:
                continue
            if now - written >= self.LauncherStartGrace:
                stale.append(session)
        return stale

    def _orphaned_sessions(self) -> list[Path]:
        """Sessions whose install no longer exists: this install restores
        them, so they don't block every launch forever."""
        return [
            session for session, owner_exists in self._other_install_sessions() if not owner_exists
        ]

    def _claim_save_quarantine(self, session_directory: Path) -> bool:
        """Takes the session for this MO2 process. False while another MO2
        process of this install holds it: it hid the saves for its own launch
        and may still be waiting for its game. Windows drops the hold when that
        process ends, even in a crash."""
        key = os.path.normcase(str(session_directory))
        if msvcrt is None or key in self._session_locks:
            return True
        try:
            stream = (session_directory / self.SaveRecoveryLockName).open("a+b")
        except OSError:
            # no lock file (a read-only folder): the session works as before
            return True
        try:
            stream.seek(0)
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            stream.close()
            return False
        self._session_locks[key] = stream
        return True

    def _release_save_quarantine(self, session_directory: Path) -> None:
        stream = self._session_locks.pop(os.path.normcase(str(session_directory)), None)
        if stream is not None:
            try:
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            except OSError:
                pass
            stream.close()
        try:
            (session_directory / self.SaveRecoveryLockName).unlink(missing_ok=True)
        except OSError:
            # still open in another MO2 process
            pass

    def _newest_save_time(self, directory: Path) -> int:
        newest = 0
        try:
            for entry in directory.iterdir():
                if entry.is_file() and entry.name.casefold().startswith(self.SavePrefix):
                    newest = max(newest, entry.stat().st_mtime_ns)
        except OSError:
            pass
        return newest

    @staticmethod
    def _write_save_quarantine_manifest(session_directory: Path, manifest: dict) -> None:
        manifest_path = session_directory / _game_class().SaveRecoveryManifestName
        temporary_path = session_directory / f".{manifest_path.name}.tmp"
        with temporary_path.open("w", encoding="utf-8", newline="\n") as stream:
            json.dump(manifest, stream, ensure_ascii=False, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_path, manifest_path)

    @classmethod
    def _save_directory_contains_elden_ring_saves(cls, directory: Path) -> bool:
        # A SteamID64 folder is an account's save folder even when it only holds
        # files of mods with their own extension (Seamless Co-op .co2,
        # alt-saves/Convergence .mod, a custom save_file_extension), so the name
        # alone is enough; other folders count when they hold Elden Ring saves.
        name = directory.name
        if len(name) == 17 and name.isdigit() and name.startswith("7656119"):
            return True
        for entry in directory.iterdir():
            if not entry.is_file():
                continue
            entry_name = entry.name.casefold()
            if entry_name == "steam_autocloud.vdf" or entry_name.startswith(cls.SavePrefix):
                return True
            if entry_name.endswith((".sl2", ".sl2.bak", ".co2", ".co2.bak", ".err", ".err.bak")):
                return True
        return False

    @classmethod
    def _is_game_launch(cls, app_path: str, args: str = "") -> bool:
        # exact names only: a save tool or another program with a similar
        # name must not hide the Steam saves
        return Path(app_path).name.casefold() in cls.GameLaunchers

    def _restore_global_save_quarantine(
        self, session_directory: Path, global_save_root: Path
    ) -> bool:
        if not session_directory.is_dir():
            return True

        manifest_path = session_directory / self.SaveRecoveryManifestName
        if manifest_path.is_file():
            try:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            except (OSError, UnicodeError, json.JSONDecodeError) as error:
                raise OSError(self._game_text(_tr("The Elden Ring save-recovery record could not be read."))) from error
            if not isinstance(manifest, dict):
                raise OSError(self._game_text(_tr("The Elden Ring save-recovery record has an invalid format.")))
            recorded_root = manifest.get("global_save_root")
            if not isinstance(recorded_root, str) or not recorded_root.strip():
                raise OSError(
                    self._game_text(_tr("The Elden Ring save-recovery record has no valid save-folder path."))
                )
            try:
                same_root = os.path.normcase(os.path.abspath(recorded_root)) == os.path.normcase(
                    os.path.abspath(global_save_root)
                )
            except (OSError, TypeError, ValueError):
                same_root = False
            if not same_root:
                raise OSError(
                    self._game_text(_tr("The save-recovery record points to a different Elden Ring save folder."))
                )
            names = manifest.get("directories", [])
        else:
            # Recover a crash between moving a folder and writing its manifest.
            names = [
                {"name": child.name}
                for child in session_directory.iterdir()
                if child.is_dir()
                and child.name != self.UnroutedSaveDirectoryName
            ]
            manifest = {"global_save_root": str(global_save_root), "directories": names}

        if not isinstance(names, list):
            raise OSError(self._game_text(_tr("The Elden Ring save-recovery record has an invalid directory list.")))

        validated_directories = []
        seen_names = set()
        for item in names:
            name = item.get("name") if isinstance(item, dict) else None
            if (
                not isinstance(name, str)
                or not name
                or name in {".", ".."}
                or any(separator in name for separator in ("/", "\\", ":"))
                or Path(name).is_absolute()
                or Path(name).name != name
            ):
                raise OSError(_tr("The save-recovery record contains an invalid folder name."))
            normalized_name = os.path.normcase(name)
            if normalized_name in seen_names:
                raise OSError(_tr("The save-recovery record contains duplicate folder names."))
            seen_names.add(normalized_name)
            validated_directories.append((item, name))

        swap = manifest.get("swap")
        if isinstance(swap, dict) and not swap.get("returned"):
            # a launch outside the VFS played from the Steam folder: its
            # saves go back to the route before the Steam ones return
            self._swap_route_saves_out(session_directory, manifest, swap, global_save_root)
            swap["returned"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)

        global_save_root.mkdir(parents=True, exist_ok=True)
        for item, name in validated_directories:
            backup_directory = session_directory / name
            if not backup_directory.is_dir():
                continue

            original_directory = global_save_root / name
            if original_directory.exists():
                unrouted_root = session_directory / self.UnroutedSaveDirectoryName
                unrouted_root.mkdir(parents=True, exist_ok=True)
                preserved_directory = unrouted_root / name
                if preserved_directory.exists():
                    preserved_directory = unrouted_root / f"{name}.{uuid.uuid4().hex}"
                # the folder in the way may hold the newer progress (the game
                # started from Steam after a crash, or a save written outside
                # the route): it is kept, and the user is told where
                newer = self._newest_save_time(original_directory) > self._newest_save_time(
                    backup_directory
                )
                os.replace(original_directory, preserved_directory)
                item["set_aside"] = str(preserved_directory)
                if newer:
                    self._displaced_newer_saves.append(preserved_directory)
                    qCritical(
                        "Elden Ring MO2: a global save folder with newer saves "
                        "than the hidden ones appeared during the isolated "
                        f"session; it was kept in '{preserved_directory}' "
                        "before restoring the Steam save."
                    )
                else:
                    qCritical(
                        "Elden Ring MO2: an unexpected global save folder appeared "
                        "during the isolated session; it was preserved in recovery "
                        f"('{preserved_directory}') before restoring the Steam save."
                    )

            os.replace(backup_directory, original_directory)
            item["restored"] = True
            self._write_save_quarantine_manifest(session_directory, manifest)

        temporary_manifest = session_directory / f".{self.SaveRecoveryManifestName}.tmp"
        temporary_manifest.unlink(missing_ok=True)
        self._release_save_quarantine(session_directory)
        if (session_directory / self.UnroutedSaveDirectoryName).is_dir():
            # Preserve unexpected game output for manual inspection.
            return False

        manifest_path.unlink(missing_ok=True)
        try:
            session_directory.rmdir()
        except OSError as error:
            # the saves are all back: an empty folder another MO2 window still
            # holds open (its .owner.lock) is no reason to report a failure
            qWarning(
                "Elden Ring MO2: the saves were restored, but their emptied "
                f"recovery folder could not be removed yet: {error}"
            )
            return True
        try:
            # the install's folder, then the shared one, once they are empty
            session_directory.parent.rmdir()
            session_directory.parent.parent.rmdir()
        except OSError:
            pass
        return True

    def _recover_stale_global_save_quarantines(self) -> bool:
        self._recovery_waiting = False
        self._recovery_waiting_for_game = False
        # whether this call put any session back (a launch may have done it
        # before a queued retry runs)
        self._recovered_sessions = 0
        recovery_root = self._save_quarantine_root()
        sessions: list[Path] = []
        try:
            if recovery_root.is_dir():
                sessions = [
                    session
                    for session in sorted(recovery_root.iterdir())
                    # the session of this process's own launch is restored
                    # when its game closes
                    if session.is_dir()
                    and session != self._active_save_quarantine
                    and self._session_is_pending(session)
                ]
        except OSError as error:
            qCritical(f"Elden Ring MO2: the save-recovery folder could not be read: {error}")
            return False
        sessions += self._orphaned_sessions() + self._stale_foreign_sessions()
        if not sessions:
            self._log_when_changed(self.RecoveryWaitLogKey, [])
            return True

        # a launcher still open (from an instance MO2 just switched away
        # from) may be about to start the game against the hidden saves
        if self._elden_ring_process_state() is not False or self._launcher_running():
            self._recovery_waiting = True
            self._recovery_waiting_for_game = True
            # the retry runs every half second while it waits: the line is
            # written once, when the wait starts
            self._log_when_changed(
                self.RecoveryWaitLogKey,
                [(
                    qCritical,
                    "Elden Ring MO2: prior global saves need recovery, but Elden Ring "
                    "or one of its launchers is running, or its process state cannot "
                    "be confirmed.",
                )],
            )
            return False

        global_save_root = Path(self.savesDirectory().absolutePath())
        for session_directory in sessions:
            if not self._claim_save_quarantine(session_directory):
                self._recovery_waiting = True
                self._log_when_changed(
                    self.RecoveryWaitLogKey,
                    [(
                        qCritical,
                        "Elden Ring MO2: another MO2 window (this installation or "
                        "another one) still holds the hidden Steam saves in "
                        f"'{session_directory}'; it restores them when its game closes.",
                    )],
                )
                return False
            self._recovering_earlier_session = True
            try:
                restored_cleanly = self._restore_global_save_quarantine(
                    session_directory, global_save_root
                )
                self._recovered_sessions += 1
                if not restored_cleanly:
                    qInfo(
                        "Elden Ring MO2: previous Steam saves were restored; "
                        "additional unrouted output remains in the recovery folder."
                    )
            except (OSError, RuntimeError, ValueError) as error:
                qCritical(
                    "Elden Ring MO2: save recovery could not restore a prior "
                    f"session: {error}"
                )
                return False
            finally:
                self._recovering_earlier_session = False
        self._log_when_changed(self.RecoveryWaitLogKey, [])
        return True

    def _launcher_running(self, processes: list[tuple[int, str]] | None = None) -> bool:
        return self._open_launcher(processes) is not None

    def _open_launcher(self, processes: list[tuple[int, str]] | None = None) -> str | None:
        """The executable name of a launcher of this game that is open. A launcher
        still open may yet start the game (an update prompt, a Steam sign-in, a
        slow first start). None too when the process list can't be read: the
        watchers then rely on the game's own state and their time limit.
        A watcher's tick passes the list it has read; without one it is read here."""
        if processes is None:
            processes = _running_processes()
        if processes is None:
            return None
        game_directory = None
        for pid, name in processes:
            if name not in self.GameLaunchers or name == self.GameProcess:
                continue
            if name == self.AntiCheatLauncher:
                image = _process_image_path(pid)
                if image is not None:
                    if game_directory is None:
                        try:
                            # both sides resolved: a game folder reached through
                            # a junction or a short name still matches
                            game_directory = os.path.normcase(
                                os.path.realpath(self.gameDirectory().absolutePath())
                            ).rstrip(os.sep)
                        except Exception:  # no game folder yet: count it
                            return name
                    try:
                        image_path = os.path.normcase(os.path.realpath(image))
                    except (OSError, ValueError):
                        return name
                    if not image_path.startswith(game_directory + os.sep):
                        continue
            return name
        return None

    @classmethod
    def _elden_ring_process_state(
        cls, processes: list[tuple[int, str]] | None = None
    ) -> bool | None:
        # this game's process: eldenring.exe or nightreign.exe. A watcher's tick
        # passes the list it has read; without one it is read here.
        names = (
            {name for _pid, name in processes}
            if processes is not None
            else _running_process_names()
        )
        if names is not None:
            return cls.GameProcess in names
        try:
            result = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {cls.GameProcess}", "/FO", "CSV", "/NH"],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=5,
                check=False,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
        except (OSError, subprocess.SubprocessError):
            return None
        if result.returncode != 0:
            return None
        return any(
            line.lstrip().casefold().startswith(f'"{cls.GameProcess}"')
            for line in result.stdout.splitlines()
        )

    def _restore_quarantine_if_launch_failed(
        self, session_directory: Path, global_save_root: Path
    ) -> None:
        if self._active_save_quarantine != session_directory:
            return
        process_state = self._elden_ring_process_state()
        if process_state is True:
            return
        if process_state is None:
            qCritical(
                "Elden Ring MO2: could not confirm that Elden Ring started; "
                "the hidden Steam saves were left in recovery for startup recovery."
            )
            return
        try:
            self._restore_global_save_quarantine(session_directory, global_save_root)
            self._active_save_quarantine = None
            qInfo(
                "Elden Ring MO2: launch did not start; the temporarily hidden "
                "global saves were restored."
            )
            self._report_displaced_saves()
        except (OSError, RuntimeError, ValueError) as error:
            qCritical(
                "Elden Ring MO2: launch did not start and save restoration "
                f"needs attention: {error}"
            )

    @staticmethod
    def _seed_route_root_files(global_save_root: Path, route: Path) -> None:
        """Copies loose files at the top of the Steam save folder (the game's
        GraphicsConfig.xml) into an isolated route that lacks them. The VFS
        sends only new files to the route: a file that exists just in the
        Steam folder would be opened and rewritten there."""
        try:
            files = [child for child in global_save_root.iterdir() if child.is_file()]
        except OSError:
            return
        for source in files:
            target = route / source.name
            if target.exists():
                continue
            try:
                route.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, target)
                qInfo(
                    f"Elden Ring MO2: copied {source.name} into the save route so "
                    "the game writes the route's copy, not the Steam one."
                )
            except OSError as error:
                qWarning(
                    f"Elden Ring MO2: could not copy {source.name} into the save "
                    f"route; the game may change the Steam copy: {error}"
                )

    def _hide_existing_global_save_directories(
        self, swap_route: Path | None = None, keep_steam_saves: bool = False
    ) -> Path | None:
        """Moves the Steam account folders into a recovery session for the
        launch. With swap_route (a launch outside the VFS) the route's saves
        then take their place, and the session exists even when Steam had no
        saves, so the route gets its saves back after the run."""
        global_save_root = Path(self.savesDirectory().absolutePath())
        if swap_route is not None:
            global_save_root.mkdir(parents=True, exist_ok=True)
        if not global_save_root.is_dir():
            return None
        if not self._recover_stale_global_save_quarantines():
            raise OSError(self._game_text(_tr("A previous Elden Ring save session still needs recovery.")))

        save_directories = [
            child
            for child in global_save_root.iterdir()
            if child.is_dir() and self._save_directory_contains_elden_ring_saves(child)
        ]
        if not save_directories and swap_route is None:
            return None

        recovery_root = self._save_quarantine_root()
        session_directory = recovery_root / uuid.uuid4().hex
        session_directory.mkdir(parents=True, exist_ok=False)
        self._claim_save_quarantine(session_directory)
        manifest = {
            "version": 1,
            "global_save_root": str(global_save_root),
            # other installs block their launches only while this one exists
            "install_root": str(self._install_root()),
            "directories": [{"name": directory.name} for directory in save_directories],
        }
        self._write_save_quarantine_manifest(session_directory, manifest)
        self._active_save_quarantine = session_directory
        try:
            for directory in save_directories:
                os.replace(directory, session_directory / directory.name)
                self._write_save_quarantine_manifest(session_directory, manifest)
            if swap_route is not None:
                self._swap_route_saves_in(
                    session_directory, manifest, global_save_root, swap_route, keep_steam_saves
                )
        except OSError:
            try:
                self._restore_global_save_quarantine(session_directory, global_save_root)
                self._active_save_quarantine = None
            except (OSError, RuntimeError, ValueError) as restore_error:
                qCritical(
                    "Elden Ring MO2: partial save isolation could not be fully "
                    f"rolled back; recovery is required: {restore_error}"
                )
            raise

        qInfo(
            "Elden Ring MO2: temporarily isolated "
            f"{len(save_directories)} existing global save folder(s) for this launch."
        )
        grace = self.LauncherStartGrace if self._launch_via_launcher else 5
        self._launch_pending_until = time.monotonic() + grace
        step = self.LaunchWatchInterval / 1000
        QTimer.singleShot(
            self.LaunchWatchInterval,
            lambda session=session_directory, save_root=global_save_root, left=grace - step: self._watch_launch_start(
                session, save_root, left
            ),
        )
        return session_directory

    def _watch_launch_start(
        self, session_directory: Path, global_save_root: Path, remaining: float
    ) -> None:
        # A launcher can take a while to start eldenring.exe. Until the game
        # shows up, or the time runs out, the Steam saves stay hidden.
        if self._active_save_quarantine != session_directory:
            self._launch_pending_until = 0.0
            return
        processes = _running_processes()
        state = self._elden_ring_process_state(processes)
        if state is True:
            # the game is up: the finish watcher knows it was seen
            self._game_seen_run = self._game_run
            self._launch_pending_until = 0.0
            return
        if state is False and (remaining > 0 or self._launcher_running(processes)):
            if remaining <= 0:
                # keep new launches waiting while the launcher is still open
                self._launch_pending_until = time.monotonic() + self.LaunchWatchInterval / 1000 + 1
            QTimer.singleShot(
                self.LaunchWatchInterval,
                lambda session=session_directory, save_root=global_save_root, left=remaining - self.LaunchWatchInterval / 1000: self._watch_launch_start(
                    session, save_root, left
                ),
            )
            return
        self._launch_pending_until = 0.0
        self._restore_quarantine_if_launch_failed(session_directory, global_save_root)
