"""The Stardew Valley plugin: manifests, layouts, content, checks, saves, and
the game plugin over a fake game folder."""

from __future__ import annotations

import json
import os
import shutil
import tempfile
import time
import unittest
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_stardewvalley as plugin_module
from basic_games.games.stardewvalley import manifest as manifests
from basic_games.games.stardewvalley.checks import ManifestCache, ModManifests, analyze
from basic_games.games.stardewvalley.content import Content, contents_of
from basic_games.games.stardewvalley.layout import StardewValleyModDataChecker
from basic_games.games.stardewvalley.plugins import DUPLICATES, MISSING, NO_SMAPI, StardewValleyCheck
from basic_games.games.stardewvalley.saves import list_saves, read_info
from fake_organizer import FakeOrganizer
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID
Game = plugin_module.StardewValleyGame


def manifest_text(uid, name=None, version="1.0.0", **extra):
    data = {"Name": name or uid, "Author": "x", "Version": version, "UniqueID": uid, **extra}
    return json.dumps(data)


class ManifestTest(unittest.TestCase):
    def test_tolerant_json(self) -> None:
        text = '﻿{\n // a comment\n "Name": "A", /* block */ "UniqueID": "Me.A",\n' \
               ' "Version": "1.2.0", "Url": "http://x//y",\n "Dependencies": [{"UniqueID": "Pathoschild.ContentPatcher",}],\n}'
        parsed = manifests.parse(text)
        self.assertEqual(parsed.unique_id, "me.a")
        self.assertEqual([d.unique_id for d in parsed.needs()], ["pathoschild.contentpatcher"])

    def test_content_pack_and_optional(self) -> None:
        parsed = manifests.parse(manifest_text(
            "Me.Pack", ContentPackFor={"UniqueID": "Pathoschild.ContentPatcher", "MinimumVersion": "2.0.0"},
            Dependencies=[{"UniqueID": "Other.Mod", "IsRequired": False}]))
        self.assertEqual([(d.unique_id, d.minimum) for d in parsed.needs()], [("pathoschild.contentpatcher", "2.0.0")])

    def test_bad_manifests(self) -> None:
        for text in ("", "[]", "{", '{"Name": "no id"}', '{"UniqueID": "   "}'):
            self.assertIsNone(manifests.parse(text), text)
        # wrong types are read as missing, never fail the manifest
        odd = manifests.parse('{"UniqueID": "Me.Odd", "Name": null, "Dependencies": 5, "Version": 2}')
        self.assertEqual((odd.name, odd.dependencies, odd.version), ("Me.Odd", [], ""))

    def test_comments_and_commas_in_strings_stay(self) -> None:
        text = '{"UniqueID": "Me.A", "Name": "a /* b */ // c, }", /* x */ "Version": "1", // t' + "\n}"
        parsed = manifests.parse(text)
        self.assertEqual((parsed.unique_id, parsed.name, parsed.version), ("me.a", "a /* b */ // c, }", "1"))
        self.assertEqual(manifests.loads('[1, 2, /* c */ ]'), [1, 2])
        self.assertEqual(manifests.loads('{"a": [1,\n 2 , // x\n ], }'), {"a": [1, 2]})

    def test_crafted_manifests_finish_fast(self) -> None:
        for text in ("{" + "/* " * 80000, '{"UniqueID": "a", ' + '"' * 100000, '{"a": 1' + "," * 100000, "x" * 300000):
            start = time.monotonic()
            self.assertIsNone(manifests.parse(text))
            self.assertLess(time.monotonic() - start, 2)

    def test_big_manifest_is_unreadable(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="sdv-manifest-"))
        try:
            padding = " " * (manifests.MAX_SIZE + 1)
            (tmp / "manifest.json").write_text('{"UniqueID": "Me.A"}' + padding, encoding="utf-8")
            self.assertIsNone(manifests.read(tmp / "manifest.json"))
            (tmp / "manifest.json").write_text('{"UniqueID": "Me.A"}', encoding="utf-8")
            self.assertEqual(manifests.read(tmp / "manifest.json").unique_id, "me.a")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_versions(self) -> None:
        self.assertTrue(manifests.older("1.9.9", "2.0.0"))
        self.assertFalse(manifests.older("2.0.0-beta.2", "2.0.0"))
        self.assertFalse(manifests.older("4.1.10.0", "4.1.0"))
        self.assertFalse(manifests.older("", "1.0"))
        self.assertFalse(manifests.older("abc", "1.0"))


