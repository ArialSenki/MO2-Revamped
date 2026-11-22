"""The save route of a profile (saves.py): its ini in odd encodings, named
save profiles and their metadata, the instance's shared folder and the VFS
mapping MO2 gets."""

from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from PyQt6.QtCore import QDir
from basic_games.games.eldenring.saves import EldenRingLocalSavegames, NightreignLocalSavegames

from fake_organizer import FakeOrganizer, FakeProfile, appdata_folder, game_for

ID = "0123456789abcdef0123456789abcdef"


def profile_dir() -> Path:
    base = Path(tempfile.mkdtemp(prefix="er-saves-"))
    folder = base / "profiles" / "Default"
    folder.mkdir(parents=True)
    return folder


def write_ini(folder: Path, text: str, encoding: str = "utf-8") -> None:
    (folder / EldenRingLocalSavegames.ConfigFilename).write_bytes(text.encode(encoding))


class SaveMode(unittest.TestCase):
    """The mode read from eldenring_mo2_saves.ini, whatever wrote it."""

    cls = EldenRingLocalSavegames

    def test_missing_file_is_profile(self):
        self.assertEqual(self.cls.mode_for_profile_directory(profile_dir()), "profile")

    def test_bom_and_case(self):
        folder = profile_dir()
        write_ini(folder, "[Saves]\nmode = Instance_Shared\n", "utf-8-sig")
        self.assertEqual(self.cls.mode_for_profile_directory(folder), "instance_shared")
        self.assertTrue(self.cls._mode_is_stored(folder))

    def test_not_utf8_falls_back(self):
        folder = profile_dir()
        (folder / self.cls.ConfigFilename).write_bytes(b"[Saves]\nmode = global\nname = \xf1\xe9\n")
        self.assertEqual(self.cls.mode_for_profile_directory(folder), "profile")
        self.assertFalse(self.cls._mode_is_stored(folder))
        self.assertEqual(self.cls.save_profile_for_profile_directory(folder, "profile"), "")

    def test_broken_ini_and_unknown_mode(self):
        folder = profile_dir()
        write_ini(folder, "mode = global\n")  # no section header
        self.assertEqual(self.cls.mode_for_profile_directory(folder), "profile")
        write_ini(folder, "[Saves]\nmode = everywhere\n")
        self.assertEqual(self.cls.mode_for_profile_directory(folder), "profile")
        self.assertFalse(self.cls._mode_is_stored(folder))

    def test_profile_without_local_saves_is_global(self):
        folder = profile_dir()
        self.assertEqual(self.cls.mode_for_profile(FakeProfile(folder, local_saves=False)), "global")
        self.assertEqual(self.cls.mode_for_profile(FakeProfile(folder, local_saves=True)), "profile")
        # a stored mode wins over MO2's setting
        write_ini(folder, "[Saves]\nmode = profile\n")
        self.assertEqual(self.cls.mode_for_profile(FakeProfile(folder, local_saves=False)), "profile")

    def test_profile_whose_setting_cannot_be_read(self):
        class Broken(FakeProfile):
            def localSavesEnabled(self):
                raise RuntimeError("gone")

        self.assertEqual(self.cls.mode_for_profile(Broken(profile_dir())), "profile")


class NamedSaveProfiles(unittest.TestCase):
    cls = EldenRingLocalSavegames

    def setUp(self):
        self.folder = profile_dir()

    def make(self, root: Path, metadata, raw: bytes | None = None) -> None:
        (root / ID).mkdir(parents=True, exist_ok=True)
        data = raw if raw is not None else json.dumps(metadata).encode("utf-8")
        (root / f"{ID}.json").write_bytes(data)

    def test_ids(self):
        write_ini(self.folder, f"[Saves]\nprofile_save_profile = {ID.upper()}\n")
        self.assertEqual(self.cls.save_profile_for_profile_directory(self.folder, "profile"), ID)
        self.assertEqual(self.cls.save_profile_for_profile_directory(self.folder, "instance_shared"), "")
        self.assertEqual(self.cls.save_profile_for_profile_directory(self.folder, "global"), "")
        for bad in ("short", "z" * 32, ID + "0"):
            with self.subTest(bad=bad):
                write_ini(self.folder, f"[Saves]\nprofile_save_profile = {bad}\n")
                self.assertEqual(self.cls.save_profile_for_profile_directory(self.folder, "profile"), "")

    def test_available(self):
        root = self.folder / "save_profiles"
        # no named profile chosen: the plain route is always there
        self.assertTrue(self.cls.save_profile_is_available(self.folder, "profile"))
        write_ini(self.folder, f"[Saves]\nprofile_save_profile = {ID}\n")
        self.assertFalse(self.cls.save_profile_is_available(self.folder, "profile"))
        self.make(root, {"id": ID, "name": "Run 1"})
        self.assertTrue(self.cls.save_profile_is_available(self.folder, "profile"))
        self.assertEqual(self.cls.active_save_directory(self.folder, "profile"), root / ID)

    def test_unusable_metadata(self):
        root = self.folder / "save_profiles"
        write_ini(self.folder, f"[Saves]\nprofile_save_profile = {ID}\n")
        cases = {
            "other id": json.dumps({"id": "f" * 32, "name": "x"}).encode(),
            "blank name": json.dumps({"id": ID, "name": "  "}).encode(),
            "not a dict": b"[1, 2]",
            "broken json": b"{",
            "not utf-8": b'{"id": "' + ID.encode() + b'", "name": "\xf1"}',
            "bom": b"\xef\xbb\xbf" + json.dumps({"id": ID, "name": "x"}).encode(),
        }
        for label, raw in cases.items():
            with self.subTest(label):
                self.make(root, None, raw)
                self.assertFalse(self.cls.save_profile_is_available(self.folder, "profile"))

    def test_folder_that_is_a_file(self):
        root = self.folder / "save_profiles"
        root.mkdir()
        (root / ID).write_bytes(b"")
        (root / f"{ID}.json").write_text(json.dumps({"id": ID, "name": "x"}))
        write_ini(self.folder, f"[Saves]\nprofile_save_profile = {ID}\n")
        self.assertFalse(self.cls.save_profile_is_available(self.folder, "profile"))

    def test_global_mode_has_no_named_profile(self):
        self.assertTrue(self.cls.save_profile_is_available(self.folder, "global"))
        self.assertIsNone(self.cls.active_save_directory(self.folder, "global"))

    def test_instance_shared(self):
        organizer = FakeOrganizer(base=self.folder.parent.parent)
        EldenRingLocalSavegames(None, organizer)
        write_ini(self.folder, f"[Saves]\nmode = instance_shared\ninstance_save_profile = {ID}\n")
        shared = organizer.base / "Elden Ring Shared Saves"
        self.assertEqual(self.cls.active_save_directory(self.folder, "instance_shared"),
                         shared / "save_profiles" / ID)
        self.assertFalse(self.cls.save_profile_is_available(self.folder, "instance_shared"))
        self.make(shared / "save_profiles", {"id": ID, "name": "Shared"})
        self.assertTrue(self.cls.save_profile_is_available(self.folder, "instance_shared"))
        write_ini(self.folder, "[Saves]\nmode = instance_shared\n")
        self.assertEqual(self.cls.active_save_directory(self.folder, "instance_shared"), shared)


