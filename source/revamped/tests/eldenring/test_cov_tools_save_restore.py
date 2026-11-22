"""The save tool's backups and restores, the restore an MO2 that closed
halfway left behind, and the choices the new instance wizard queues for a
profile that has not started yet (eldenring_mo2_tools/save_isolation.py)."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from unittest import mock

from eldenring_mo2_tools import save_isolation

from test_cov_tools_save_routes import ACCOUNT, ToolCase, write

Tool = save_isolation.EldenRingMo2SaveIsolation


def files_of(root: Path) -> dict[str, bytes]:
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


class BackupNames(ToolCase):
    def test_backup_roots(self):
        route = self.profile / "saves"
        self.assertEqual(Tool._auto_backup_root(route, self.organizer.base, False),
                         self.profile / "saves Backups" / "auto")
        self.assertEqual(Tool._auto_backup_root(route, self.organizer.base, True),
                         self.organizer.base / "Elden Ring Global Save Backups" / "auto")

    def test_age_key_orders_by_time_then_suffix(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cov-backups-"))
        first = folder / "20261101-120000"
        second = folder / "20261101-120000-3"
        first.mkdir()
        second.mkdir()
        os.utime(first, ns=(1, 1))
        os.utime(second, ns=(1, 1))
        self.assertLess(Tool._backup_age_key(first), Tool._backup_age_key(second))
        self.assertEqual(Tool._backup_age_key(first)[2], 1)
        self.assertEqual(Tool._backup_age_key(second)[2], 3)
        # a folder gone meanwhile sorts first, it is not an error
        self.assertEqual(Tool._backup_age_key(folder / "20261101-130000-x")[0], 0)

    def test_label(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cov-backups-")) / "20261101-120000"
        write(folder / ACCOUNT / "ER0000.sl2", b"x" * 1024)
        write(folder / ACCOUNT / "ER0000.sl2.bak", b"x")
        label = Tool._backup_label(folder)
        self.assertIn("2026-11-01 12:00:00", label)
        self.assertIn("2 file(s)", label)
        self.assertIn(ACCOUNT, label)
        odd = folder.parent / "hand made"
        write(odd / "ER0000.sl2")
        label = Tool._backup_label(odd)
        self.assertIn("hand made", label)
        self.assertIn("—", label)


class RestoreBackup(ToolCase):
    def setUp(self):
        super().setUp()
        self.route = self.profile / "saves"
        self.replaced_root = self.profile / "saves Backups" / "replaced"
        self.backup = self.profile / "saves Backups" / "auto" / "20261101-120000"
        write(self.backup / ACCOUNT / "ER0000.sl2", b"backup")

    def test_only_the_backup_families_are_replaced(self):
        write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        write(self.route / ACCOUNT / "ER0000.sl2.bak", b"old-bak")
        write(self.route / ACCOUNT / "ER0000.co2", b"coop")
        write(self.route / ACCOUNT / "notes.txt", b"mine")
        replaced = self.tool._restore_backup(self.backup, self.route, self.replaced_root)
        self.assertEqual(files_of(self.route), {
            f"{ACCOUNT}/ER0000.co2": b"coop",
            f"{ACCOUNT}/ER0000.sl2": b"backup",
            f"{ACCOUNT}/notes.txt": b"mine",
        })
        # the replaced save and its old .bak are kept, the marker is gone
        self.assertEqual(files_of(replaced), {
            f"{ACCOUNT}/ER0000.sl2": b"old",
            f"{ACCOUNT}/ER0000.sl2.bak": b"old-bak",
        })

    def test_into_an_empty_route(self):
        self.assertIsNone(self.tool._restore_backup(self.backup, self.route, self.replaced_root))
        self.assertEqual(files_of(self.route), {f"{ACCOUNT}/ER0000.sl2": b"backup"})
        # the folder made only for the marker is gone
        self.assertEqual(list(self.replaced_root.iterdir()), [])

    def test_two_restores_in_one_second_get_their_own_folders(self):
        write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        first = self.tool._restore_backup(self.backup, self.route, self.replaced_root)
        write(self.backup / ACCOUNT / "ER0000.sl2", b"backup2")
        with mock.patch.object(save_isolation, "datetime") as clock:
            clock.now.return_value = datetime.strptime(first.name[:15], "%Y%m%d-%H%M%S")
            second = self.tool._restore_backup(self.backup, self.route, self.replaced_root)
        self.assertNotEqual(first, second)
        self.assertEqual(second.name, first.name[:15] + "-2")

    def test_empty_backup(self):
        empty = self.backup.parent / "20261101-130000"
        (empty / ACCOUNT).mkdir(parents=True)
        with self.assertRaises(OSError):
            self.tool._restore_backup(empty, self.route, self.replaced_root)

    def test_failure_puts_the_route_back(self):
        write(self.backup / "76561198000000002" / "ER0000.sl2", b"backup-2")
        write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        write(self.route / "76561198000000002" / "ER0000.sl2", b"old-2")
        before = files_of(self.route)
        real_replace = os.replace
        calls = []

        def replace(source, target):
            # the first call puts the marker in place, the next ones the saves
            calls.append(target)
            if len(calls) == 3:
                raise PermissionError("locked by the game")
            return real_replace(source, target)

        with mock.patch.object(save_isolation.os, "replace", side_effect=replace):
            with self.assertRaises(PermissionError):
                self.tool._restore_backup(self.backup, self.route, self.replaced_root)
        self.assertEqual(files_of(self.route), before)
        # nothing to finish later: no marker, no copies
        self.assertEqual(self.tool._interrupted_restores(self.replaced_root), [])

    def test_failed_rollback_leaves_a_rolled_back_marker(self):
        write(self.backup / "76561198000000002" / "ER0000.sl2", b"backup-2")
        write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        write(self.route / "76561198000000002" / "ER0000.sl2", b"old-2")
        real_replace, real_copy = os.replace, shutil.copy2
        state = {"replaces": 0, "failed": False}

        def replace(source, target):
            state["replaces"] += 1
            if state["replaces"] == 3:
                state["failed"] = True
                raise PermissionError("locked by the game")
            return real_replace(source, target)

        def copy(source, target, *args, **kwargs):
            if state["failed"]:
                raise PermissionError("antivirus")
            return real_copy(source, target, *args, **kwargs)

        with mock.patch.object(save_isolation.os, "replace", side_effect=replace), \
                mock.patch.object(save_isolation.shutil, "copy2", side_effect=copy):
            with self.assertRaises(OSError) as raised:
                self.tool._restore_backup(self.backup, self.route, self.replaced_root)
        markers = self.tool._interrupted_restores(self.replaced_root)
        self.assertEqual(len(markers), 1)
        self.assertIn(str(markers[0].parent), str(raised.exception))
        record = json.loads(markers[0].read_text(encoding="utf-8"))
        self.assertTrue(Tool._marker_rolled_back(record))
        # a rolled-back restore is never finished
        self.assertFalse(self.tool._finish_interrupted_restore(markers[0], self.route))


class InterruptedRestore(ToolCase):
    """A marker as _restore_backup writes it, with MO2 gone before any copy
    took its place."""

    def setUp(self):
        super().setUp()
        self.route = self.profile / "saves"
        self.destination = write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        self.old_bak = write(self.route / ACCOUNT / "ER0000.sl2.bak", b"old-bak")
        self.temporary = write(self.route / ACCOUNT / ".ER0000.sl2.restoring", b"new")
        self.replaced = self.profile / "saves Backups" / "replaced" / "20261101-120000"
        self.kept = write(self.replaced / ACCOUNT / "ER0000.sl2", b"old")
        self.kept_bak = write(self.replaced / ACCOUNT / "ER0000.sl2.bak", b"old-bak")
        self.marker = self.tool._write_restore_marker(
            self.replaced, self.route, [(self.temporary, self.destination)],
            [(self.old_bak, self.kept_bak)],
        )

    def edit_marker(self, change) -> None:
        record = json.loads(self.marker.read_text(encoding="utf-8"))
        change(record)
        self.marker.write_text(json.dumps(record), encoding="utf-8")

    def test_finished_as_it_would_have_gone_on(self):
        self.assertTrue(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.assertEqual(files_of(self.route), {f"{ACCOUNT}/ER0000.sl2": b"new"})
        self.assertFalse(self.marker.exists())
        self.assertTrue(self.kept.is_file())

    def test_a_save_written_since_stops_it(self):
        self.destination.write_bytes(b"newer progress")
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.assertEqual(self.destination.read_bytes(), b"newer progress")
        self.assertTrue(self.marker.exists())

    def test_a_changed_copy_stops_it(self):
        self.temporary.write_bytes(b"other")
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))

    def test_a_bak_changed_since_stops_it(self):
        self.old_bak.write_bytes(b"changed")
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.assertTrue(self.temporary.exists())

    def test_steps_already_done_with_copies_left(self):
        shutil.copy2(self.temporary, self.destination)
        self.old_bak.unlink()
        self.assertTrue(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.assertFalse(self.temporary.exists())
        self.assertEqual(self.destination.read_bytes(), b"new")

    def test_step_done_with_another_file_in_the_copy_place(self):
        shutil.copy2(self.temporary, self.destination)
        self.temporary.write_bytes(b"something else")
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))

    def test_destination_missing_is_still_finished(self):
        self.destination.unlink()
        self.assertTrue(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.assertEqual(self.destination.read_bytes(), b"new")

    def test_original_not_kept_stops_it(self):
        self.kept.unlink()
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))

    def test_old_or_rolled_back_markers_are_never_finished(self):
        self.edit_marker(lambda record: record.update(version=1))
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))
        self.edit_marker(lambda record: record.update(version=2, state="rolled_back"))
        self.assertFalse(self.tool._finish_interrupted_restore(self.marker, self.route))

    def test_records_pointing_outside_the_route_are_refused(self):
        for bad in ("../outside.sl2", "C:/Windows/x.sl2", "C:x.sl2", "", ".", "/home/user/ER0000.sl2",
                    "\\\\server\\share\\ER0000.sl2"):
            self.assertIsNone(Tool._inside(self.route, bad), bad)
        self.assertEqual(Tool._inside(self.route, f"{ACCOUNT}/ER0000.sl2"), self.route / ACCOUNT / "ER0000.sl2")
        self.edit_marker(lambda record: record["steps"][0].update(destination="../../x.sl2"))
        with self.assertRaises(ValueError):
            self.tool._finish_interrupted_restore(self.marker, self.route)
        self.edit_marker(lambda record: record["steps"][0].update(destination=f"{ACCOUNT}/ER0000.sl2"))
        self.edit_marker(lambda record: record.update(remove=[["/home/user/x", "x"]]))
        with self.assertRaises(ValueError):
            self.tool._finish_interrupted_restore(self.marker, self.route)

    def test_steps_of_both_formats(self):
        record = json.loads(self.marker.read_text(encoding="utf-8"))
        steps = self.tool._marker_steps(record, self.route)
        self.assertEqual(steps[0][:2], (self.temporary, self.destination))
        older = {"staged": [[f"{ACCOUNT}/.ER0000.sl2.restoring", f"{ACCOUNT}/ER0000.sl2"]]}
        self.assertEqual(self.tool._marker_steps(older, self.route)[0][2], {})
        with self.assertRaises(ValueError):
            self.tool._marker_steps({"steps": ["not a step"]}, self.route)

    def test_leave_it_drops_the_copies_and_the_marker(self):
        self.tool._leave_interrupted_restore(self.marker, self.route)
        self.assertFalse(self.temporary.exists())
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.destination.read_bytes(), b"old")
        self.assertTrue(self.kept.exists())

    def test_retire_a_rolled_back_marker(self):
        self.tool._mark_restore_rolled_back(self.marker)
        self.tool._retire_rolled_back_marker(self.marker)
        self.assertFalse(self.marker.exists())
        self.assertTrue((self.replaced / ".restore-rolled-back.json").is_file())
        self.assertEqual(self.tool._interrupted_restores(self.replaced.parent), [])

    def test_listing(self):
        self.assertEqual(self.tool._interrupted_restores(self.replaced.parent), [self.marker])
        self.assertEqual(self.tool._interrupted_restores(self.replaced.parent / "missing"), [])
        found = self.tool._collect_restore_markers(
            [(self.route, self.replaced.parent), (self.route / "reforged_online", self.replaced.parent)]
        )
        self.assertEqual(len(found), 1)
        self.assertEqual(len(found[0][1]), 2)

    def test_route_of_a_marker(self):
        online = self.route / "reforged_online"
        record = {"route": str(self.route)}
        self.assertEqual(self.tool._marker_route(record, [online, self.route]), self.route)
        # the instance moved: the route of the same kind
        self.assertEqual(self.tool._marker_route({"route": r"D:\old\saves"}, [online, self.route]), self.route)
        self.assertEqual(self.tool._marker_route({"route": r"D:\old\saves\reforged_online"}, [self.route, online]),
                         online)
        self.assertEqual(self.tool._marker_route({}, [online]), online)

    def test_marker_folder_cleared(self):
        Tool._remove_restore_marker(None, None)
        empty = self.profile / "empty-replaced"
        empty.mkdir()
        marker = write(empty / Tool.RestoreMarkerName, "{}")
        Tool._remove_restore_marker(marker, empty)
        self.assertFalse(empty.exists())
        # a folder that still holds files stays, quietly
        Tool._remove_restore_marker(self.marker, self.replaced)
        self.assertTrue(self.replaced.exists())

    def test_fingerprint_of_a_missing_or_odd_step(self):
        self.assertFalse(Tool._matches_fingerprint(self.destination, {}))
        self.assertFalse(Tool._matches_fingerprint(self.destination, {"size": "x", "sha256": ""}))
        self.assertFalse(Tool._matches_fingerprint(self.route / "missing", {"size": 1, "sha256": ""}))


class StrayCopies(ToolCase):
    def test_only_old_unreferenced_copies_go(self):
        route = self.profile / "saves"
        old = write(route / ACCOUNT / ".ER0000.sl2.restoring")
        referenced = write(route / ACCOUNT / ".ER0000.co2.restoring")
        other = write(route / ACCOUNT / ".notes.restoring")
        later = datetime.now() + timedelta(hours=1)
        with mock.patch.object(save_isolation, "datetime") as clock:
            clock.now.return_value = later
            self.tool._sweep_stray_restore_copies(route, {os.path.normcase(str(referenced))})
        self.assertFalse(old.exists())
        self.assertTrue(referenced.exists())
        self.assertTrue(other.exists())
        # one made a moment ago may belong to a restore in another MO2 window
        fresh = write(route / ".ER0000.sl2.restoring")
        self.tool._sweep_stray_restore_copies(route, set())
        self.assertTrue(fresh.exists())
        self.tool._sweep_stray_restore_copies(route / "missing", set())


class OfferToFinish(ToolCase):
    def setUp(self):
        super().setUp()
        self.route = self.profile / "saves"
        self.replaced_root = self.profile / "saves Backups" / "replaced"
        self.destination = write(self.route / ACCOUNT / "ER0000.sl2", b"old")
        self.temporary = write(self.route / ACCOUNT / ".ER0000.sl2.restoring", b"new")
        replaced = self.replaced_root / "20261101-120000"
        write(replaced / ACCOUNT / "ER0000.sl2", b"old")
        self.marker = self.tool._write_restore_marker(
            replaced, self.route, [(self.temporary, self.destination)], []
        )
        self.opened = []
        services = mock.patch.object(save_isolation, "QDesktopServices")
        services.start().openUrl.side_effect = self.opened.append
        self.addCleanup(services.stop)

    def test_finished_on_request(self):
        self.answers = [True]
        self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertEqual(self.destination.read_bytes(), b"new")
        self.assertEqual(self.notices[-1][2], "success")

    def test_not_now_leaves_it(self):
        self.answers = [False]
        self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertTrue(self.marker.exists())
        self.assertEqual(self.notices, [])

    def test_unconfirmed_files_are_left_as_they_are(self):
        self.destination.write_bytes(b"newer")
        self.answers = [True]
        self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertEqual(self.destination.read_bytes(), b"newer")
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.temporary.exists())
        self.assertEqual(self.notices[-1][2], "warning")

    def test_busy_game_stops_it(self):
        self.answers = [True]
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertTrue(self.marker.exists())
        self.assertEqual(self.notices[-1][0], "Restore not finished")

    def test_rolled_back_only_opens_the_folder(self):
        self.tool._mark_restore_rolled_back(self.marker)
        self.answers = [True]
        self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertEqual(len(self.opened), 1)
        self.assertFalse(self.marker.exists())
        self.assertEqual(self.destination.read_bytes(), b"old")

    def test_unreadable_marker_keeps_every_copy(self):
        self.marker.write_bytes(b"\xff not json")
        stray = write(self.route / ACCOUNT / ".ER0000.co2.restoring")
        later = datetime.now() + timedelta(hours=1)
        with mock.patch.object(save_isolation, "datetime") as clock:
            clock.now.return_value = later
            self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertTrue(stray.exists())
        self.assertTrue(self.temporary.exists())

    def test_a_record_that_fails_is_reported(self):
        record = json.loads(self.marker.read_text(encoding="utf-8"))
        record["remove"] = [["../x", "y"]]
        self.marker.write_text(json.dumps(record), encoding="utf-8")
        self.answers = [True]
        self.tool._offer_to_finish_restores(None, [(self.route, self.replaced_root)])
        self.assertEqual(self.notices[-1][0], "Restore not finished")
        self.assertTrue(self.marker.exists())


class PendingModes(ToolCase):
    def setUp(self):
        super().setUp()
        self.source_root = Path(tempfile.mkdtemp(prefix="er-cov-start-"))
        self.source = write(self.source_root / ACCOUNT / "ER0000.sl2", b"start")
        write(self.source_root / ACCOUNT / "ER0000.sl2.bak", b"start-bak")

    def item(self, **extra) -> dict:
        entry = {
            "profile_path": str(self.profile),
            "mode": Tool.ProfileIsolated,
            "save_source_path": str(self.source),
            "save_relative_path": f"{ACCOUNT}/ER0000.sl2",
        }
        entry.update(extra)
        return entry

    def test_queue_lives_outside_the_instance(self):
        self.assertEqual(Tool._pending_modes_path(),
                         self.local / "MO2 Revamped" / "EldenRingMO2SaveIsolation" / Tool.PendingModesFilename)
        os.environ.pop("LOCALAPPDATA")
        self.assertEqual(Tool._pending_modes_path().name, Tool.PendingModesFilename)
        self.assertIsNone(Tool._legacy_pending_modes_path(None))

    def test_queue_read_and_discard(self):
        self.tool._queue_pending_mode(self.profile, Tool.InstanceShared, str(self.source),
                                      f"{ACCOUNT}/ER0000.sl2", "  Run  ", "sl2,co2")
        # queued again for the same profile: one entry
        self.tool._queue_pending_mode(self.profile, Tool.GlobalShared)
        entries = self.tool._read_pending_modes(self.profile)
        self.assertEqual(entries, [{"profile_path": str(self.profile), "mode": Tool.GlobalShared}])
        self.tool._queue_pending_mode(self.profile, Tool.InstanceShared, game_save_profile_name="  Run  ",
                                      save_formats="sl2")
        self.assertEqual(self.tool._read_pending_modes(self.profile)[0]["game_save_profile_name"], "Run")
        with self.assertRaises(ValueError):
            self.tool._queue_pending_mode(self.profile, "cloud")
        self.tool._discard_pending_mode(self.profile)
        self.assertEqual(self.tool._read_pending_modes(self.profile), [])

    def test_legacy_queue_is_merged_and_removed(self):
        legacy = Tool._legacy_pending_modes_path(self.profile)
        other = str(self.profile.parent / "Other")
        write(legacy, json.dumps([
            {"profile_path": str(self.profile), "mode": Tool.GlobalShared},
            {"profile_path": other, "mode": Tool.InstanceShared},
        ]))
        self.tool._queue_pending_mode(self.profile, Tool.ProfileIsolated)
        entries = {item["profile_path"]: item["mode"] for item in self.tool._read_pending_modes(self.profile)}
        self.assertEqual(entries, {str(self.profile): Tool.ProfileIsolated, other: Tool.InstanceShared})
        self.assertFalse(legacy.exists())
        self.assertFalse(legacy.parent.exists())

    def test_broken_queues_are_empty(self):
        path = Tool._pending_modes_path()
        for data in (b"{bad", b"\xff\xfe", b'{"a": 1}',
                     json.dumps([1, {"profile_path": 3, "mode": "profile"},
                                 {"profile_path": "x", "mode": "cloud"}]).encode()):
            write(path, data)
            self.assertEqual(self.tool._read_pending_modes(), [], data)

    def test_source_root_of_a_queued_save(self):
        self.assertEqual(Tool._normalized_path(Tool._queued_save_source_root(self.item())),
                         Tool._normalized_path(self.source_root))
        # Wine paths with forward slashes and another case
        item = self.item(save_source_path=self.source.as_posix(), save_relative_path=f"{ACCOUNT}/er0000.SL2")
        self.assertEqual(Tool._normalized_path(Tool._queued_save_source_root(item)),
                         Tool._normalized_path(self.source_root))
        for relative in ("../ER0000.sl2", "ER0000.txt", "", str(self.source)):
            with self.assertRaises(ValueError, msg=relative):
                Tool._queued_save_source_root(self.item(save_relative_path=relative))
        with self.assertRaises(FileNotFoundError):
            Tool._queued_save_source_root(self.item(save_source_path=str(self.source_root / "gone.sl2")))
        with self.assertRaises(FileNotFoundError):
            Tool._queued_save_source_root(self.item(save_relative_path=f"{ACCOUNT}/ER0000.co2"))
        with self.assertRaises(ValueError):
            Tool._queued_save_source_root(self.item(save_relative_path="other/ER0000.sl2"))
        deep = "/".join(["a"] * 80) + "/ER0000.sl2"
        with self.assertRaises(ValueError):
            Tool._queued_save_source_root(self.item(save_relative_path=deep))

    def test_copy_into_the_profile_with_its_bak(self):
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated, self.item()),
                         "copied")
        destination = self.profile / "saves" / ACCOUNT
        self.assertEqual((destination / "ER0000.sl2").read_bytes(), b"start")
        self.assertEqual((destination / "ER0000.sl2.bak").read_bytes(), b"start-bak")
        # again: the same save is there already
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated, self.item()),
                         "identical")
        (destination / "ER0000.sl2").write_bytes(b"another character")
        with self.assertRaises(FileExistsError):
            self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated, self.item())
        self.assertEqual([p.name for p in destination.iterdir() if p.name.endswith(".copying")], [])

    def test_copy_to_the_other_routes(self):
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.GlobalShared, self.item()),
                         "global")
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.InstanceShared, self.item()),
                         "copied")
        shared = self.organizer.base / "Elden Ring Shared Saves" / ACCOUNT / "ER0000.sl2"
        self.assertEqual(shared.read_bytes(), b"start")
        # the save already is the destination
        item = self.item(save_source_path=str(shared))
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.InstanceShared, item), "same")

    def test_copy_refuses_odd_items(self):
        for relative in ("../ER0000.sl2", "ER0000.txt"):
            with self.assertRaises(ValueError):
                self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated,
                                                       self.item(save_relative_path=relative))
        with self.assertRaises(FileNotFoundError):
            self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated,
                                                   self.item(save_source_path=str(self.source_root / "x.sl2")))
        # a folder where the save would go
        (self.profile / "saves" / ACCOUNT / "ER0000.sl2").mkdir(parents=True)
        with self.assertRaises(FileExistsError):
            self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated, self.item())

    def test_copy_never_leaves_the_route(self):
        # a rooted path (\account\ER0000.sl2 on Windows, /account/... from a
        # Linux tool) would join to the drive's root
        for relative in ("/x/ER0000.sl2", "\\x\\ER0000.sl2"):
            with self.assertRaises(ValueError, msg=relative):
                self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated,
                                                       self.item(save_relative_path=relative))

    def test_an_old_bak_of_another_character_is_moved_aside(self):
        destination = self.profile / "saves" / ACCOUNT
        write(destination / "ER0000.sl2.bak", b"someone else")
        self.assertEqual(self.tool._copy_queued_save_to_profile(self.profile, Tool.ProfileIsolated, self.item()),
                         "copied")
        self.assertEqual((destination / "ER0000.sl2.bak").read_bytes(), b"start-bak")
        kept = list((self.profile / "saves Backups" / "replaced").rglob("ER0000.sl2.bak"))
        self.assertEqual([p.read_bytes() for p in kept], [b"someone else"])

    def test_bak_kept_without_a_route_or_when_it_is_the_same(self):
        destination = write(self.profile / "x" / "ER0000.sl2", b"start")
        bak = write(self.profile / "x" / "ER0000.sl2.bak", b"other")
        self.tool._copy_queued_save_bak(self.source, destination, None)
        self.assertEqual(bak.read_bytes(), b"other")
        bak.write_bytes(b"start-bak")
        self.tool._copy_queued_save_bak(self.source, destination, self.profile / "x")
        self.assertEqual(bak.read_bytes(), b"start-bak")
        # no .bak beside the source: nothing happens
        lone = write(self.source_root / "lone" / "ER0000.sl2")
        self.tool._copy_queued_save_bak(lone, self.profile / "y" / "ER0000.sl2", self.profile / "y")
        self.assertFalse((self.profile / "y").exists())

    def test_apply_copies_the_starting_save(self):
        write(Tool._pending_modes_path(), json.dumps([self.item(save_formats="sl2"),
                                                       {"profile_path": "C:/other", "mode": "global"}]))
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual((self.profile / "saves" / ACCOUNT / "ER0000.sl2").read_bytes(), b"start")
        self.assertIn("formats = sl2", (self.profile / Tool.ConfigFilename).read_text(encoding="utf-8"))
        # the other profile's choice stays queued
        self.assertEqual([item["profile_path"] for item in self.tool._read_pending_modes()], ["C:/other"])
        # applied once per session
        write(Tool._pending_modes_path(), json.dumps([self.item(mode=Tool.GlobalShared)]))
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.ProfileIsolated)
        self.assertEqual(self.tool._read_pending_modes(), [])

    def test_apply_with_a_named_game_save_profile(self):
        write(Tool._pending_modes_path(), json.dumps([self.item(game_save_profile_name="Run")]))
        self.tool._apply_pending_mode_to_active_profile()
        profiles = Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated)
        self.assertEqual([name for _id, name in profiles], ["Run"])
        profile_id = profiles[0][0]
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.ProfileIsolated), profile_id)
        saves = self.profile / "save_profiles" / profile_id / ACCOUNT
        self.assertEqual((saves / "ER0000.sl2").read_bytes(), b"start")

    def test_apply_reuses_a_named_profile_of_that_name(self):
        existing = self.tool._create_named_save_profile(self.profile, Tool.InstanceShared, "Run")
        write(Tool._pending_modes_path(), json.dumps([self.item(mode=Tool.InstanceShared,
                                                                game_save_profile_name="RUN")]))
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.InstanceShared), existing)
        self.assertEqual(len(Tool._read_named_save_profiles(self.profile, Tool.InstanceShared)), 1)

    def test_apply_with_a_lost_save_still_makes_the_profile(self):
        write(Tool._pending_modes_path(), json.dumps([
            self.item(game_save_profile_name="Run", save_source_path=str(self.source_root / "gone.sl2"))
        ]))
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(len(Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated)), 1)
        self.assertEqual(self.tool._read_pending_modes(), [])

    def test_apply_global_with_a_name_uses_the_steam_route(self):
        write(Tool._pending_modes_path(), json.dumps([self.item(mode=Tool.GlobalShared,
                                                                game_save_profile_name="Run")]))
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.GlobalShared)
        self.assertFalse((self.appdata / "EldenRing").exists())

    def test_apply_a_copy_that_fails(self):
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"another character")
        write(Tool._pending_modes_path(), json.dumps([self.item()]))
        self.tool._apply_pending_mode_to_active_profile()
        # the route is applied, the different save is left as it was
        self.assertEqual((self.profile / "saves" / ACCOUNT / "ER0000.sl2").read_bytes(), b"another character")
        self.assertEqual(self.tool._read_pending_modes(), [])

    def test_apply_waits_while_the_game_runs(self):
        write(Tool._pending_modes_path(), json.dumps([self.item(mode=Tool.InstanceShared)]))
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.tool._apply_pending_mode_to_active_profile()
            # told once per profile
            self.tool._apply_pending_mode_to_active_profile()
        # the route is chosen now, the copy waits in the queue
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.InstanceShared)
        self.assertEqual(len(self.tool._read_pending_modes()), 1)
        self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(self.tool._read_pending_modes(), [])
        self.assertTrue((self.organizer.base / "Elden Ring Shared Saves" / ACCOUNT / "ER0000.sl2").is_file())

    def test_apply_that_fails_keeps_the_item(self):
        write(Tool._pending_modes_path(), json.dumps([self.item()]))
        with mock.patch.object(Tool, "_save_mode_directory", side_effect=PermissionError("locked")):
            self.tool._apply_pending_mode_to_active_profile()
            self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(len(self.tool._read_pending_modes()), 1)

    def test_apply_without_a_profile(self):
        self.organizer._profile = None
        self.tool._apply_pending_mode_to_active_profile()

    def test_a_queue_that_cannot_be_written_is_tried_once(self):
        write(Tool._pending_modes_path(), json.dumps([self.item(save_source_path="", save_relative_path="")]))
        with mock.patch.object(Tool, "_write_pending_modes", side_effect=PermissionError("read-only")) as writer:
            self.tool._apply_pending_mode_to_active_profile()
            self.tool._apply_pending_mode_to_active_profile()
        self.assertEqual(writer.call_count, 1)


if __name__ == "__main__":
    unittest.main()
