"""The save tool (eldenring_mo2_tools/save_isolation.py) outside MO2: the
route each profile plays from, its stored settings, named game save
profiles, the save lists and the instance's launch file. Only folders under
a temporary APPDATA and LOCALAPPDATA; no window opens (message boxes are
replaced on the tool)."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import time
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtWidgets import QComboBox

from eldenring_mo2_tools import save_isolation

from fake_organizer import FakeOrganizer

Tool = save_isolation.EldenRingMo2SaveIsolation
Diagnose = save_isolation.EldenRingMo2SaveDiagnose

ACCOUNT = "76561198000000001"
PROFILE_ID = "0123456789abcdef0123456789abcdef"


class FakeDirectory:
    def __init__(self, path: Path):
        self._path = path

    def absolutePath(self) -> str:
        return str(self._path)


class FakeGame:
    """Only what the save tool asks the managed game."""

    def __init__(self, short_name: str = "eldenring", folder: Path | None = None):
        self._short = short_name
        self._folder = folder or Path(tempfile.mkdtemp(prefix="er-game-"))

    def gameShortName(self) -> str:
        return self._short

    def dataDirectory(self):
        return FakeDirectory(self._folder / "Game")

    def gameDirectory(self):
        return FakeDirectory(self._folder)


def write(path: Path, data: bytes | str = b"save") -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data, encoding="utf-8")
    else:
        path.write_bytes(data)
    return path


class ToolCase(unittest.TestCase):
    """A tool initialized on a fake instance, with its own APPDATA and
    LOCALAPPDATA, and the message boxes recorded instead of shown."""

    nightreign = False

    def setUp(self):
        self._environment = {key: os.environ.get(key) for key in ("APPDATA", "LOCALAPPDATA")}
        self.appdata = Path(tempfile.mkdtemp(prefix="er-cov-appdata-"))
        self.local = Path(tempfile.mkdtemp(prefix="er-cov-local-"))
        os.environ["APPDATA"] = str(self.appdata)
        os.environ["LOCALAPPDATA"] = str(self.local)
        self.organizer = FakeOrganizer()
        (self.organizer.profile_dir / "modlist.txt").write_text("", encoding="utf-8")
        self.organizer.managed = FakeGame("nightreign" if self.nightreign else "eldenring",
                                          self.organizer.game_dir)
        self.tool = Tool()
        self.assertTrue(self.tool.init(self.organizer))
        self.assertTrue(self.tool._active_instance_is_elden_ring())
        self.notices: list[tuple[str, str, str]] = []
        self.answers: list[bool] = []
        self.tool._show_notice = lambda parent, title, message, kind="info": self.notices.append(
            (title, message, kind)
        )
        self.tool._ask_confirmation = lambda *args, **kwargs: (
            self.answers.pop(0) if self.answers else False
        )
        # no game running, whatever runs on this PC
        self.real_process_names = save_isolation._running_process_names
        patcher = mock.patch.object(save_isolation, "_running_process_names", return_value={"explorer.exe"})
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        Tool._game_key = "eldenring"
        Tool._wizard_text_game = None
        Tool._path_instance_base = None
        for key, value in self._environment.items():
            if value is None:
                os.environ.pop(key, None)
            else:
                os.environ[key] = value

    @property
    def steam(self) -> Path:
        return self.appdata / ("Nightreign" if self.nightreign else "EldenRing")

    @property
    def profile(self) -> Path:
        return self.organizer.profile_dir

    def named_profile(self, root: Path, profile_id: str, name: str) -> Path:
        folder = root / profile_id
        folder.mkdir(parents=True, exist_ok=True)
        write(root / f"{profile_id}.json", json.dumps({"id": profile_id, "name": name}))
        return folder


class Texts(ToolCase):
    def test_nightreign_names_replace_elden_ring(self):
        Tool._game_key = "nightreign"
        self.assertEqual(save_isolation._tr("Close Elden Ring and eldenring.exe"),
                         "Close Nightreign and nightreign.exe")
        self.assertIn("NR0000", save_isolation._trs("ER0000.sl2"))
        Tool._game_key = "eldenring"
        self.assertEqual(save_isolation._tr("Elden Ring"), "Elden Ring")

    def test_wizard_game_wins_for_its_texts(self):
        Tool._wizard_text_game = "nightreign"
        self.assertEqual(save_isolation._tr("Elden Ring"), "Nightreign")

    def test_menu_path_stays_on_one_line(self):
        self.assertEqual(save_isolation._keep_menu_path("open Tools > Save isolation now"),
                         "open Tools\u00a0>\u00a0Save\u00a0isolation now")

    def test_plugin_identity_and_modes(self):
        self.assertEqual(self.tool.name(), "Elden Ring MO2 Save Isolation")
        self.assertEqual(self.tool.author(), "MO2 Revamped")
        self.assertTrue(self.tool.enabledByDefault())
        self.assertEqual(self.tool.settings(), [])
        self.assertTrue(self.tool.description())
        self.assertTrue(self.tool.tooltip())
        self.assertTrue(self.tool.localizedName())
        self.assertTrue(self.tool.displayName())
        self.assertEqual(self.tool.version().displayString(), mobase.VersionInfo(0, 5, 0, 85).displayString())
        for mode in (Tool.ProfileIsolated, Tool.InstanceShared, Tool.GlobalShared):
            self.assertTrue(Tool.mode_description(mode))
        self.assertEqual(Tool.mode_description("other"), "")

    def test_other_games_are_left_alone(self):
        self.organizer.managed = FakeGame("skyrimse")
        self.assertFalse(self.tool._active_instance_is_elden_ring())
        self.organizer.managed = None
        self.assertFalse(self.tool._active_instance_is_elden_ring())
        # the hooks do nothing there
        self.tool._initialize_for_managed_game(None)
        self.tool._on_profile_changed(None, None)
        self.tool.display()


class StoredMode(ToolCase):
    def test_missing_file_is_the_profile_route(self):
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.ProfileIsolated)

    def test_bom_and_case_are_accepted(self):
        write(self.profile / Tool.ConfigFilename, "\ufeff[Saves]\nmode = Instance_Shared \n")
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.InstanceShared)

    def test_unknown_or_broken_values_fall_back(self):
        write(self.profile / Tool.ConfigFilename, "[Saves]\nmode = cloud\n")
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.ProfileIsolated)
        write(self.profile / Tool.ConfigFilename, "no section here\n")
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.ProfileIsolated)
        # a file in the system code page (not UTF-8)
        write(self.profile / Tool.ConfigFilename, "[Saves]\nmode = global\nname = Espa\xf1a\n".encode("cp1252"))
        self.assertEqual(Tool._read_mode_directory(self.profile), Tool.ProfileIsolated)

    def test_local_saves_off_without_a_mode_is_steam(self):
        self.organizer._profile._local = False
        profile = self.organizer.profile()
        self.assertEqual(Tool._read_mode(profile), Tool.GlobalShared)
        self.assertEqual(self.tool._effective_mode_directory(self.profile), Tool.GlobalShared)
        # a stored mode wins over MO2's switch
        Tool._save_mode(profile, Tool.ProfileIsolated)
        self.assertEqual(Tool._read_mode(profile), Tool.ProfileIsolated)
        # another profile is known by its file alone
        other = self.profile.parent / "Other"
        other.mkdir()
        self.assertEqual(self.tool._effective_mode_directory(other), Tool.ProfileIsolated)

    def test_save_mode_keeps_other_keys_and_checks_ids(self):
        write(self.profile / Tool.ConfigFilename, "[Saves]\nmode = global\nformats = sl2\n[Other]\nkey = 1\n")
        Tool._save_mode_directory(self.profile, Tool.ProfileIsolated, PROFILE_ID.upper(), "sl2,co2")
        text = (self.profile / Tool.ConfigFilename).read_text(encoding="utf-8")
        self.assertIn("formats = sl2,co2", text)
        self.assertIn("[Other]", text)
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.ProfileIsolated), PROFILE_ID)
        # global has no save profile key: the id is ignored
        Tool._save_mode_directory(self.profile, Tool.GlobalShared, PROFILE_ID)
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.GlobalShared), "")
        # an empty id clears it, a bad one is refused
        Tool._save_mode_directory(self.profile, Tool.ProfileIsolated, "")
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.ProfileIsolated), "")
        with self.assertRaises(ValueError):
            Tool._save_mode_directory(self.profile, Tool.ProfileIsolated, "not-an-id")
        with self.assertRaises(ValueError):
            Tool._save_mode_directory(self.profile, "cloud")
        with self.assertRaises(FileNotFoundError):
            Tool._save_mode_directory(self.profile / "missing", Tool.ProfileIsolated)
        self.assertEqual([p.name for p in self.profile.glob(".*.tmp")], [])

    def test_unreadable_id_is_none(self):
        write(self.profile / Tool.ConfigFilename, b"[Saves]\nprofile_save_profile = \xff\xfe\n")
        self.assertEqual(Tool._read_save_profile_id_directory(self.profile, Tool.ProfileIsolated), "")

    def test_valid_ids(self):
        self.assertEqual(Tool._valid_save_profile_id(" " + PROFILE_ID.upper() + " "), PROFILE_ID)
        self.assertEqual(Tool._valid_save_profile_id(PROFILE_ID[:-1]), "")
        self.assertEqual(Tool._valid_save_profile_id(PROFILE_ID[:-1] + "g"), "")
        self.assertIsNone(Tool._save_profile_key(Tool.GlobalShared))


class RouteFolders(ToolCase):
    def test_global_folder_follows_appdata(self):
        self.assertEqual(Tool._global_save_directory(), self.steam)
        # Wine gives APPDATA with forward slashes
        os.environ["APPDATA"] = self.appdata.as_posix()
        self.assertEqual(Tool._normalized_path(Tool._global_save_directory()),
                         Tool._normalized_path(self.steam))
        os.environ.pop("APPDATA")
        self.assertEqual(Tool._global_save_directory(), Path.home() / "AppData" / "Roaming" / "EldenRing")

    def test_shared_folder_anchored_legacy_and_unknown_base(self):
        base = self.organizer.base
        anchored = base / "Elden Ring Shared Saves"
        self.assertEqual(Tool._instance_shared_directory_from_path(self.profile), anchored)
        # profiles moved elsewhere: the legacy folder beside them is kept while
        # it holds saves and the anchored one has none
        custom = Path(tempfile.mkdtemp(prefix="er-cov-profiles-")) / "profiles" / "Default"
        custom.mkdir(parents=True)
        legacy = custom.parent.parent / "Elden Ring Shared Saves"
        self.assertEqual(Tool._instance_shared_directory_from_path(custom), anchored)
        write(legacy / ACCOUNT / "ER0000.sl2")
        self.assertEqual(Tool._instance_shared_directory_from_path(custom), legacy)
        write(anchored / ACCOUNT / "ER0000.sl2")
        self.assertEqual(Tool._instance_shared_directory_from_path(custom), anchored)
        Tool._path_instance_base = None
        self.assertEqual(Tool._instance_shared_directory_from_path(custom), legacy)
        self.assertEqual(Tool._instance_shared_directory(self.organizer.profile()),
                         self.profile.parent.parent / "Elden Ring Shared Saves")

    def test_folder_for_each_selection(self):
        base = self.organizer.base / "Elden Ring Shared Saves"
        self.assertEqual(Tool._save_directory_for_selection(self.profile, Tool.GlobalShared, None), self.steam)
        self.assertEqual(Tool._save_directory_for_selection(self.profile, Tool.InstanceShared, ""), base)
        self.assertEqual(Tool._save_directory_for_selection(self.profile, Tool.InstanceShared, PROFILE_ID),
                         base / "save_profiles" / PROFILE_ID)
        self.assertEqual(Tool._save_directory_for_selection(self.profile, Tool.ProfileIsolated, PROFILE_ID),
                         self.profile / "save_profiles" / PROFILE_ID)
        Tool._save_mode_directory(self.profile, Tool.ProfileIsolated, PROFILE_ID)
        self.assertEqual(Tool._active_save_directory(self.organizer.profile(), Tool.ProfileIsolated),
                         self.profile / "save_profiles" / PROFILE_ID)
        self.assertEqual(Tool._profile_save_directory(self.organizer.profile()), self.profile / "saves")
        self.assertIsNone(Tool._save_profiles_root_directory(self.profile, Tool.GlobalShared))

    def test_transfer_routes_list_every_profile(self):
        other = self.profile.parent / "alpha"
        write(other / "modlist.txt", "")
        # a folder without modlist.txt is not a profile
        (self.profile.parent / "not a profile").mkdir()
        self.named_profile(other / "save_profiles", PROFILE_ID, "Run B")
        shared_id = "f" * 32
        self.named_profile(self.organizer.base / "Elden Ring Shared Saves" / "save_profiles", shared_id, "Coop")
        routes, current = Tool._save_transfer_routes(self.profile)
        ids = [route_id for _label, route_id, _path in routes]
        self.assertEqual(current, Tool._profile_route_id(self.profile))
        # the current profile first
        self.assertEqual(ids[0], current)
        self.assertIn(Tool._profile_route_id(other) + ":save:" + PROFILE_ID, ids)
        self.assertIn(f"{Tool.InstanceShared}:save:{shared_id}", ids)
        self.assertEqual(ids[-1], Tool.GlobalShared)
        self.assertFalse(any("not a profile" in label for label, _id, _path in routes))

    def test_transfer_routes_of_a_profile_folder_that_is_gone(self):
        lone = Path(tempfile.mkdtemp(prefix="er-cov-lone-")) / "missing" / "Default"
        routes, current = Tool._save_transfer_routes(lone)
        self.assertEqual(routes[0][1], current)


class NamedSaveProfiles(ToolCase):
    def test_names_are_checked(self):
        self.assertEqual(Tool._validated_save_profile_name("  Run A  "), "Run A")
        for bad in ("", "   ", "x" * 65, "tab\there", "del\x7f", None, 3):
            with self.assertRaises(ValueError, msg=repr(bad)):
                Tool._validated_save_profile_name(bad)

    def test_only_valid_records_are_listed(self):
        root = self.profile / "save_profiles"
        self.named_profile(root, PROFILE_ID, "Zeta")
        self.named_profile(root, "a" * 32, "alpha")
        # an uppercase folder, a record naming another id, broken json, no record
        (root / ("B" * 32)).mkdir()
        write(root / ("B" * 32 + ".json"), json.dumps({"id": "b" * 32, "name": "upper"}))
        (root / ("c" * 32)).mkdir()
        write(root / ("c" * 32 + ".json"), json.dumps({"id": "d" * 32, "name": "wrong"}))
        (root / ("e" * 32)).mkdir()
        write(root / ("e" * 32 + ".json"), b"{not json")
        (root / ("1" * 32)).mkdir()
        # not UTF-8, and a name that is no text
        (root / ("2" * 32)).mkdir()
        write(root / ("2" * 32 + ".json"), b'{"id": "' + b"2" * 32 + b'", "name": "\xe9"}')
        (root / ("3" * 32)).mkdir()
        write(root / ("3" * 32 + ".json"), json.dumps({"id": "3" * 32, "name": 5}))
        write(root / "loose.txt", "x")
        listed = Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated)
        self.assertEqual(listed, [("a" * 32, "alpha"), (PROFILE_ID, "Zeta")])
        self.assertEqual(Tool._read_named_save_profiles(self.profile, Tool.GlobalShared), [])

    def test_create_with_a_starting_save_and_its_bak(self):
        source_root = Path(tempfile.mkdtemp(prefix="er-cov-src-"))
        source = write(source_root / ACCOUNT / "ER0000.CO2", b"coop")
        write(source_root / ACCOUNT / "ER0000.CO2.bak", b"coop-bak")
        profile_id = self.tool._create_named_save_profile(
            self.profile, Tool.ProfileIsolated, "Coop run", source, source_root
        )
        folder = self.profile / "save_profiles" / profile_id
        # the save takes the game's name, its extension in lower case
        self.assertEqual((folder / ACCOUNT / "ER0000.co2").read_bytes(), b"coop")
        self.assertEqual((folder / ACCOUNT / "ER0000.co2.bak").read_bytes(), b"coop-bak")
        self.assertEqual(Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated),
                         [(profile_id, "Coop run")])
        # the same name again, any case
        with self.assertRaises(ValueError):
            self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "COOP RUN")
        with self.assertRaises(ValueError):
            self.tool._create_named_save_profile(self.profile, Tool.GlobalShared, "x")
        # no temporary folder left behind
        self.assertEqual([p.name for p in (self.profile / "save_profiles").iterdir() if p.name.startswith(".")], [])

    def test_create_refuses_a_missing_or_odd_source(self):
        source_root = Path(tempfile.mkdtemp(prefix="er-cov-src-"))
        missing = source_root / ACCOUNT / "ER0000.sl2"
        with self.assertRaises(FileNotFoundError):
            self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "A", missing, source_root)
        text = write(source_root / ACCOUNT / "notes.txt", "x")
        with self.assertRaises(FileNotFoundError):
            self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "B", text, source_root)
        save = write(source_root / ACCOUNT / "ER0000.sl2")
        with self.assertRaises(ValueError):
            self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "C", save, None)
        elsewhere = Path(tempfile.mkdtemp(prefix="er-cov-else-"))
        with self.assertRaises(ValueError):
            self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "D", save, elsewhere)
        # nothing was created by the refused attempts
        self.assertEqual(Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated), [])
        root = self.profile / "save_profiles"
        self.assertEqual([p for p in root.iterdir()] if root.is_dir() else [], [])

    def test_create_in_the_shared_route_and_empty(self):
        profile_id = self.tool._create_named_save_profile(self.profile, Tool.InstanceShared, "Shared one")
        root = self.organizer.base / "Elden Ring Shared Saves" / "save_profiles"
        self.assertTrue((root / profile_id).is_dir())
        self.assertTrue((root / f"{profile_id}.json").is_file())

    def test_rename_and_delete(self):
        first = self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "First")
        second = self.tool._create_named_save_profile(self.profile, Tool.ProfileIsolated, "Second")
        with self.assertRaises(ValueError):
            self.tool._rename_named_save_profile(self.profile, Tool.ProfileIsolated, second, "first")
        # renaming to its own name in another case is fine
        self.tool._rename_named_save_profile(self.profile, Tool.ProfileIsolated, first, "FIRST")
        self.tool._rename_named_save_profile(self.profile, Tool.ProfileIsolated, second, "Ünïcode ✓")
        names = dict(Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated))
        self.assertEqual(names, {first: "FIRST", second: "Ünïcode ✓"})
        with self.assertRaises(ValueError):
            self.tool._rename_named_save_profile(self.profile, Tool.GlobalShared, first, "x")
        target = self.tool._delete_named_save_profile(self.profile, Tool.ProfileIsolated, first)
        # moved aside, never erased
        self.assertTrue((target / first).is_dir())
        self.assertTrue((target / f"{first}.json").is_file())
        self.assertEqual(target.parent.parent.name, "save_profiles Backups")
        self.assertEqual([i for i, _n in Tool._read_named_save_profiles(self.profile, Tool.ProfileIsolated)],
                         [second])
        with self.assertRaises(ValueError):
            self.tool._delete_named_save_profile(self.profile, Tool.ProfileIsolated, "../x")
        with self.assertRaises(ValueError):
            self.tool._delete_named_save_profile(self.profile, Tool.GlobalShared, second)

    def test_combo_lists_profiles_and_a_missing_selection(self):
        self.named_profile(self.profile / "save_profiles", PROFILE_ID, "Run")
        combo = QComboBox()
        Tool._populate_save_profile_combo(combo, self.profile, Tool.ProfileIsolated, PROFILE_ID)
        self.assertEqual(combo.currentData(), PROFILE_ID)
        self.assertTrue(combo.isEnabled())
        self.assertFalse(Tool._is_missing_save_profile_selection(combo, PROFILE_ID))
        missing = "9" * 32
        Tool._populate_save_profile_combo(combo, self.profile, Tool.InstanceShared, missing)
        self.assertEqual(combo.currentData(), missing)
        self.assertTrue(Tool._is_missing_save_profile_selection(combo, missing))
        Tool._populate_save_profile_combo(combo, self.profile, Tool.GlobalShared, "")
        self.assertFalse(combo.isEnabled())
        self.assertEqual(combo.count(), 1)


class SaveLists(ToolCase):
    def test_only_account_level_saves_of_known_types(self):
        root = self.steam
        write(root / ACCOUNT / "ER0000.sl2")
        write(root / ACCOUNT / "ER0000.SL2.bak")
        write(root / ACCOUNT / "ER0000.co2")
        write(root / "ER0000.sl2")  # loose in the route root
        write(root / ACCOUNT / "deeper" / "ER0000.sl2")
        write(root / "save_profiles" / PROFILE_ID / ACCOUNT / "ER0000.sl2")
        write(root / ".hidden" / "ER0000.sl2")
        write(root / ACCOUNT / "notes.txt")
        found = [p.relative_to(root).as_posix() for p in Tool._list_transferable_saves(root)]
        self.assertEqual(found, [f"{ACCOUNT}/ER0000.sl2", "ER0000.sl2"])
        both = Tool._list_transferable_saves(root, (".sl2", ".co2", ".bak"))
        self.assertEqual(len(both), 4)
        self.assertEqual(Tool._list_transferable_saves(root / "missing"), [])

    def test_reparse_points_are_skipped(self):
        self.assertFalse(Tool._is_reparse_point(self.steam / "missing"))
        root = self.steam
        write(root / ACCOUNT / "ER0000.sl2")
        target = Path(tempfile.mkdtemp(prefix="er-cov-link-"))
        write(target / "ER0000.sl2")
        link = root / "76561198000000002"
        try:
            os.symlink(target, link, target_is_directory=True)
        except OSError:
            self.skipTest("symbolic links need developer mode here")
        self.assertTrue(Tool._is_reparse_point(link))
        found = [p.parent.name for p in Tool._list_transferable_saves(root)]
        self.assertEqual(found, [ACCOUNT])

    def test_same_contents(self):
        first = write(self.steam / "a", b"x" * 10)
        self.assertTrue(Tool._same_file_contents(first, write(self.steam / "b", b"x" * 10)))
        self.assertFalse(Tool._same_file_contents(first, write(self.steam / "c", b"x" * 9 + b"y")))
        self.assertFalse(Tool._same_file_contents(first, write(self.steam / "d", b"x")))
        self.assertFalse(Tool._contents_match(first, self.steam / "missing"))

    def test_route_save_time_and_when(self):
        self.assertIsNone(Tool._route_save_time(self.steam, "sl2"))
        old = write(self.steam / ACCOUNT / "ER0000.sl2")
        os.utime(old, (1_000_000, 1_000_000))
        new = write(self.steam / "76561198000000002" / "ER0000.sl2")
        os.utime(new, (2_000_000, 2_000_000))
        write(self.steam / "save_profiles" / "ER0000.sl2")
        self.assertEqual(Tool._route_save_time(self.steam, "sl2"), 2_000_000)
        self.assertTrue(Tool._when_text(time.time()).startswith("Today"))
        self.assertRegex(Tool._when_text(1_000_000), r"^\d{4}-\d\d-\d\d \d\d:\d\d$")

    def test_sources_of_the_new_instance_window(self):
        write(self.steam / ACCOUNT / "ER0000.sl2")
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2", b"profile")
        self.named_profile(self.profile / "save_profiles", PROFILE_ID, "Run")
        write(self.profile / "save_profiles" / PROFILE_ID / ACCOUNT / "ER0000.sl2", b"named")
        shared = self.organizer.base / "Elden Ring Shared Saves"
        write(shared / ACCOUNT / "ER0000.sl2", b"shared")
        write(shared / "reforged_online" / ACCOUNT / "ER0000.err", b"online")
        self.named_profile(shared / "save_profiles", "f" * 32, "Coop")
        write(shared / "save_profiles" / ("f" * 32) / ACCOUNT / "ER0000.sl2", b"coop")
        sources = self.tool._create_instance_save_sources()
        labels = [label for label, _path, _relative in sources]
        self.assertEqual(len(sources), 5, labels)
        self.assertTrue(labels[0].startswith("Steam"))
        self.assertTrue(all(relative == f"{ACCOUNT}/ER0000.sl2" for _l, _p, relative in sources))
        online = self.tool._create_instance_save_sources((".err",), include_online=True)
        self.assertEqual([relative for _l, _p, relative in online], [f"{ACCOUNT}/ER0000.err"])
        self.assertIn("(online)", online[0][0])

    def test_known_suffixes_add_a_mod_extension(self):
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        write(self.organizer.mods / "Seamless" / "SeamlessCoop" / "ersc_settings.ini",
              "[SAVE]\nsave_file_extension = MyCoop ; my own\n")
        # the alt-saves extension ("mod") is known even with that mod off
        self.assertEqual(self.tool._known_save_suffixes(), (".sl2", ".co2", ".cnv", ".err", ".mycoop", ".mod"))
        with mock.patch.object(Tool, "_detect_save_formats", side_effect=OSError("locked")):
            self.assertEqual(self.tool._known_save_suffixes(), (".sl2", ".co2", ".cnv", ".err"))


class SaveFormats(ToolCase):
    def formats(self) -> dict[str, dict]:
        return {item["key"]: item for item in self.tool._detect_save_formats()}

    def test_plain_game(self):
        formats = self.formats()
        self.assertTrue(formats["vanilla"]["available"])
        self.assertFalse(formats["seamless"]["available"])
        self.assertFalse(formats["alt_saves_cnv"]["available"])
        self.assertFalse(formats["reforged"]["available"])
        self.assertEqual(self.tool._seamless_mod(), "")

    def test_seamless_with_convergence_and_alt_saves(self):
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        conv = self.organizer.add_mod("The Convergence", ["convergence.me3", "DLLs/x.dll"])
        write(conv / "DLLs" / "ersc_settings.ini", "save_file_extension = \"cnvx\"\n")
        write(conv / "mod" / "altsaves.toml", "extension = \".cnv\"\n")
        self.organizer.add_mod("Reforged", ["DLLs/reforged.dll"])
        formats = self.formats()
        self.assertEqual(formats["seamless"]["extension"], "co2")
        self.assertEqual(formats["convergence_seamless"]["extension"], "cnvx")
        self.assertEqual(formats["alt_saves"]["name"], "Convergence")
        self.assertEqual(formats["alt_saves"]["extension"], "cnv")
        self.assertTrue(formats["reforged"]["available"])
        self.assertEqual(self.tool._seamless_mod(), "Seamless")

    def test_convergence_extension_same_as_seamless_is_not_listed_twice(self):
        seamless = self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        write(seamless / "SeamlessCoop" / "ersc_settings.ini", "save_file_extension = cnvco2\n")
        self.organizer.add_mod("Conv", ["convergence.me3"])
        formats = self.formats()
        self.assertNotIn("convergence_seamless", formats)

    def test_alt_saves_without_extension_and_inactive_mods(self):
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"], active=False)
        alt = self.organizer.add_mod("alt saves", ["altsaves.toml"])
        write(alt / "altsaves.toml", "# no extension\n")
        formats = self.formats()
        self.assertFalse(formats["seamless"]["available"])
        self.assertEqual(formats["alt_saves"]["extension"], "mod")
        self.assertEqual(formats["alt_saves"]["name"], "alt-saves")

    def test_settings_extension_reads_odd_files(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cov-ini-"))
        ini = write(folder / "a.ini", b"\xef\xbb\xbf[SAVE]\nSave_File_Extension = 'co3' ; c\n")
        self.assertEqual(Tool._settings_extension(ini, "save_file_extension"), "co3")
        ini = write(folder / "b.ini", "save_file_extension=\xe9t\xe9\n".encode("cp1252"))
        # not UTF-8: read with replacement characters, never an exception
        self.assertTrue(Tool._settings_extension(ini, "save_file_extension"))
        self.assertEqual(Tool._settings_extension(folder / "missing.ini", "x"), "")
        self.assertEqual(Tool._settings_extension(write(folder / "c.ini", "other = 1\n"), "x"), "")

    def test_convergence_meta_ini_not_utf8(self):
        folder = Path(tempfile.mkdtemp(prefix="er-cov-meta-"))
        write(folder / "meta.ini", b"[General]\nmodid=3419\ngameName=eldenring\ncomments=\xe9\n")
        self.assertFalse(Tool._is_convergence(folder))
        write(folder / "meta.ini", "[General]\nmodid=3419\ngameName=Elden Ring\n[General]\nx=1\n")
        # a repeated section is accepted (MO2 writes none, but a hand edit may)
        self.assertTrue(Tool._is_convergence(folder))
        write(folder / "DLLs" / "ersc_settings.ini", "save_file_extension = co2\n")
        write(folder / "ersc_settings.ini", "save_file_extension = rootco\n")
        self.assertEqual(Tool._convergence_coop_extension(folder), "rootco")


class NightreignFormats(ToolCase):
    nightreign = True

    def test_nightreign_lists_its_own(self):
        self.organizer.add_mod("Seamless NR", ["SeamlessCoop/nrsc.dll"])
        formats = {item["key"]: item for item in self.tool._detect_save_formats()}
        self.assertTrue(formats["seamless"]["available"])
        self.assertNotIn("reforged", formats)
        self.assertNotIn("alt_saves_cnv", formats)
        self.assertEqual(self.tool._seamless_mod(), "Seamless NR")
        self.assertEqual(Tool._global_save_directory(), self.steam)


class LaunchFile(ToolCase):
    def test_executables_from_mo2_ini(self):
        self.assertEqual(self.tool._read_executables(), [])
        write(self.organizer.base / "ModOrganizer.ini",
              "[customExecutables]\nsize=3\n1\\title=Elden Ring\n1\\binary=Z:/games/ELDEN RING/Game/eldenring.exe\n"
              "2\\title=\n2\\binary=x.exe\n3\\title=Reforged\n3\\binary=C:/R/ReforgedLauncher.exe\n")
        executables = self.tool._read_executables()
        self.assertEqual([title for title, _binary in executables], ["Elden Ring", "Reforged"])
        self.assertTrue(Tool._is_game_launch_binary("Z:/games/ELDEN RING/Game/EldenRing.exe"))
        self.assertFalse(Tool._is_game_launch_binary("/home/user/tools/me3.exe"))

    def test_methods_round_trip_keep_other_keys(self):
        path = self.organizer.base / "eldenring_mo2_launch_methods.json"
        self.assertEqual(self.tool._launch_methods(), {})
        self.assertTrue(self.tool._reforged_online_separate())
        write(path, json.dumps({"reforged_online_saves": "shared", "methods": {"a": "vfs", "b": "bad"}}))
        self.assertEqual(self.tool._launch_methods(), {"a": "vfs"})
        self.assertFalse(self.tool._reforged_online_separate())
        binary = r"C:\Games\ELDEN RING\Game\eldenring.exe"
        self.tool._store_launch_method(binary, "swap")
        self.assertEqual(self.tool._launch_method_of(binary, self.tool._launch_methods()), ("swap", True))
        # back to the automatic method: the entry goes
        self.tool._store_launch_method(binary, "vfs")
        self.assertEqual(self.tool._launch_method_of(binary, self.tool._launch_methods()), ("vfs", False))
        self.assertEqual(self.tool._launch_method_of("C:/R/reforgedlauncher.exe", {}), ("swap", False))
        data = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(data["reforged_online_saves"], "shared")
        self.assertEqual(data["version"], 1)

    def test_broken_launch_files(self):
        path = self.organizer.base / "eldenring_mo2_launch_methods.json"
        for text in (b"[1, 2]", b"{bad", b'{"methods": [1]}', b"\xff\xfe\x00"):
            write(path, text)
            self.assertEqual(self.tool._launch_methods(), {}, text)
            self.assertTrue(self.tool._reforged_online_separate(), text)
        write(path, b"[1]")
        self.tool._write_launch_file({"methods": {}})
        self.assertEqual(json.loads(path.read_text(encoding="utf-8")), {"version": 1, "methods": {}})

    def test_no_instance_folder(self):
        tool = Tool()
        self.assertEqual(tool._read_executables(), [])
        self.assertEqual(tool._launch_methods(), {})
        self.assertTrue(tool._reforged_online_separate())
        self.assertIsNone(tool._game_directory())
        with self.assertRaises(OSError):
            tool._store_launch_method("x.exe", "swap")
        with self.assertRaises(OSError):
            tool._write_launch_file({})

    def test_game_directory(self):
        self.assertEqual(self.tool._game_directory(), self.organizer.game_dir / "Game")


class RunningGame(ToolCase):
    def test_game_or_its_launchers(self):
        game_folder = self.organizer.game_dir
        cases = [
            (None, None, True),
            ([(1, "explorer.exe")], None, False),
            ([(1, "eldenring.exe")], None, True),
            # Nightreign's process does not block Elden Ring
            ([(1, "nightreign.exe")], None, False),
            ([(1, "reforgedlauncher.exe")], None, True),
            # Easy Anti-Cheat's launcher: only the one in this game's folder
            ([(1, "start_protected_game.exe")], str(game_folder / "start_protected_game.exe"), True),
            ([(1, "start_protected_game.exe")], r"C:\Other Game\start_protected_game.exe", False),
            ([(1, "start_protected_game.exe")], None, True),
        ]
        for processes, image, expected in cases:
            with mock.patch.object(save_isolation, "_running_processes", return_value=processes), \
                    mock.patch.object(save_isolation, "_process_image_path", return_value=image):
                self.assertEqual(self.tool._game_or_launcher_running(), expected, (processes, image))

    def test_anti_cheat_launcher_without_a_game_folder(self):
        self.organizer.managed = None
        with mock.patch.object(save_isolation, "_running_processes",
                               return_value=[(1, "start_protected_game.exe")]), \
                mock.patch.object(save_isolation, "_process_image_path", return_value=r"C:\x.exe"):
            self.assertTrue(self.tool._game_or_launcher_running())

    def test_process_list_helpers_answer(self):
        # the real Windows calls: a list of this PC's processes, this one included
        names = self.real_process_names()
        processes = save_isolation._running_processes()
        self.assertTrue(processes)
        self.assertIn("python.exe", {name for _pid, name in processes})
        self.assertTrue(save_isolation._process_image_path(os.getpid()).casefold().endswith("python.exe"))
        self.assertIsNone(save_isolation._process_image_path(0))
        self.assertIn("python.exe", names)

    def test_apply_saved_route_waits_while_busy(self):
        with mock.patch.object(Tool, "_game_or_launcher_running", return_value=True):
            self.tool._apply_saved_route()
        self.assertEqual(self.notices[-1][2], "info")

    def test_apply_saved_route_offers_a_restart(self):
        self.answers = [False]
        with mock.patch.object(Tool, "_game_or_launcher_running", return_value=False), \
                mock.patch.object(Diagnose, "_pending_sessions", return_value=[]):
            self.tool._apply_saved_route()
        self.assertEqual(self.answers, [])

    def test_transfer_blocked_while_the_game_runs(self):
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.assertTrue(Tool._transfer_blocked_reason())
        self.assertEqual(Tool._transfer_blocked_reason(), "")

    def test_transfer_check_falls_back_to_tasklist(self):
        class Result:
            def __init__(self, code, out):
                self.returncode, self.stdout = code, out

        with mock.patch.object(save_isolation, "_running_process_names", return_value=None):
            with mock.patch.object(save_isolation.subprocess, "run",
                                   return_value=Result(0, '"eldenring.exe","1234","Console"\n')):
                self.assertTrue(Tool._transfer_blocked_reason())
            with mock.patch.object(save_isolation.subprocess, "run",
                                   return_value=Result(0, "INFO: No tasks are running\n")):
                self.assertEqual(Tool._transfer_blocked_reason(), "")
            with mock.patch.object(save_isolation.subprocess, "run", return_value=Result(1, "")):
                self.assertTrue(Tool._transfer_blocked_reason())
            with mock.patch.object(save_isolation.subprocess, "run", side_effect=OSError("no tasklist")):
                self.assertTrue(Tool._transfer_blocked_reason())


class RecoverySessions(ToolCase):
    def session(self, name: str, manifest=None, folders=()) -> Path:
        recovery = Diagnose._recovery_folder()
        session = recovery / name
        session.mkdir(parents=True, exist_ok=True)
        for folder in folders:
            (session / folder).mkdir(parents=True, exist_ok=True)
        if manifest is not None:
            write(session / "session.json", manifest if isinstance(manifest, (str, bytes)) else json.dumps(manifest))
        return session

    def test_recovery_folder_beside_the_steam_folder(self):
        recovery = Diagnose._recovery_folder()
        self.assertEqual(recovery.parent, self.appdata / ".MO2RevampedSaveRecovery")
        Tool._game_key = "nightreign"
        self.assertNotEqual(Diagnose._recovery_folder(), recovery)

    def test_no_sessions(self):
        self.assertEqual(Diagnose._pending_sessions(), [])
        self.assertEqual(Diagnose._set_aside_folders(), [])
        self.assertEqual(Tool._transfer_blocked_reason(), "")

    def test_hidden_account_folders_are_pending(self):
        session = self.session("one", {"global_save_root": str(self.steam)}, [ACCOUNT])
        self.assertEqual(Diagnose._pending_sessions(), [session])
        self.assertTrue(Tool._transfer_blocked_reason())

    def test_other_games_sessions_and_unrouted_output_are_not(self):
        self.session("other", {"global_save_root": str(self.appdata / "Nightreign")}, [ACCOUNT])
        self.assertEqual(Tool._transfer_blocked_reason(), "")
        unrouted = self.session("kept", {"global_save_root": str(self.steam)}, ["unrouted-global"])
        write(unrouted / "unrouted-global" / ACCOUNT / "ER0000.sl2")
        self.assertEqual(Tool._transfer_blocked_reason(), "")
        self.assertEqual(Diagnose._set_aside_folders(), [unrouted / "unrouted-global"])

    def test_swap_not_returned_is_pending_without_folders(self):
        session = self.session("swap", {"swap": {"returned": False}})
        self.assertTrue(Diagnose._swap_not_returned(session))
        self.assertEqual(Diagnose._pending_sessions(), [session])
        self.assertTrue(Tool._transfer_blocked_reason())
        write(session / "session.json", json.dumps({"swap": {"returned": True}}))
        self.assertFalse(Diagnose._swap_not_returned(session))
        self.assertEqual(Tool._transfer_blocked_reason(), "")

    def test_broken_manifest_counts_by_its_folders(self):
        self.session("broken", b"\xff{", [ACCOUNT])
        self.assertTrue(Tool._transfer_blocked_reason())
        self.assertFalse(Diagnose._swap_not_returned(Diagnose._recovery_folder() / "broken"))
        # a list instead of an object
        self.session("list", "[1]")
        self.assertFalse(Diagnose._swap_not_returned(Diagnose._recovery_folder() / "list"))

    def test_while_the_game_runs_its_session_is_not_pending(self):
        self.session("one", {}, [ACCOUNT])
        with mock.patch.object(save_isolation, "_running_process_names", return_value={"eldenring.exe"}):
            self.assertEqual(Diagnose._pending_sessions(), [])
        with mock.patch.object(save_isolation, "_running_process_names", return_value=None):
            self.assertEqual(Diagnose._pending_sessions(), [])


class SaveChecks(ToolCase):
    def setUp(self):
        super().setUp()
        self.diagnose = Diagnose(self.tool)
        self.assertTrue(self.diagnose.init(self.organizer))

    def test_identity(self):
        self.assertEqual(self.diagnose.name(), "Elden Ring MO2 Save Diagnose")
        self.assertTrue(self.diagnose.localizedName())
        self.assertTrue(self.diagnose.description())
        self.assertEqual(self.diagnose.author(), "MO2 Revamped")
        self.assertEqual(self.diagnose.settings(), [])
        self.assertTrue(self.diagnose.requirements())
        self.assertTrue(self.diagnose.version())

    def test_empty_route_while_steam_has_saves(self):
        self.assertEqual(self.diagnose.activeProblems(), [])
        write(self.steam / ACCOUNT / "ER0000.sl2")
        # a folder that is not a Steam account does not count
        write(self.profile / "saves" / "backup" / "ER0000.sl2")
        self.assertEqual(self.diagnose.activeProblems(), [Diagnose.EmptyRoute])
        self.assertTrue(self.diagnose.hasGuidedFix(Diagnose.EmptyRoute))
        self.assertFalse(self.diagnose.hasGuidedFix(Diagnose.PendingRecovery))
        self.assertIn(str(self.profile / "saves"), self.diagnose.fullDescription(Diagnose.EmptyRoute))
        self.assertTrue(self.diagnose.shortDescription(Diagnose.EmptyRoute))
        write(self.profile / "saves" / ACCOUNT / "ER0000.sl2")
        self.assertEqual(self.diagnose.activeProblems(), [])

    def test_global_route_is_never_empty(self):
        write(self.steam / ACCOUNT / "ER0000.sl2")
        Tool._save_mode_directory(self.profile, Tool.GlobalShared)
        self.assertEqual(self.diagnose.activeProblems(), [])

    def test_pending_and_set_aside(self):
        recovery = Diagnose._recovery_folder()
        (recovery / "a" / ACCOUNT).mkdir(parents=True)
        write(recovery / "b" / "unrouted-global" / ACCOUNT / "ER0000.sl2")
        problems = self.diagnose.activeProblems()
        self.assertIn(Diagnose.PendingRecovery, problems)
        self.assertIn(Diagnose.SetAsideSaves, problems)
        self.assertIn(str(recovery / "a"), self.diagnose.fullDescription(Diagnose.PendingRecovery))
        self.assertIn("unrouted-global", self.diagnose.fullDescription(Diagnose.SetAsideSaves))
        for key in (Diagnose.PendingRecovery, Diagnose.SetAsideSaves):
            self.assertTrue(self.diagnose.shortDescription(key))

    def test_other_games_and_no_profile(self):
        self.organizer.managed = FakeGame("skyrimse")
        self.assertEqual(self.diagnose.activeProblems(), [])
        self.organizer.managed = FakeGame("nightreign")
        self.assertEqual(self.diagnose.activeProblems(), [])
        self.assertEqual(Tool._game_key, "nightreign")
        self.organizer._profile = None
        self.assertIsNone(self.diagnose._active_route())
        self.assertIn("Route: ", self.diagnose.fullDescription(Diagnose.EmptyRoute))
        # the guided fix opens the tool, which needs a profile: it says so
        self.diagnose.startGuidedFix(Diagnose.PendingRecovery)
        self.assertEqual(self.notices, [])
        self.diagnose.startGuidedFix(Diagnose.EmptyRoute)
        self.assertEqual(self.notices[-1][2], "warning")

    def test_create_plugins(self):
        tool, diagnose = save_isolation.createPlugins()
        self.assertIs(tool._save_diagnose, diagnose)
        # refreshing the checks never fails, even with no cache to clear
        tool._refresh_save_checks()


if __name__ == "__main__":
    unittest.main()