class SharedFolder(unittest.TestCase):
    """The instance's shared saves: anchored to the instance, or the folder
    earlier builds made beside a customised profiles folder."""

    def tearDown(self):
        EldenRingLocalSavegames._path_organizer = None

    def test_without_an_organizer_the_legacy_place(self):
        EldenRingLocalSavegames._path_organizer = None
        folder = profile_dir()
        self.assertEqual(EldenRingLocalSavegames.instance_shared_directory(folder),
                         folder.parent.parent / "Elden Ring Shared Saves")

    def test_custom_profiles_folder(self):
        instance = FakeOrganizer()
        EldenRingLocalSavegames(None, instance)
        profiles = Path(tempfile.mkdtemp(prefix="er-custom-")) / "profiles" / "Default"
        profiles.mkdir(parents=True)
        anchored = instance.base / "Elden Ring Shared Saves"
        self.assertEqual(EldenRingLocalSavegames.instance_shared_directory(profiles), anchored)
        # an older build's folder that holds saves is kept
        legacy = profiles.parent.parent / "Elden Ring Shared Saves"
        (legacy / "76561198000000001").mkdir(parents=True)
        self.assertEqual(EldenRingLocalSavegames.instance_shared_directory(profiles), legacy)
        # unless the instance's own folder is already in use
        (anchored / "76561198000000001").mkdir(parents=True)
        self.assertEqual(EldenRingLocalSavegames.instance_shared_directory(profiles), anchored)

    def test_organizer_without_a_base(self):
        class NoBase:
            def basePath(self):
                raise RuntimeError("not yet")

        EldenRingLocalSavegames._path_organizer = NoBase()
        folder = profile_dir()
        self.assertEqual(EldenRingLocalSavegames.instance_shared_directory(folder),
                         folder.parent.parent / "Elden Ring Shared Saves")

    def test_nightreign_has_its_own_folder(self):
        EldenRingLocalSavegames._path_organizer = None
        folder = profile_dir()
        self.assertEqual(NightreignLocalSavegames.instance_shared_directory(folder).name,
                         "Nightreign Shared Saves")


class RouteAndMapping(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.feature = self.game._local_savegames_feature
        self.profile = self.organizer.profile()

    def tearDown(self):
        EldenRingLocalSavegames._path_organizer = None

    def test_prepare_and_current(self):
        self.assertFalse(self.feature.profile_route_is_current(self.profile))
        self.assertTrue(self.feature.prepareProfile(self.profile))
        self.assertTrue(self.feature.profile_route_is_current(self.profile))
        # another local route is taken without restarting MO2
        write_ini(self.organizer.profile_dir, f"[Saves]\nmode = instance_shared\n")
        self.assertTrue(self.feature.profile_route_is_current(self.profile))
        # the Steam route needs MO2's own setting reloaded
        write_ini(self.organizer.profile_dir, "[Saves]\nmode = global\n")
        self.assertFalse(self.feature.profile_route_is_current(self.profile))
        self.assertFalse(self.feature.prepareProfile(self.profile))

    def test_mappings(self):
        saves = QDir(str(self.organizer.profile_dir / "saves"))
        mapping = self.feature.mappings(saves)
        self.assertEqual(len(mapping), 1)
        self.assertEqual(Path(mapping[0].source), self.organizer.profile_dir / "saves")
        self.assertEqual(Path(mapping[0].destination), Path(self.game.savesDirectory().absolutePath()))
        write_ini(self.organizer.profile_dir, "[Saves]\nmode = global\n")
        self.assertEqual(self.feature.mappings(saves), [])
        write_ini(self.organizer.profile_dir, "[Saves]\nmode = instance_shared\n")
        self.assertEqual(Path(self.feature.mappings(saves)[0].source),
                         self.organizer.base / "Elden Ring Shared Saves")

    def test_profile_key_survives_a_missing_folder(self):
        gone = FakeProfile(Path(tempfile.mkdtemp(prefix="er-gone-")) / "nothing")
        self.assertTrue(self.feature._profile_key(gone))


if __name__ == "__main__":
    unittest.main()
