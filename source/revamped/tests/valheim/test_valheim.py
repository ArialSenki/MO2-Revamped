"""The Valheim plugin: layouts, content, settings files, checks, saves and the
game plugin over a fake game folder."""

from __future__ import annotations

import shutil
import struct
import tempfile
import time
import unittest
from pathlib import Path

import mobase
from PyQt6.QtCore import QDir

from basic_games.games import game_valheim as plugin_module
from basic_games.games.valheim import configs
from basic_games.games.valheim.checks import ModFolder, analyze
from basic_games.games.valheim.content import Content, contents_of
from basic_games.games.valheim.layout import ValheimModDataChecker
from basic_games.games.revamped_shared import dotnet
from basic_games.games.valheim.plugins import MISSING, NO_BEPINEX, ValheimCheck
from basic_games.games.valheim.saves import list_saves, read_world
from fake_organizer import FakeOrganizer
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID
Game = plugin_module.ValheimGame


class LayoutTest(unittest.TestCase):
    checker = ValheimModDataChecker()

    def check(self, paths):
        return self.checker.dataLooksValid(make_tree(paths))

    def fixed(self, paths):
        tree = make_tree(paths)
        self.assertIs(self.checker.dataLooksValid(tree), FIXABLE, paths)
        return tree_paths(self.checker.fix(tree))

    def test_valid(self) -> None:
        for paths in (
            ["BepInEx/plugins/EpicLoot/EpicLoot.dll"],
            ["winhttp.dll", "doorstop_config.ini", "BepInEx/core/BepInEx.dll"],
            ["InSlimVML/Mods/Foo_VML.dll", "inslimvml.ini"],
            # a Thunderstore package already in place keeps its manifest
            ["manifest.json", "icon.png", "BepInEx/plugins/A.dll"],
        ):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), VALID)

    def test_fixes(self) -> None:
        # Thunderstore: its package files go, the plugin goes to BepInEx/plugins
        self.assertEqual(
            self.fixed(["manifest.json", "icon.png", "README.md", "EpicLoot.dll", "EpicLoot.cfg"]),
            ["BepInEx/config/EpicLoot.cfg", "BepInEx/plugins/EpicLoot.dll"],
        )
        self.assertEqual(
            self.fixed(["manifest.json", "plugins/Jotunn/Jotunn.dll"]),
            ["BepInEx/plugins/Jotunn/Jotunn.dll"],
        )
        self.assertEqual(
            self.fixed(["BepInExPack_Valheim/winhttp.dll", "BepInExPack_Valheim/BepInEx/core/BepInEx.dll"]),
            ["BepInEx/core/BepInEx.dll", "winhttp.dll"],
        )
        self.assertEqual(self.fixed(["Foo_VML.dll"]), ["InSlimVML/Mods/Foo_VML.dll"])
        # a loose DLL takes its data folders with it
        self.assertEqual(
            self.fixed(["MyMod.dll", "Sprites/a.png"]),
            ["BepInEx/plugins/MyMod.dll", "BepInEx/plugins/Sprites/a.png"],
        )
        # a plugin folder with its DLL deeper down
        self.assertEqual(
            self.fixed(["MyMod/bin/MyMod.dll", "MyMod/Bundles/x.bundle"]),
            ["BepInEx/plugins/MyMod/Bundles/x.bundle", "BepInEx/plugins/MyMod/bin/MyMod.dll"],
        )
        # a plugin's own folder keeps its data files with it (real Nexus mods:
        # ExpandWorld, Thoracle)
        self.assertEqual(
            self.fixed(["ExpandWorld/ExpandWorld.WorldEditor.dll", "ExpandWorld/worldeditor.json", "ExpandWorld/README.txt"]),
            ["BepInEx/plugins/ExpandWorld/ExpandWorld.WorldEditor.dll", "BepInEx/plugins/ExpandWorld/README.txt",
             "BepInEx/plugins/ExpandWorld/worldeditor.json"],
        )
        self.assertEqual(
            self.fixed(["README.md", "icon.png", "manifest.json", "Thoracle/Thoracle.dll", "Thoracle/Thoracle.pdb"]),
            ["BepInEx/plugins/Thoracle/Thoracle.dll", "BepInEx/plugins/Thoracle/Thoracle.pdb"],
        )
        self.assertEqual(
            self.fixed(["My Mod v2/plugins/MyMod.dll", "My Mod v2/readme.txt"]),
            ["BepInEx/plugins/MyMod.dll", "readme.txt"],
        )

    def test_invalid(self) -> None:
        for paths in (["readme.txt"], ["manifest.json", "icon.png", "README.md"], ["A/x.dll", "B/y.dll"],
                      ["Main.dll", "Optional/Extra.dll"]):
            with self.subTest(paths=paths):
                self.assertIs(self.check(paths), INVALID)


