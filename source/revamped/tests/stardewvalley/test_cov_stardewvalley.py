"""The Stardew Valley plugin's less trodden paths: manifests in other
encodings, unreadable files and folders, the problem texts, SMAPI's files
changing outside MO2, and layouts deeper than SMAPI looks."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_stardewvalley as plugin_module
from basic_games.games.stardewvalley import checks as checks_module
from basic_games.games.stardewvalley import manifest as manifests
from basic_games.games.stardewvalley import plugins as sdv_plugins
from basic_games.games.stardewvalley.checks import (
    ManifestCache, ModManifests, analyze, display_name, game_folder_manifests, mod_manifests,
)
from basic_games.games.stardewvalley.content import Content, StardewValleyModDataContent, contents_of, mod_folders
from basic_games.games.stardewvalley.layout import StardewValleyModDataChecker, holds_mods
from basic_games.games.stardewvalley.plugins import (
    DUPLICATES, MISSING, NO_SMAPI, SMAPI_TOO_OLD, TOO_OLD, StardewValleyCheck,
)
from fake_organizer import FakeOrganizer
from harness import make_tree

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID
Game = plugin_module.StardewValleyGame


def manifest_text(uid, name=None, version="1.0.0", **extra):
    data = {"Name": name or uid, "Author": "x", "Version": version, "UniqueID": uid, **extra}
    return json.dumps(data, ensure_ascii=False)


class ManifestFilesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="sdv-cov-manifest-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def read(self, data: bytes):
        path = self.tmp / "manifest.json"
        path.write_bytes(data)
        return manifests.read(path)

    def test_encodings(self) -> None:
        text = manifest_text("Me.Café", name="Café Mod")
        self.assertEqual(self.read(text.encode("utf-8")).name, "Café Mod")
        self.assertEqual(self.read(b"\xef\xbb\xbf" + text.encode("utf-8")).unique_id, "me.café")
        # SMAPI reads with .NET's File.ReadAllText, which follows a UTF-16 BOM
        # (Notepad's "Unicode")
        self.assertEqual(self.read(b"\xff\xfe" + text.encode("utf-16-le")).name, "Café Mod")
        self.assertEqual(self.read(b"\xfe\xff" + text.encode("utf-16-be")).name, "Café Mod")
        # an ANSI file: the accent is lost, the manifest still read
        self.assertEqual(self.read(text.encode("cp1252")).unique_id, "me.caf\ufffd")

    def test_unreadable(self) -> None:
        self.assertIsNone(manifests.read(self.tmp / "missing.json"))
        (self.tmp / "folder.json").mkdir()
        self.assertIsNone(manifests.read(self.tmp / "folder.json"))
        self.assertIsNone(self.read(b"\x00\x01\x02"))

    def test_odd_dependencies(self) -> None:
        parsed = manifests.parse(manifest_text("Me.A", Dependencies=[
            "not an object", {"UniqueID": ""}, {"UniqueID": 5}, {"uniqueid": "lower.case", "minimumversion": 2},
            {"UniqueID": " Spaced.Id ", "IsRequired": None},
        ], contentpackfor={"UniqueID": "Pathoschild.ContentPatcher"}))
        self.assertEqual([(d.unique_id, d.minimum, d.required) for d in parsed.dependencies],
                         [("lower.case", "", True), ("spaced.id", "", True)])
        self.assertEqual(parsed.content_pack_for.unique_id, "pathoschild.contentpatcher")

    def test_is_required_as_a_string(self) -> None:
        # Newtonsoft turns "true"/"false" into a bool in any case, as SMAPI reads it
        parsed = manifests.parse(manifest_text("Me.A", Dependencies=[
            {"UniqueID": "a.b", "IsRequired": "false"}, {"UniqueID": "c.d", "IsRequired": "False"},
            {"UniqueID": "e.f", "IsRequired": "TRUE"}, {"UniqueID": "g.h", "IsRequired": "true"},
        ]))
        self.assertEqual([d.required for d in parsed.dependencies], [False, False, True, True])

    def test_cache(self) -> None:
        path = self.tmp / "manifest.json"
        path.write_text(manifest_text("Me.A"))
        cache = ManifestCache()
        with mock.patch.object(manifests, "read", wraps=manifests.read) as reads:
            self.assertEqual(cache.read(path).unique_id, "me.a")
            self.assertEqual(cache.read(path).unique_id, "me.a")
            self.assertEqual(reads.call_count, 1)
            path.write_text(manifest_text("Me.Changed"))
            self.assertEqual(cache.read(path).unique_id, "me.changed")
            self.assertEqual(reads.call_count, 2)
        self.assertIsNone(cache.read(self.tmp / "missing.json"))


class FoldersTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="sdv-cov-folders-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_game_folder_manifests(self) -> None:
        mods = self.tmp / "Mods"
        for folder in ("ConsoleCommands", ".hidden", "Broken", "Empty"):
            (mods / folder).mkdir(parents=True)
        (mods / "ConsoleCommands/manifest.json").write_text(manifest_text("SMAPI.ConsoleCommands"))
        (mods / ".hidden/manifest.json").write_text(manifest_text("Me.Hidden"))
        (mods / "Broken/manifest.json").write_text("{ not json")
        (mods / "loose.txt").write_text("x")
        found = game_folder_manifests(mods, ManifestCache())
        self.assertEqual(([m.unique_id for m in found.manifests], found.folders),
                         (["smapi.consolecommands"], ["consolecommands"]))
        # no Mods folder yet (SMAPI not run): nothing, no error
        self.assertEqual(game_folder_manifests(self.tmp / "none", ManifestCache()).manifests, [])

    def test_mod_manifests_with_other_case_on_disk(self) -> None:
        mod = self.tmp / "mod"
        (mod / "MyMod").mkdir(parents=True)
        (mod / "MyMod/Manifest.JSON").write_text(manifest_text("Me.Mine"))
        found = mod_manifests("Mine", mod, make_tree(["MyMod/Manifest.JSON"]), ManifestCache())
        self.assertEqual(([m.unique_id for m in found.manifests], found.folders), (["me.mine"], ["mymod"]))
        # the tree lists a manifest the folder no longer has
        gone = mod_manifests("Gone", mod, make_tree(["Other/manifest.json"]), ManifestCache())
        self.assertEqual(gone.manifests, [])


class AnalyzeTest(unittest.TestCase):
    def test_bundled_and_optional(self) -> None:
        mods = [
            ModManifests("Old lib", [manifests.parse(manifest_text("Me.Lib", version="1.0.0"))]),
            ModManifests("User", [manifests.parse(manifest_text("Me.User", Dependencies=[
                {"UniqueID": "SMAPI.ConsoleCommands"},
                {"UniqueID": "Me.Lib", "MinimumVersion": "2.0.0", "IsRequired": False},
                {"UniqueID": "Me.NotThere", "MinimumVersion": "9.0", "IsRequired": False},
                {"UniqueID": "SMAPI"},
            ]))]),
        ]
        report = analyze(mods, None, "4.1.0")
        self.assertEqual(report.missing, {})
        self.assertEqual(report.too_old, [("User", "Me.Lib", "1.0.0", "2.0.0")])
        # without SMAPI, its bundled mods aren't there either
        self.assertEqual(analyze(mods, None, None).missing, {"smapi.consolecommands": ["User"]})
        # an unreadable SMAPI version never warns about its age
        report = analyze([ModManifests("New", [manifests.parse(manifest_text("Me.N", MinimumApiVersion="9.0"))])],
                         None, "")
        self.assertEqual(report.smapi_too_old, [])

    def test_display_name(self) -> None:
        known = {"me.a": manifests.parse(manifest_text("Me.A", name="Pretty A"))}
        self.assertEqual(display_name("pathoschild.contentpatcher", known), "Content Patcher")
        self.assertEqual(display_name("me.a", known), "Pretty A")
        self.assertEqual(display_name("me.unknown", known), "me.unknown")


class LayoutTest(unittest.TestCase):
    def test_deeper_than_smapi_looks(self) -> None:
        checker = StardewValleyModDataChecker()
        self.assertIs(checker.dataLooksValid(make_tree(["a/b/c/d/Mod/manifest.json"])), VALID)
        self.assertIs(checker.dataLooksValid(make_tree(["a/b/c/d/e/Mod/manifest.json"])), INVALID)
        self.assertFalse(holds_mods(make_tree(["a/b/c/d/e/f/manifest.json"])))

    def test_ignored_folders_and_case(self) -> None:
        checker = StardewValleyModDataChecker()
        self.assertIs(checker.dataLooksValid(make_tree(["__MACOSX/Mod/manifest.json"])), INVALID)
        self.assertIs(checker.dataLooksValid(make_tree([".git/x", "Mod/MANIFEST.JSON"])), VALID)
        # the game's Mods folder in another case, with Mac junk next to it
        tree = make_tree(["MODS/ModA/manifest.json", ".DS_Store"])
        self.assertIs(checker.dataLooksValid(tree), FIXABLE)
        self.assertEqual(sorted(e.name() for e in checker.fix(tree)), ["ModA"])
        # a Mods folder that is itself a mod is kept
        self.assertIs(checker.dataLooksValid(make_tree(["Mods/manifest.json", "Mods/x.dll"])), VALID)

    def test_inside_a_fixable_archive(self) -> None:
        checker = StardewValleyModDataChecker()
        tree = make_tree(["Mods/ModA/manifest.json"])
        self.assertIs(checker.dataLooksValid(tree.find("Mods/ModA")), FIXABLE)

    def test_organizer_not_ready(self) -> None:
        class Organizer:
            def virtualFileTree(self):
                raise RuntimeError("not yet")

        checker = StardewValleyModDataChecker(Organizer())
        self.assertIs(checker.dataLooksValid(make_tree(["ChirpValley/i18n/zh.json"])), INVALID)


class ContentTest(unittest.TestCase):
    def test_contents_and_errors(self) -> None:
        content = StardewValleyModDataContent()
        self.assertEqual([c.id for c in content.getAllContents()], [int(c) for c in Content])
        self.assertEqual(content.getContentsFor(make_tree([".hidden/manifest.json", "Mod/i18n"])), [])
        self.assertEqual(content.getContentsFor(None), [])
        self.assertEqual([f.name() for f in mod_folders(make_tree(["__MACOSX/a/manifest.json", "A/manifest.json"]))],
                         ["A"])
        # a folder named like a DLL is no code
        self.assertEqual(contents_of(make_tree(["A/manifest.json", "A/x.dll/", "A/i18n"])), {Content.CONTENT_PACK})


class GameTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="sdv-cov-game-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.game.init(self.organizer)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_version_and_saves(self) -> None:
        self.assertEqual(self.game.gameVersion(), "")
        self.game.setGamePath(self.tmp)
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", return_value="1.6.15"):
            self.assertEqual(self.game.gameVersion(), "1.6.15")
        with mock.patch.object(plugin_module.BasicGame, "gameVersion", side_effect=RuntimeError("no exe")):
            self.assertEqual(self.game.gameVersion(), "")
        self.assertEqual(self.game.listSaves(QDir(str(self.tmp / "no saves"))), [])

    def test_proton_paths(self) -> None:
        self.game.setGamePath("Z:/home/deck/.steam/steam/steamapps/common/Stardew Valley")
        smapi, game = (e.binary().absoluteFilePath() for e in self.game.executables())
        self.assertTrue(smapi.endswith("Stardew Valley/StardewModdingAPI.exe"))
        self.assertTrue(game.endswith("Stardew Valley/Stardew Valley.exe"))


class CheckPluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="sdv-cov-check-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.game.init(self.organizer)
        self.game_dir = self.tmp / "Stardew Valley"
        (self.game_dir / "Mods").mkdir(parents=True)
        (self.game_dir / "Stardew Valley.exe").write_text("exe")
        self.organizer.game_dir = self.game_dir
        self.game.setGamePath(self.game_dir)
        self.check = StardewValleyCheck()
        self.check.init(self.organizer)
        # mobase reads the version resource of SMAPI's files (not of fake ones)
        self.smapi = "4.1.10"
        patcher = mock.patch.object(sdv_plugins.mobase, "getFileVersion", lambda path: self.smapi)
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install_smapi(self) -> None:
        (self.game_dir / "StardewModdingAPI.dll").write_text("dll")
        (self.game_dir / "Mods/ConsoleCommands").mkdir()
        (self.game_dir / "Mods/ConsoleCommands/manifest.json").write_text(manifest_text("SMAPI.ConsoleCommands"))

    def mod(self, name: str, folder: str, text: str) -> Path:
        root = self.organizer.add_mod(name, [f"{folder}/manifest.json"])
        (root / folder / "manifest.json").write_text(text, encoding="utf-8")
        return root

    def test_identity(self) -> None:
        check = self.check
        self.assertEqual(check.name(), "Stardew Valley Mod Check")
        self.assertEqual(check.localizedName(), "Stardew Valley Mod Check")
        self.assertEqual(check.author(), "MO2 Revamped")
        self.assertIn("SMAPI", check.description())
        self.assertIsInstance(check.version(), mobase.VersionInfo)
        self.assertEqual(len(check.requirements()), 1)
        self.assertEqual(check.settings(), [])
        self.assertFalse(check.hasGuidedFix(MISSING))
        check.startGuidedFix(MISSING)
        self.assertEqual(check.shortDescription(42), "")
        self.assertEqual([type(p) for p in sdv_plugins.createPlugins()], [StardewValleyCheck])

    def test_smapi_version(self) -> None:
        self.assertIsNone(sdv_plugins.smapi_version(self.game_dir))
        (self.game_dir / "StardewModdingAPI.exe").write_text("exe")
        self.assertEqual(sdv_plugins.smapi_version(self.game_dir), "4.1.10")
        self.smapi = ""
        self.assertEqual(sdv_plugins.smapi_version(self.game_dir), "")
        with mock.patch.object(sdv_plugins.mobase, "getFileVersion", side_effect=RuntimeError("locked")):
            self.assertEqual(sdv_plugins.smapi_version(self.game_dir), "")
        self.assertEqual(sdv_plugins.nexus_link("me.unknown"), "")

    def test_every_problem_text(self) -> None:
        self.install_smapi()
        self.smapi = "3.18.0"
        self.mod("Lib", "Lib", manifest_text("Me.Lib", name="Old <Lib>", version="1.0.0"))
        self.mod("Uses lib", "Uses", manifest_text("Me.Uses", MinimumApiVersion="4.0.0",
                                                     Dependencies=[{"UniqueID": "Me.Lib", "MinimumVersion": "2.0"}]))
        for index in range(14):
            self.mod(f"Needs {index}", f"Needs{index}", manifest_text(
                f"Me.Needs{index}", Dependencies=[{"UniqueID": f"Me.Missing{index}"}]))
        # a copy of SMAPI's own mod in a mod
        self.mod("Old SMAPI mod", "ConsoleCommands v2", manifest_text("SMAPI.ConsoleCommands"))
        self.assertEqual(self.check.activeProblems(), [MISSING, TOO_OLD, SMAPI_TOO_OLD, DUPLICATES])
        for key in (MISSING, TOO_OLD, SMAPI_TOO_OLD, DUPLICATES):
            self.assertTrue(self.check.shortDescription(key))
        missing = self.check.fullDescription(MISSING)
        self.assertIn("and 2 more", missing)
        self.assertIn("me.missing0", missing)
        self.assertIn("Uses lib needs Old &lt;Lib&gt; 2.0 or newer; 1.0.0 is installed",
                      self.check.fullDescription(TOO_OLD))
        smapi_text = self.check.fullDescription(SMAPI_TOO_OLD)
        self.assertIn("(3.18.0)", smapi_text)
        self.assertIn("SMAPI 4.0.0 or newer", smapi_text)
        duplicates = self.check.fullDescription(DUPLICATES)
        self.assertIn("the game&#x27;s Mods folder, Old SMAPI mod", duplicates)

    def test_no_smapi_text(self) -> None:
        for index in range(13):
            self.mod(f"Mod & {index}", f"M{index}", manifest_text(f"Me.M{index}"))
        self.assertEqual(self.check.activeProblems(), [NO_SMAPI])
        text = self.check.fullDescription(NO_SMAPI)
        self.assertIn("Mod &amp; 0", text)
        self.assertIn("and 1 more", text)

    def test_smapi_installed_outside_mo2(self) -> None:
        self.mod("A", "A", manifest_text("Me.A"))
        self.assertEqual(self.check.activeProblems(), [NO_SMAPI])
        # its installer ran while MO2 was open: no change MO2 tells about
        self.install_smapi()
        self.assertEqual(self.check.activeProblems(), [])
        # without a change the report is kept
        self.assertIs(self.check._current(), self.check._current())

    def test_changes_and_failures(self) -> None:
        self.mod("A", "A", manifest_text("Me.A", Dependencies=[{"UniqueID": "Me.B"}]))
        self.assertEqual(self.check.activeProblems(), [NO_SMAPI, MISSING])
        self.mod("B", "B", manifest_text("Me.B"))
        self.check._mod_files_changed("B")
        self.assertEqual(self.check.activeProblems(), [NO_SMAPI])
        with mock.patch.object(sdv_plugins, "analyze", side_effect=RuntimeError("broken")):
            self.check._changed()
            self.assertEqual(self.check.activeProblems(), [])
        # not installed, or another game
        self.game.setGamePath("")
        self.check._changed()
        self.assertEqual(self.check.activeProblems(), [])
        self.organizer.game = None
        self.check._changed()
        self.assertEqual(self.check.activeProblems(), [])


if __name__ == "__main__":
    unittest.main()