class LayoutTest(unittest.TestCase):
    checker = StardewValleyModDataChecker()

    def test_valid(self) -> None:
        for paths in (
            ["ModA/manifest.json", "ModA/ModA.dll"],
            ["[CP] Pack/manifest.json", "[CP] Pack/content.json", "[JA] Pack/manifest.json"],
            # SMAPI looks into a folder with no files of its own
            ["Pack/[CP] A/manifest.json", "Pack/[CP] B/manifest.json", "Pack/readme.txt"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.checker.dataLooksValid(make_tree(paths)), VALID)

    def test_mods_folder_taken_out(self) -> None:
        tree = make_tree(["Mods/ModA/manifest.json", "Mods/ModB/manifest.json"])
        self.assertIs(self.checker.dataLooksValid(tree), FIXABLE)
        self.assertEqual(tree_paths(self.checker.fix(tree)), ["ModA/manifest.json", "ModB/manifest.json"])

    def test_invalid(self) -> None:
        for paths in (
            ["manifest.json", "ModA.dll"],  # no folder name to give it
            ["Content/Characters/Abigail.xnb"],  # an XNB replacement for the game folder
            ["Pack/data.json", "Pack/[CP] A/manifest.json"],  # SMAPI won't look past data.json
            ["Pack/LICENSE", "Pack/[CP] A/manifest.json"],  # nor past a file it doesn't ignore
            ["readme.txt"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.checker.dataLooksValid(make_tree(paths)), INVALID)


class OverlayTest(unittest.TestCase):
    def test_files_for_installed_mods(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="sdv-overlay-"))
        try:
            organizer = FakeOrganizer(tmp / "instance")
            organizer.add_mod("Chirp Valley", ["ChirpValley/manifest.json", "ChirpValley/ChirpValley.dll"])
            checker = StardewValleyModDataChecker(organizer)
            # a translation for it: valid, it goes over the installed folder
            self.assertIs(checker.dataLooksValid(make_tree(["ChirpValley/i18n/zh.json"])), VALID)
            # one for a mod that isn't installed: the manual installer
            self.assertIs(checker.dataLooksValid(make_tree(["Other/i18n/zh.json"])), INVALID)
            self.assertIs(checker.dataLooksValid(make_tree(["ChirpValley/i18n/zh.json", "Other/x.json"])), INVALID)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class ContentTest(unittest.TestCase):
    def test_contents(self) -> None:
        self.assertEqual(
            contents_of(make_tree([
                "ModA/manifest.json", "ModA/ModA.dll", "ModA/i18n/de.json", "ModA/config.json",
                "[CP] B/manifest.json", "[CP] B/content.json", "Pack/[JA] C/manifest.json",
            ])),
            {Content.SMAPI_MOD, Content.TRANSLATIONS, Content.CONFIG, Content.CONTENT_PATCHER, Content.CONTENT_PACK},
        )


def mm(name, *manifest_texts, folders=None):
    parsed = [manifests.parse(t) for t in manifest_texts]
    return ModManifests(name, parsed, folders or [f"{name}/{i}".casefold() for i in range(len(parsed))])


class ChecksTest(unittest.TestCase):
    def test_analyze(self) -> None:
        game = mm("", manifest_text("SMAPI.ConsoleCommands"))
        mods = [
            mm("Content Patcher", manifest_text("Pathoschild.ContentPatcher", version="1.30.0")),
            mm("Pack", manifest_text("Me.Pack", ContentPackFor={"UniqueID": "Pathoschild.ContentPatcher", "MinimumVersion": "2.0.0"},
                                      MinimumApiVersion="4.2.0")),
            mm("Needs JA", manifest_text("Me.Items", Dependencies=[{"UniqueID": "spacechase0.JsonAssets"}])),
            mm("Copy 1", manifest_text("Me.Dup")),
            mm("Copy 2", manifest_text("Me.Dup")),
        ]
        report = analyze(mods, game, "4.1.10.0")
        self.assertEqual(report.missing, {"spacechase0.jsonassets": ["Needs JA"]})
        self.assertEqual(report.too_old, [("Pack", "Pathoschild.ContentPatcher", "1.30.0", "2.0.0")])
        self.assertEqual(report.smapi_too_old, [("Pack", "4.2.0", "4.1.10.0")])
        self.assertEqual(report.duplicates, {"me.dup": ["Copy 1", "Copy 2"]})
        # two MO2 mods with the same folder are one folder to SMAPI
        same = [mm("A", manifest_text("Me.Same"), folders=["same"]), mm("A fix", manifest_text("Me.Same"), folders=["same"])]
        self.assertEqual(analyze(same, None, "4.1.0").duplicates, {})
        # a copy in the game's own Mods folder counts
        game_copy = mm("", manifest_text("Me.Same"), folders=["old copy"])
        self.assertEqual(analyze(same, game_copy, "4.1.0").duplicates, {"me.same": ["", "A fix"]})
        self.assertEqual(report.no_smapi, [])
        self.assertEqual(analyze(mods[:1], None, None).no_smapi, ["Content Patcher"])


class SavesTest(unittest.TestCase):
    def test_saves(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="sdv-saves-"))
        try:
            for index, folder in enumerate(["Sunny_123", "Rocky_456"]):
                (tmp / folder).mkdir()
                (tmp / folder / folder).write_text("<SaveGame/>")
                os.utime(tmp / folder / folder, (1_700_000_000 + index,) * 2)
            (tmp / "Sunny_123/SaveGameInfo").write_text(
                "<Farmer><name>Ana</name><farmName>Sunny</farmName><money>1500</money>"
                "<dayOfMonthForSaveGame>12</dayOfMonthForSaveGame><seasonForSaveGame>2</seasonForSaveGame>"
                "<yearForSaveGame>3</yearForSaveGame><millisecondsPlayed>5400000</millisecondsPlayed></Farmer>",
                encoding="utf-8",
            )
            (tmp / "not_a_save").mkdir()
            saves = list_saves(tmp)
            self.assertEqual([s.getName() for s in saves], ["Rocky", "Ana (Sunny)"])
            info = read_info(tmp / "Sunny_123/SaveGameInfo")
            self.assertEqual(info["Date"], "Fall 12, Year 3")
            self.assertEqual(info["Play Time"], "01:30")
            self.assertEqual(read_info(tmp / "Rocky_456/SaveGameInfo"), {})
        finally:
            shutil.rmtree(tmp, ignore_errors=True)

    def test_absurd_play_time_is_left_out(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="sdv-saves-"))
        try:
            for played in ("9" * 5000, "²"):
                path = tmp / "SaveGameInfo"
                path.write_text(f"<Farmer><name>Ana</name><millisecondsPlayed>{played}</millisecondsPlayed></Farmer>", encoding="utf-8")
                info = read_info(path)
                self.assertEqual(info["Farmer"], "Ana")
                self.assertNotIn("Play Time", info)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class PluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="sdv-test-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, smapi: bool = True) -> Path:
        game_dir = self.tmp / "Stardew Valley"
        (game_dir / "Mods/ConsoleCommands").mkdir(parents=True)
        (game_dir / "Stardew Valley.exe").write_text("exe")
        if smapi:
            (game_dir / "StardewModdingAPI.exe").write_text("exe")
            (game_dir / "Mods/ConsoleCommands/manifest.json").write_text(manifest_text("SMAPI.ConsoleCommands"))
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def test_identity(self) -> None:
        self.assertEqual(self.game.name(), "Stardew Valley Support Plugin")
        self.assertEqual(self.game.gameName(), "Stardew Valley")
        self.assertEqual(self.game.gameShortName(), "stardewvalley")
        self.assertEqual(self.game.nexusGameID(), 1303)
        self.assertEqual(self.game.settings(), [])
        self.assertTrue(self.game.savesDirectory().absolutePath().replace("\\", "/").endswith("StardewValley/Saves"))

    def test_executables(self) -> None:
        self.assertEqual(self.game.executables(), [])
        self.install()
        self.assertEqual([e.title() for e in self.game.executables()], ["SMAPI", "Stardew Valley"])

    def test_check_plugin(self) -> None:
        self.install(smapi=False)
        mod = self.organizer.add_mod("Needs CP", ["[CP] Mine/manifest.json"])
        (mod / "[CP] Mine/manifest.json").write_text(manifest_text(
            "Me.Mine", ContentPackFor={"UniqueID": "Pathoschild.ContentPatcher"}))
        for name, folder in (("Copy 1", "Dup"), ("Copy 2", "Dup v2")):
            copy = self.organizer.add_mod(name, [f"{folder}/manifest.json"])
            (copy / folder / "manifest.json").write_text(manifest_text("Me.Dup"))
        check = StardewValleyCheck()
        check.init(self.organizer)
        self.assertEqual(check.activeProblems(), [NO_SMAPI, MISSING, DUPLICATES])
        self.assertIn("Content Patcher", check.fullDescription(MISSING))
        self.assertIn("nexusmods.com/stardewvalley/mods/1915", check.fullDescription(MISSING))


if __name__ == "__main__":
    unittest.main()
