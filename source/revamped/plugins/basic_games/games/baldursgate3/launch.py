"""What happens when MO2 starts Baldur's Gate 3 and when the game closes:
the load order read and written, the warnings, the clean-up.

Cancel is safe everywhere: nothing is written and the game doesn't start.
"""

from __future__ import annotations

import html
import shutil
import time
from pathlib import Path
from typing import Any

from PyQt6.QtCore import Qt
from PyQt6.QtWidgets import QApplication, QMessageBox

from . import loadorder, modsettings
from .loadorder import Canceled, Plan, Worker
from .paks import PakCache, scan_mod_paks
from .util import (
    debug_enabled,
    log_debug,
    log_info,
    log_warning,
    runs_the_game,
    tr,
)

LISTED = 15
CRASH_CHECKS_KEPT = 10


def _listing(lines: list[str]) -> str:
    shown = [f"• {html.escape(line)}" for line in lines[:LISTED]]
    if len(lines) > LISTED:
        shown.append(html.escape(tr("• and {0} more").replace("{0}", str(len(lines) - LISTED))))
    return "<br>".join(shown)


class Launch:
    def __init__(self, game) -> None:
        self._game = game
        self.unread: list[Path] = []
        # paks the background read couldn't read, by path, size and date: not
        # cached, so each check listed them as unread again and read them
        # again, forever (a damaged pak, one that needs LSLib)
        self._unreadable: set[tuple[str, int, int]] = set()
        self._background: Worker | None = None
        self.cache = PakCache(game.plugin_data_path / "pak_cache.json")
        self.last_plan: Plan | None = None
        self.written_uuids: list[str] = []
        self.dropped_last_run: list[str] = []

    # ------------------------------------------------------------- reading

    def _active(self) -> list[tuple[str, Path]]:
        return [(m.name(), Path(m.absolutePath())) for m in self._game.active_mods()]

    def _read(self, mods, divine, title: str) -> list[loadorder.ModEntry] | None:
        """read_mods in a worker thread under a progress window; None if the
        user canceled."""
        game = self._game
        progress = game.progress_window(title, max(len(mods), 1))
        state = {"done": 0, "cancel": False}

        def canceled() -> bool:
            return state["cancel"]

        def advance(done: int) -> None:
            state["done"] = done

        worker = Worker(
            lambda: loadorder.read_mods(
                mods,
                self.cache,
                divine,
                autobuild=bool(game.setting("autobuild_paks")),
                canceled=canceled,
                progress=advance,
            )
        )
        worker.start()
        try:
            while worker.is_alive():
                worker.join(0.02)
                if progress is not None:
                    progress.setValue(state["done"])
                    game.process_events()
                    if progress.wasCanceled():
                        state["cancel"] = True
        finally:
            if progress is not None:
                progress.close()
        worker.join()
        if isinstance(worker.error, Canceled) or state["cancel"]:
            return None
        if worker.error is not None:
            raise worker.error
        return worker.result

    def _divine(self):
        divine = self._game.divine()
        return divine.read if divine is not None and divine.available() else None

    def plan(self, entries: list[loadorder.ModEntry]) -> Plan:
        game = self._game
        previous = modsettings.read(game.modsettings_path)
        game_written = modsettings.read(game.real_modsettings_path)
        return loadorder.make_plan(
            entries,
            previous if previous and previous.modules else game_written,
            game_written,
            external_uuids=self.external_uuids(),
            keep_external=bool(game.setting("keep_external_mods")),
            has_script_extender=game.has_script_extender(),
        )

    def external_uuids(self) -> set[str]:
        """The modules of paks put in the real documents' Mods folder outside
        MO2 (by hand, or the in-game mod manager)."""
        folder = Path(self._game.documentsDirectory().absolutePath()) / "Mods"
        uuids: set[str] = set()
        if not folder.is_dir():
            return uuids
        for pak in scan_mod_paks(folder):
            try:
                module = self.cache.info(pak, None).get("module")
            except Exception:  # noqa: BLE001 - an odd pak there isn't MO2's
                continue
            if module:
                uuids.add(module["uuid"].casefold())
        return uuids

    # -------------------------------------------------------- before a run

    def before_run(self, exec_path: str, force_reparse: bool = False) -> bool:
        if not runs_the_game(exec_path):
            return True
        try:
            return self._before_run(force_reparse)
        except Exception as e:  # noqa: BLE001 - a bug here mustn't stop the game
            import traceback

            log_warning(
                "the load order couldn't be made; the game starts with the one it "
                f"had: {e}\n{traceback.format_exc()}"
            )
            return True

    def _before_run(self, force_reparse: bool) -> bool:
        game = self._game
        if force_reparse:
            self.cache.clear()
        self.clear_crash_check()
        mods = self._active()
        try:
            entries = self._read(mods, self._divine(), "Reading the mods' load order")
            if entries is None:
                log_warning("the load order was canceled; the game doesn't start")
                return False
            if not self._fix_lslib_needs(entries):
                return False
            plan = self.plan(entries)
        finally:
            self.cache.save()
        self.last_plan = plan
        if plan.left_out or plan.needs_lslib:
            self._warn_left_out(plan)
        if plan.missing and game.setting("warn_missing_dependencies"):
            if not self._ask_missing(plan):
                log_warning("start canceled over missing dependencies")
                return False
        for line in plan.late:
            log_warning(f"{line[0]} needs {line[1]}, which loads after it")
        for name, dropped, kept in plan.duplicates:
            log_warning(f"{name} is in both {dropped} and {kept}; {kept}'s is used")
        game.modsettings_path.parent.mkdir(parents=True, exist_ok=True)
        text = plan.text()
        loadorder.write(game.modsettings_path, text, game.modsettings_backups)
        self._keep_copy(text)
        self.written_uuids = [m["uuid"].casefold() for m in plan.modules + plan.external]
        # the file mapper of this same start uses the paks found here
        game.mapper().known_paks = {str(e.path): e.paks for e in entries}
        log_info(
            f"load order: {len(plan.modules)} mods from MO2"
            + (f", {len(plan.external)} kept from outside MO2" if plan.external else "")
        )
        return True

    def _fix_lslib_needs(self, entries: list[loadorder.ModEntry]) -> bool:
        """Paks only Divine reads and mod workspaces to pack: LSLib is
        downloaded for them if the user agrees. Returns False on cancel."""
        game = self._game
        unread = [(e, p) for e in entries for p in e.needs_lslib]
        workspaces = [(e, w) for e in entries for w in e.workspaces]
        if not unread and not workspaces:
            return True
        reason = "\n".join(
            [f"{e.name}: {p}" for e, p in unread] + [f"{e.name}: {w.name}/" for e, w in workspaces]
        )
        if not game.lslib().download_lslib_if_missing(reason=reason[:2000]):
            return True  # they stay out; the others load
        divine = game.divine()
        if divine is None or not divine.available():
            return True
        for entry, pak_name in unread:
            pak = next((p for p in entry.paks if p.name == pak_name), None)
            if pak is None:
                continue
            try:
                info = self.cache.info(pak, divine.read)
            except Exception as e:  # noqa: BLE001
                entry.failed.append((pak_name, str(e)))
                continue
            entry.needs_lslib.remove(pak_name)
            module = info.get("module")
            if info.get("kind") == "module" and module:
                entry.modules.append(dict(module, mod=entry.name, pak=pak_name))
        for entry, folder in workspaces:
            self._pack(entry, folder, divine)
        return True

    def _pack(self, entry: loadorder.ModEntry, folder: Path, divine) -> None:
        """A mod workspace packed into Overwrite/Mods (only when its files
        changed), then read like any pak."""
        game = self._game
        if (entry.path / f"{folder.name}.pak").exists() or (
            entry.path / "Mods" / f"{folder.name}.pak"
        ).exists():
            log_info(f"{entry.name}: {folder.name}.pak is there already, {folder} isn't packed")
            return
        pak = loadorder.workspace_pak(game.overwrite_path, entry.name, folder)
        try:
            build = not pak.exists() or loadorder.changed_since(folder, pak.stat().st_mtime)
        except OSError:
            build = True
        if build:
            log_info(f"packing {folder} into {pak}")
            if not divine.pack(folder, pak):
                entry.failed.append((folder.name, "Divine couldn't pack it"))
                return
        try:
            info = self.cache.info(pak, divine.read)
        except Exception as e:  # noqa: BLE001
            entry.failed.append((pak.name, str(e)))
            return
        module = info.get("module")
        if module:
            entry.modules.append(dict(module, mod=entry.name, pak=pak.name))

    def _warn_left_out(self, plan: Plan) -> None:
        lines = [f"{mod} — {why}" for mod, why in plan.left_out] + [
            f"{mod} — {pak} ({tr('needs LSLib')})" for mod, pak in plan.needs_lslib
        ]
        box = QMessageBox(self._game.main_window)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(tr("Some mods aren't in the load order"))
        box.setTextFormat(Qt.TextFormat.RichText)
        box.setText(
            html.escape(
                tr(
                    "These mods' paks couldn't be read, so the game won't load them "
                    "this time. The other mods load normally. If a mod stays out, "
                    "reinstall it."
                )
            )
            + "<br><br>"
            + _listing(lines)
        )
        box.exec()

    def _ask_missing(self, plan: Plan) -> bool:
        lines = [tr("{0} needs {1}").replace("{0}", m).replace("{1}", d) for m, d in plan.missing]
        box = QMessageBox(self._game.main_window)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(tr("Missing mod dependencies"))
        box.setTextFormat(Qt.TextFormat.RichText)
        box.setText(
            html.escape(
                tr(
                    "These active mods need mods that aren't installed or are turned "
                    "off. The game turns such mods off when it starts:"
                )
            )
            + "<br><br>"
            + _listing(lines)
            + "<br><br>"
            + html.escape(tr("Start the game anyway?"))
        )
        start = box.addButton(tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
        cancel = box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(start)
        box.setEscapeButton(cancel)
        box.exec()
        return box.clickedButton() is start

    def _keep_copy(self, text: str) -> None:
        """What MO2 wrote, for the diff after the run (log_diff)."""
        try:
            copy = self._game.plugin_data_path / "temp" / "modsettings.lsx"
            copy.parent.mkdir(parents=True, exist_ok=True)
            copy.write_text(text, encoding="utf-8")
        except OSError:
            pass

    def clear_crash_check(self) -> None:
        """The game's ModCrashSanityCheck folder: when it holds something at
        start, the game blames the last crash on the mods and turns them all
        off (Patch 8). BG3 Mod Manager and the Script Extender clear it too.
        Here what it holds is moved, not deleted, to the plugin's
        crash-checks folder (the last CRASH_CHECKS_KEPT), so a crash can
        still be looked into."""
        game = self._game
        if not game.setting("clear_mod_crash_check"):
            return
        kept = game.plugin_data_path / "crash-checks"
        for where, folder in (
            ("overwrite", game.overwrite_path / "ModCrashSanityCheck"),
            ("documents", Path(game.documentsDirectory().absolutePath()) / "ModCrashSanityCheck"),
        ):
            try:
                children = list(folder.iterdir()) if folder.is_dir() else []
            except OSError:
                children = []
            if not children:
                continue
            target = _free_name(kept / f"{time.strftime('%Y%m%d-%H%M%S')}-{where}")
            try:
                target.mkdir(parents=True, exist_ok=True)
                for child in children:
                    shutil.move(str(child), str(target / child.name))
                log_info(f"moved the game's crash check ({folder}) to {target}")
            except OSError as e:
                log_warning(f"couldn't move {folder} aside: {e}")
        try:
            old = sorted(p for p in kept.iterdir() if p.is_dir())
        except OSError:
            old = []
        for stale in old[:-CRASH_CHECKS_KEPT]:
            shutil.rmtree(stale, ignore_errors=True)

    # ---------------------------------------------------------- after a run

    def after_run(self, exec_path: str) -> None:
        game = self._game
        mapper = game.mapper()
        if not runs_the_game(exec_path):
            # the empty folders the mappings made in Overwrite, after any
            # program; the real Documents folder only after the game
            mapper.remove_empty_folders(game.overwrite_path, documents=False)
            return
        now = modsettings.read(game.modsettings_path)
        self.dropped_last_run = loadorder.dropped_by_game(self.written_uuids, now)
        if self.dropped_last_run:
            names = self._names(self.dropped_last_run)
            log_warning(
                "the game turned off these mods in its load order: " + ", ".join(names)
            )
        if game.setting("log_diff") and debug_enabled():
            self._log_diff()
        self._move_logs()
        self._clean_level_cache()
        mapper.remove_empty_folders(game.overwrite_path)
        try:
            game.invalidate_problems()
        except Exception:  # noqa: BLE001
            pass

    def _names(self, uuids: list[str]) -> list[str]:
        plan = self.last_plan
        known = {}
        if plan is not None:
            known = {m["uuid"].casefold(): m.get("name") or m["uuid"] for m in plan.all_modules()}
        return [known.get(u, u) for u in uuids]

    def _log_diff(self) -> None:
        import difflib

        copy = self._game.plugin_data_path / "temp" / "modsettings.lsx"
        try:
            before = copy.read_text(encoding="utf-8", errors="replace").splitlines()
            after = self._game.modsettings_path.read_text(
                encoding="utf-8", errors="replace"
            ).splitlines()
        except OSError as e:
            log_debug(f"modsettings diff skipped: {e}")
            return
        for line in difflib.unified_diff(
            before, after, fromfile="written by MO2", tofile="after the game", lineterm=""
        ):
            log_debug(line)

    def _move_logs(self) -> None:
        game = self._game
        log_dir = game.log_dir
        overwrite = game.overwrite_path
        moved: dict[str, str] = {}
        try:
            log_dir.mkdir(parents=True, exist_ok=True)
            # *.log and log.txt only (changelog.txt or catalog.txt are a
            # mod's files, not the game's logs)
            found = [
                p
                for p in overwrite.rglob("*")
                if p.is_file() and (p.suffix.casefold() == ".log" or p.name.casefold() == "log.txt")
            ]
        except OSError:
            return
        for path in found:
            name = path.name
            if name.casefold() == "log.txt":
                name = f"{path.parent.name}-{name}"
            dest = _free_name(log_dir / name)
            try:
                path.replace(dest)
                moved[str(path)] = str(dest)
            except OSError as e:
                log_debug(str(e))
        if moved and debug_enabled():
            log_debug(f"moved log files to the logs folder: {moved}")

    def _clean_level_cache(self) -> None:
        days = self._game.setting("delete_levelcache_folders_older_than_x_days")
        if type(days) is not int or days < 0:
            return
        cutoff = time.time() - days * 86400
        for path in self._game.overwrite_path.glob("LevelCache/*"):
            try:
                if path.stat().st_mtime < cutoff:
                    shutil.rmtree(path, ignore_errors=True)
            except OSError:
                pass

    # ---------------------------------------------------- the problem list

    def read_in_background(self) -> bool:
        """Starts reading the paks quick_plan didn't know, in a worker
        thread. Returns whether a read is running (the caller checks again
        later and refreshes the problem list when it's done)."""
        if self._background is not None and self._background.is_alive():
            return True
        self._background = None
        paks = [p for p in self.unread if _stamp(p) not in self._unreadable]
        self.unread = []
        if not paks:
            return False
        unreadable = self._unreadable
        self._background = Worker(lambda: _read_paks(self.cache, paks, unreadable))
        self._background.start()
        return True

    def quick_plan(self) -> Plan | None:
        """The load order as it would be now, for MO2's problem list, from
        what the cache knows: no pak is read on the interface's thread (a big
        list with a cold cache froze MO2). The paks it doesn't know yet are
        in unread, for read_in_background. None if it can't be worked out."""
        try:
            entries = loadorder.read_mods(self._active(), self.cache, None, cached_only=True)
            self.unread = [p for e in entries for p in e.unread]
            plan = self.plan(entries)
        except Exception as e:  # noqa: BLE001 - never break MO2's problem list
            log_warning(f"Baldur's Gate 3 load order check failed: {e}")
            return None
        finally:
            self.cache.save()
        return plan


def _stamp(pak: Path) -> tuple[str, int, int] | None:
    try:
        st = pak.stat()
    except OSError:
        return None
    return (str(pak).lower(), st.st_size, st.st_mtime_ns)


def _read_paks(
    cache: PakCache, paks: list[Path], unreadable: set[tuple[str, int, int]]
) -> None:
    for pak in paks:
        try:
            cache.info(pak, None)
        except Exception:  # noqa: BLE001 - read again when the game starts
            stamp = _stamp(pak)
            if stamp is not None:
                # read again only once it changes (or when the game starts)
                unreadable.add(stamp)
    cache.save()


def _free_name(path: Path) -> Path:
    """path, or path with -1, -2... before its suffix if it's taken: a file
    never replaces another."""
    candidate, n = path, 0
    while candidate.exists():
        n += 1
        candidate = path.with_name(f"{path.stem}-{n}{path.suffix}")
    return candidate


def check_dotnet_before_run(game, exec_path: str) -> bool:
    """BG3 Mod Manager doesn't start without the .NET Desktop Runtime its
    runtimeconfig.json names; say so before it runs, with the link."""
    from . import dotnet

    if Path(exec_path).name.casefold() != dotnet.BG3MM_BINARY:
        return True
    major = dotnet.missing_dotnet_major(Path(exec_path))
    if major is None:
        return True
    from PyQt6.QtWidgets import QLabel

    log_warning(f".NET {major} Desktop Runtime not found, BG3 Mod Manager needs it")
    url = dotnet.DOWNLOAD_URL.format(major)
    box = QMessageBox(game.main_window)
    box.setIcon(QMessageBox.Icon.Warning)
    box.setWindowTitle(tr("BG3 Mod Manager needs .NET {0}").replace("{0}", str(major)))
    text = tr(
        "BG3 Mod Manager needs the .NET {1} Desktop Runtime, which isn't installed "
        "on this PC, so it will close with an error as it starts.\n\nInstall the "
        ".NET Desktop Runtime {1} for Windows x64 from {0}, then run BG3 Mod "
        "Manager again."
    )
    link = f'<a href="{url}">{url}</a>'
    box.setTextFormat(Qt.TextFormat.RichText)
    box.setText(
        html.escape(text).replace("\n", "<br>").replace("{1}", str(major)).replace("{0}", link)
    )
    box.setTextInteractionFlags(Qt.TextInteractionFlag.TextBrowserInteraction)
    run_anyway = box.addButton(tr("Start anyway"), QMessageBox.ButtonRole.AcceptRole)
    cancel = box.addButton(QMessageBox.StandardButton.Cancel)
    box.setDefaultButton(cancel)
    box.setEscapeButton(cancel)
    for label in box.findChildren(QLabel):
        label.setOpenExternalLinks(True)
    box.exec()
    return box.clickedButton() is run_anyway


def process_events() -> None:
    QApplication.processEvents()


def describe(plan: Plan) -> dict[str, Any]:
    """For the tests and the log: the plan in plain data."""
    return {
        "modules": [m["uuid"] for m in plan.modules],
        "external": [m["uuid"] for m in plan.external],
        "missing": plan.missing,
        "late": plan.late,
    }