class ContentTest(unittest.TestCase):
    def test_contents(self) -> None:
        self.assertEqual(
            contents_of(make_tree([
                "winhttp.dll", "BepInEx/core/BepInEx.dll", "BepInEx/plugins/A/A.dll", "BepInEx/config/a.cfg",
                "BepInEx/patchers/P.dll", "inslimvml.ini", "unstripped_corlib/x.dll", "AdvancedBuilder/Builds/a.vbuild",
            ])),
            set(Content),
        )


def write(path: Path, data: bytes | str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
    return path


class FakeIndex(dotnet.AssemblyIndex):
    """An index whose DLLs declare what a test says, by file name."""

    def __init__(self, declared: dict[str, dotnet.AssemblyInfo]) -> None:
        super().__init__()
        self.declared = declared

    def info(self, path: Path) -> dotnet.AssemblyInfo:
        return self.declared.get(path.name, dotnet.EMPTY)


def declares(plugins=(), required=()) -> dotnet.AssemblyInfo:
    return dotnet.AssemblyInfo(frozenset(plugins), frozenset(required))


class ConfigsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-cfg-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_plan_by_plugin_guid(self) -> None:
        overwrite = self.tmp / "overwrite"
        loot = self.tmp / "mods/Epic Loot"
        other = self.tmp / "mods/Other"
        checker = self.tmp / "mods/Compat checker"
        write(loot / "BepInEx/plugins/EpicLoot/EpicLoot.dll", "x")
        write(other / "BepInEx/plugins/Other.dll", "x")
        write(checker / "BepInEx/plugins/Checker.dll", "x")
        index = FakeIndex({
            "EpicLoot.dll": declares(["randyknapp.mods.epicloot"]),
            "Other.dll": declares(["other.guid"]),
            # names Epic Loot (a dependency): never its owner
            "Checker.dll": declares(["checker"], required=["randyknapp.mods.epicloot"]),
        })
        write(overwrite / "BepInEx/config/randyknapp.mods.epicloot.cfg", "## Settings file\n")
        # named otherwise, its header says whose it is
        write(overwrite / "BepInEx/config/renamed.cfg", "## Settings file was created by plugin Other v1\n## Plugin GUID: other.guid\n")
        write(overwrite / "BepInEx/config/nobody.cfg", "x")
        write(overwrite / "BepInEx/config/BepInEx.cfg", "x")
        mods = [("Epic Loot", loot), ("Other", other), ("Compat checker", checker)]
        moves = configs.plan(overwrite, mods, True, index)
        self.assertEqual(sorted((m.source.name, m.mod) for m in moves),
                         [("randyknapp.mods.epicloot.cfg", "Epic Loot"), ("renamed.cfg", "Other")])
        # without reading headers, only the file name counts
        self.assertEqual([m.source.name for m in configs.plan(overwrite, mods, False, index)],
                         ["randyknapp.mods.epicloot.cfg"])
        self.assertEqual(configs.apply(moves), 2)
        self.assertTrue((loot / "BepInEx/config/randyknapp.mods.epicloot.cfg").is_file())
        self.assertTrue((overwrite / "BepInEx/config/nobody.cfg").is_file())

    def test_never_over_a_mod_file_or_to_two_mods(self) -> None:
        overwrite = self.tmp / "overwrite"
        a, b = self.tmp / "mods/A", self.tmp / "mods/B"
        write(a / "BepInEx/plugins/A.dll", "x")
        write(a / "BepInEx/config/a.guid.cfg", "the mod's own")
        write(b / "BepInEx/plugins/B.dll", "x")
        write(overwrite / "BepInEx/config/a.guid.cfg", "x")
        write(overwrite / "BepInEx/config/shared.cfg", "x")
        index = FakeIndex({"A.dll": declares(["a.guid", "shared"]), "B.dll": declares(["shared"])})
        self.assertEqual(configs.plan(overwrite, [("A", a), ("B", b)], True, index), [])


class DotnetTest(unittest.TestCase):
    """The metadata reader on real plugins from Nexus Mods (Hugin and Jotunn,
    downloaded to D:\mo2build\nexus-test-downloads\valheim for the tests)."""

    FOLDER = Path(r"D:\mo2build\nexus-test-downloads\valheim")

    def dll(self, name: str) -> Path:
        found = list(self.FOLDER.rglob(name)) if self.FOLDER.is_dir() else []
        if not found:
            self.skipTest(f"{name} isn't downloaded")
        return found[0]

    def test_real_plugins(self) -> None:
        hugin = dotnet.read_assembly(self.dll("Hugin.dll"))
        # Hugin uses Jotunn only when present: a soft dependency
        self.assertEqual(hugin, declares(["Hugin"]))
        jotunn = dotnet.read_assembly(self.dll("Jotunn.dll"))
        # [GeneratedCode("simple-json", "1.0.0")] has a dependency's shape
        # but isn't one
        self.assertEqual(jotunn, declares(["com.jotunn.jotunn"]))

    def test_not_dotnet(self) -> None:
        for path in (Path(r"C:\Windows\System32\winhttp.dll"), Path(__file__)):
            self.assertEqual(dotnet.read_assembly(path), dotnet.EMPTY)

    @staticmethod
    def metadata(attribute_rows: int = 1, padding: int = 0) -> tuple[bytes, bytes, bytes]:
        """#~, #Strings and #Blob streams of a one-attribute assembly:
        [BepInPlugin("abc")] on a TypeRef/MemberRef pair; `attribute_rows`
        claims that many CustomAttribute rows, `padding` zeros follow the
        tables."""
        head = bytes(6) + bytes([0, 1]) + struct.pack("<QQ", 1 << 1 | 1 << 0x0A | 1 << 0x0C, 0)
        head += struct.pack("<III", 1, 1, attribute_rows)
        type_ref = struct.pack("<HHH", 0, 9, 1)
        member_ref = struct.pack("<HHH", (1 << 3) | 1, 0, 0)
        attribute = struct.pack("<HHH", 0, (1 << 3) | 3, 1)
        tables = head + type_ref + member_ref + attribute * min(attribute_rows, 1) + bytes(padding)
        return tables, b"\0BepInEx\0BepInPlugin\0", b"\0" + bytes([8]) + b"\x01\x00\x03abc\x00\x00"

    def test_synthetic_plugin(self) -> None:
        plugins, required = dotnet.bepinex_attributes(*self.metadata())
        self.assertEqual((plugins, required), ({"abc"}, set()))

    def test_crafted_tables_finish_fast(self) -> None:
        # a few MB of zeros claiming a million attribute rows: the tables
        # run past the stream
        tables, strings, blobs = self.metadata(attribute_rows=1_000_000, padding=4 * 1024 * 1024)
        start = time.monotonic()
        with self.assertRaises(ValueError):
            dotnet.bepinex_attributes(tables, strings, blobs)
        self.assertLess(time.monotonic() - start, 2)

    def test_many_attribute_rows_are_capped(self) -> None:
        # rows that do fit in the stream, more than the cap: still fast
        count = dotnet._ATTRIBUTE_ROWS_LIMIT * 3
        tables, strings, blobs = self.metadata(attribute_rows=count, padding=count * 6)
        start = time.monotonic()
        dotnet.bepinex_attributes(tables, strings, blobs)
        self.assertLess(time.monotonic() - start, 2)

    def test_encodings(self) -> None:
        self.assertEqual(dotnet._packed(bytes([0x03]), 0), (3, 1))
        self.assertEqual(dotnet._packed(bytes([0x80, 0x80]), 0), (0x80, 2))
        self.assertEqual(dotnet._packed(bytes([0xC0, 0x00, 0x40, 0x00]), 0), (0x4000, 4))
        self.assertEqual(dotnet._ser_string(b"\x03abc", 0), ("abc", 4))
        self.assertEqual(dotnet._ser_string(b"\xff", 0), (None, 1))


class ChecksTest(unittest.TestCase):
    def test_frameworks(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="vh-chk-"))
        try:
            plugin = tmp / "mods/Plugin"
            write(plugin / "BepInEx/plugins/P.dll", "x")
            index = FakeIndex({
                "P.dll": declares(["me.plugin"], required=["com.jotunn.jotunn"]),
                "Jotunn.dll": declares(["com.jotunn.jotunn"]),
            })
            report = analyze([ModFolder("Plugin", plugin)], tmp / "game", index)
            self.assertEqual(report.without_bepinex, ["Plugin"])
            self.assertEqual(report.missing, {"com.jotunn.jotunn": ["Plugin"]})
            pack = tmp / "mods/BepInExPack"
            write(pack / "winhttp.dll", "x")
            write(pack / "BepInEx/core/BepInEx.dll", "x")
            jotunn = tmp / "mods/Jotunn"
            write(jotunn / "BepInEx/plugins/Jotunn/Jotunn.dll", "x")
            mods = [ModFolder("BepInExPack", pack), ModFolder("Jotunn", jotunn), ModFolder("Plugin", plugin)]
            report = analyze(mods, tmp / "game", index)
            self.assertEqual((report.without_bepinex, report.missing), ([], {}))
            # Jotunn installed in the game folder itself counts too
            write(tmp / "game/BepInEx/plugins/Jotunn/Jotunn.dll", "x")
            report = analyze([ModFolder("BepInExPack", pack), ModFolder("Plugin", plugin)], tmp / "game", index)
            self.assertEqual(report.missing, {})
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


def fwl(name: str, seed: str) -> bytes:
    def string(text: str) -> bytes:
        data = text.encode("utf-8")
        return bytes([len(data)]) + data

    body = struct.pack("<i", 34) + string(name) + string(seed) + struct.pack("<i", 123)
    return struct.pack("<i", len(body)) + body


class SavesTest(unittest.TestCase):
    def test_saves(self) -> None:
        tmp = Path(tempfile.mkdtemp(prefix="vh-saves-"))
        try:
            write(tmp / "worlds_local/Midgard.fwl", fwl("Midgard", "HnK3q8Ya"))
            write(tmp / "worlds_local/Midgard.db", "db")
            write(tmp / "characters_local/Ragnar.fch", "fch")
            write(tmp / "characters/Old.fch", "fch")
            write(tmp / "worlds_local/broken.fwl", b"\x01")
            names = sorted(s.getName() for s in list_saves(tmp))
            self.assertEqual(names, ["[Character (cloud)] Old", "[Character] Ragnar", "[World] Midgard", "[World] broken"])
            self.assertEqual(read_world(tmp / "worlds_local/Midgard.fwl")["Seed"], "HnK3q8Ya")
            world = next(s for s in list_saves(tmp) if "Midgard" in s.getName())
            self.assertEqual(len(world.allFiles()), 2)
        finally:
            shutil.rmtree(tmp, ignore_errors=True)


class PluginTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="vh-test-"))
        self.organizer = FakeOrganizer(self.tmp / "instance")
        self.game = Game()
        self.organizer.game = self.game
        self.assertTrue(self.game.init(self.organizer))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self) -> Path:
        game_dir = self.tmp / "Valheim"
        write(game_dir / "valheim.exe", "exe")
        self.organizer.game_dir = game_dir
        self.game.setGamePath(game_dir)
        return game_dir

    def test_identity(self) -> None:
        self.assertEqual(self.game.name(), "Valheim Support Plugin")
        self.assertEqual(self.game.gameName(), "Valheim")
        self.assertEqual(self.game.gameShortName(), "valheim")
        self.assertEqual(self.game.nexusGameID(), 3667)
        self.assertEqual([s.key for s in self.game.settings()], ["sync_overwrite", "search_overwrite_file_content"])

    def test_forced_load_only_with_bepinex(self) -> None:
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.install()
        self.assertEqual(self.game.executableForcedLoads(), [])
        self.organizer.add_mod("BepInExPack", ["winhttp.dll", "BepInEx/core/BepInEx.dll"])
        self.assertEqual([f.library() for f in self.game.executableForcedLoads()], ["winhttp.dll"])

    def test_check_plugin(self) -> None:
        self.install()
        self.organizer.add_mod("Plugin", ["BepInEx/plugins/P.dll"])
        check = ValheimCheck()
        check.init(self.organizer)
        original = dotnet.SHARED.info
        dotnet.SHARED.info = lambda path: declares(["me.p"], ["com.jotunn.jotunn"]) if path.name == "P.dll" else dotnet.EMPTY
        try:
            self.assertEqual(check.activeProblems(), [NO_BEPINEX, MISSING])
            text = check.fullDescription(MISSING)
            self.assertIn("Jotunn", text)
            self.assertIn("nexusmods.com/valheim/mods/1138", text)
        finally:
            dotnet.SHARED.info = original


if __name__ == "__main__":
    unittest.main()
