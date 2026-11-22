"""revamped_shared, the code MO2 Revamped's own game plugins share: logging,
known folders, the file tree helpers, the per-mod cache, the BepInEx layout
checker and the .NET metadata reader, on the odd inputs real mods and
machines give (non-ASCII text, junctions, broken DLLs, missing folders)."""

from __future__ import annotations

import logging
import os
import shutil
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase

from basic_games.games import revamped_shared as shared
from basic_games.games.revamped_shared import dotnet
from basic_games.games.revamped_shared.bepinex import BepInExModDataChecker
from basic_games.games.revamped_shared.mod_cache import PerModCache, _folder_stamps
from basic_games.games.valheim.layout import ValheimModDataChecker
from harness import make_tree, tree_paths

VALID = mobase.ModDataChecker.VALID
FIXABLE = mobase.ModDataChecker.FIXABLE
INVALID = mobase.ModDataChecker.INVALID


# a mod list with everything MO2 can hand: inactive mods, separators, foreign
# mods, Overwrite and a name without a mod


class Mod:
    def __init__(self, name, path, separator=False, foreign=False, overwrite=False):
        self._name, self._path = name, path
        self._separator, self._foreign, self._overwrite = separator, foreign, overwrite

    def name(self):
        return self._name

    def absolutePath(self):
        return str(self._path)

    def isSeparator(self):
        return self._separator

    def isForeign(self):
        return self._foreign

    def isOverwrite(self):
        return self._overwrite


class ModList:
    def __init__(self):
        self.entries: list[tuple[str, bool, Mod | None]] = []

    def add(self, name, path, active=True, **kinds):
        mod = Mod(name, path, **kinds)
        self.entries.append((name, active, mod))
        return mod

    def allModsByProfilePriority(self):
        return [name for name, _, _ in self.entries]

    def state(self, name):
        active = next(a for n, a, _ in self.entries if n == name)
        return mobase.ModState.ACTIVE | mobase.ModState.VALID if active else mobase.ModState.VALID

    def getMod(self, name):
        return next(m for n, _, m in self.entries if n == name)


class Organizer:
    def __init__(self):
        self.mod_list = ModList()

    def modList(self):
        return self.mod_list


class LoggingTest(unittest.TestCase):
    def test_non_ascii_goes_through_python_logging(self) -> None:
        # a path with an accent: PyQt's qWarning takes ASCII only
        with self.assertLogs("mo2revamped", level="WARNING") as logs:
            shared.log_warning("C:/Users/José/Documents")
        self.assertEqual(logs.records[0].getMessage(), "C:/Users/José/Documents")
        self.assertEqual(logs.records[0].levelno, logging.WARNING)
        with self.assertLogs("mo2revamped", level="INFO") as logs:
            shared.log_info("Ñandú")
            shared.log_critical("日本語")
        self.assertEqual([r.levelno for r in logs.records], [logging.INFO, logging.CRITICAL])

    def test_ascii_and_none_never_raise(self) -> None:
        shared.log_info(None)
        shared.log_warning("plain")
        shared.log_critical(12)

    def test_translator_without_a_translation(self) -> None:
        self.assertEqual(shared.translator("NoSuchContext")("Some text"), "Some text")


class KnownFolderTest(unittest.TestCase):
    def test_fallback_for_a_bad_guid(self) -> None:
        with mock.patch.dict(os.environ, {"MO2R_COV_FOLDER": r"D:\Users\Ana\Docs"}):
            self.assertEqual(shared.known_folder("not a guid", "%MO2R_COV_FOLDER%/Games"), "D:/Users/Ana/Docs/Games")
        # an unknown variable stays as it is (expandvars)
        self.assertEqual(shared.known_folder("{00000000-0000-0000-0000-000000000000}", "%MO2R_NO_SUCH%/x"),
                         "%MO2R_NO_SUCH%/x")

    def test_real_folders_use_forward_slashes(self) -> None:
        for folder in (shared.documents_folder(), shared.roaming_app_data_folder(), shared.local_low_app_data_folder()):
            self.assertNotIn("\\", folder)
            self.assertTrue(folder)


class RootBuilderTest(unittest.TestCase):
    def test_names_and_errors(self) -> None:
        class Org:
            def __init__(self, enabled):
                self.enabled = enabled

            def isPluginEnabled(self, name):
                if self.enabled is None:
                    raise RuntimeError("MO2 not ready")
                return name in self.enabled

        self.assertTrue(shared.root_builder_enabled(Org({"Root Builder"})))
        self.assertTrue(shared.root_builder_enabled(Org({"RootBuilder"})))
        self.assertFalse(shared.root_builder_enabled(Org(set())))
        self.assertFalse(shared.root_builder_enabled(Org(None)))
        self.assertFalse(shared.root_builder_enabled(object()))


class TreeHelpersTest(unittest.TestCase):
    def test_is_doc(self) -> None:
        tree = make_tree(["LICENSE", "readme.TXT", "._mac", "Thumbs.db", "plugin.dll", "folder/x.dll"])
        docs = {e.name(): shared.is_doc(e) for e in tree}
        self.assertEqual(docs, {"LICENSE": True, "readme.TXT": True, "._mac": True, "Thumbs.db": True,
                                "plugin.dll": False, "folder": False})

    def test_find_dir_and_walk(self) -> None:
        tree = make_tree(["A/b.txt", "C.txt"])
        self.assertIsNone(shared.find_dir(tree, "C.txt"))
        self.assertIsNone(shared.find_dir(tree, "missing"))
        self.assertIsNotNone(shared.find_dir(tree, "a"))
        seen = []
        shared.walk_files(None, lambda p, e: seen.append(e) or False)
        self.assertEqual(seen, [])
        # stops at the first True
        shared.walk_files(make_tree(["a.txt", "b.txt", "c.txt"]), lambda p, e: seen.append(e.name()) or True)
        self.assertEqual(len(seen), 1)
        self.assertFalse(shared.contains_suffix(None, (".dll",)))
        self.assertTrue(shared.contains_suffix(make_tree(["deep/er/X.DLL"]), (".dll",)))

    def test_unwrap_with_a_name_taken(self) -> None:
        # the temporary name is already used in the archive
        tree = make_tree(["__mo2_revamped_unwrap__/keep.txt", "Wrapper/a.txt", "Wrapper/Sub/b.txt"])
        shared.unwrap(tree, shared.find_dir(tree, "Wrapper"))
        self.assertEqual(tree_paths(tree), ["Sub/b.txt", "__mo2_revamped_unwrap__/keep.txt", "a.txt"])

    def test_unwrap_a_folder_holding_its_own_name(self) -> None:
        # "Foo/Foo/x": merging it into its parent crashed MO2
        tree = make_tree(["Foo/Foo/x.txt", "Foo/y.txt"])
        shared.unwrap(tree, shared.find_dir(tree, "Foo"))
        self.assertEqual(tree_paths(tree), ["Foo/x.txt", "y.txt"])

    def test_unwrap_when_the_tree_refuses(self) -> None:
        class Tree:
            def __init__(self, moves):
                self.moves = moves
                self.inserted = []

            def exists(self, name):
                return False

            def move(self, entry, name):
                return self.moves

            def find(self, path, kind):
                return None

            def insert(self, entry, policy):
                self.inserted.append(entry)

        refusing = Tree(False)
        shared.unwrap(refusing, object())
        self.assertEqual(refusing.inserted, [])
        # moved, and then not found as a folder
        lost = Tree(True)
        shared.unwrap(lost, object())
        self.assertEqual(lost.inserted, [])

    def test_remove_junk_at_any_depth(self) -> None:
        tree = make_tree(["__MACOSX/._a.dll", "Mod/.DS_Store", "Mod/sub/desktop.ini", "Mod/sub/._x",
                          "Mod/a.dll", "Thumbs.db"])
        shared.remove_junk(tree)
        self.assertEqual(tree_paths(tree), ["Mod/a.dll", "Mod/sub/"])

    def test_prune_empty_goes_up(self) -> None:
        tree = make_tree(["a/b/c/", "a/keep.txt"])
        shared.prune_empty(shared.find_dir(tree, "a/b/c"))
        self.assertEqual(tree_paths(tree), ["a/keep.txt"])
        # the top folder itself is never removed
        top = make_tree([])
        shared.prune_empty(top)
        shared.prune_empty(None)
        self.assertEqual(len(top), 0)

    def test_lone_wrapper(self) -> None:
        self.assertEqual(shared.lone_wrapper(make_tree(["Pack/a.dll", "readme.md", "__MACOSX/x"])).name(), "Pack")
        self.assertIsNone(shared.lone_wrapper(make_tree(["Pack/a.dll", "other.dll"])))
        self.assertIsNone(shared.lone_wrapper(make_tree(["A/a.dll", "B/b.dll"])))
        self.assertIsNone(shared.lone_wrapper(make_tree(["BepInEx/a.dll"]), frozenset({"bepinex"})))
        self.assertIsNone(shared.lone_wrapper(make_tree(["readme.txt"])))


class ModCacheTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cov-cache-"))
        self.mods = self.tmp / "mods"
        self.mods.mkdir()
        self.organizer = Organizer()
        self.reads: list[str] = []

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def folder(self, name: str) -> Path:
        path = self.mods / name
        path.mkdir()
        (path / "file.txt").write_text("x")
        return path

    def read(self, name, path, mod):
        self.reads.append(name)
        return name

    def test_skips_what_isnt_an_active_mod(self) -> None:
        mods = self.organizer.mod_list
        mods.add("A", self.folder("A"))
        mods.add("Off", self.folder("Off"), active=False)
        mods.add("Sep_separator", self.folder("Sep_separator"), separator=True)
        mods.add("Foreign", self.folder("Foreign"), foreign=True)
        mods.add("Overwrite", self.folder("Overwrite"), overwrite=True)
        mods.entries.append(("Gone", True, None))
        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, self.read), ["A"])

    def test_read_once_while_the_folder_stays(self) -> None:
        a = self.folder("A")
        self.organizer.mod_list.add("A", a)
        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, self.read), ["A"])
        self.assertEqual(cache.active(self.organizer, self.read), ["A"])
        self.assertEqual(self.reads, ["A"])
        # installed again (by name or by the mod itself): read again
        cache.forget("A")
        cache.active(self.organizer, self.read)
        cache.forget(self.organizer.mod_list.getMod("A"))
        cache.active(self.organizer, self.read)
        self.assertEqual(self.reads, ["A", "A", "A"])
        # a forgotten name nobody has is fine
        cache.forget("Nobody")

    def test_a_changed_folder_is_read_again(self) -> None:
        a = self.folder("A")
        self.organizer.mod_list.add("A", a)
        cache: PerModCache[str] = PerModCache()
        cache.active(self.organizer, self.read)
        stamp = os.stat(a).st_mtime_ns
        (a / "new.txt").write_text("y")
        os.utime(a, ns=(stamp + 10_000_000_000, stamp + 10_000_000_000))
        cache.active(self.organizer, self.read)
        self.assertEqual(self.reads, ["A", "A"])

    def test_missing_folder_and_failing_reads(self) -> None:
        mods = self.organizer.mod_list
        # a mod whose folder was removed outside MO2, in a mods folder that
        # isn't there either
        mods.add("Removed", self.tmp / "elsewhere" / "Removed")
        mods.add("Raises", self.folder("Raises"))
        mods.add("Nothing", self.folder("Nothing"))
        mods.add("Fine", self.folder("Fine"))
        calls = []

        def read(name, path, mod):
            calls.append(name)
            if name == "Raises":
                raise OSError("removed while read")
            return None if name == "Nothing" else name

        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, read), ["Fine"])
        # what gave nothing is tried again next time
        self.assertEqual(cache.active(self.organizer, read), ["Fine"])
        self.assertEqual(calls, ["Raises", "Nothing", "Fine", "Raises", "Nothing"])

    def test_a_cached_mod_whose_folder_goes_away(self) -> None:
        a = self.folder("A")
        self.organizer.mod_list.add("A", a)
        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, self.read), ["A"])
        shutil.rmtree(a)
        self.assertEqual(cache.active(self.organizer, self.read), [])
        self.assertEqual(cache._entries, {})

    def test_a_junction_in_the_mods_folder(self) -> None:
        # mods kept on another drive, linked into the mods folder: the listing
        # doesn't follow links, so its folder is looked at by itself
        try:
            import _winapi

            target = self.tmp / "other drive" / "Linked"
            target.mkdir(parents=True)
            (target / "x.txt").write_text("x")
            _winapi.CreateJunction(str(target), str(self.mods / "Linked"))
        except (ImportError, OSError, AttributeError) as error:
            self.skipTest(f"no junction: {error}")
        self.organizer.mod_list.add("Linked", self.mods / "Linked")
        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, self.read), ["Linked"])
        self.assertEqual(cache.active(self.organizer, self.read), ["Linked"])
        self.assertEqual(self.reads, ["Linked"])
        os.rmdir(self.mods / "Linked")

    def test_folder_stamps(self) -> None:
        self.folder("A")
        (self.mods / "file.txt").write_text("not a folder")
        stamps = _folder_stamps([str(self.mods), str(self.tmp / "missing"), str(self.mods)])
        self.assertEqual(list(stamps), [os.path.normcase(os.path.normpath(str(self.mods / "A")))])

    def test_forward_slashes_and_case(self) -> None:
        # MO2 hands paths with forward slashes; Windows doesn't mind the case
        a = self.folder("Mod A")
        self.organizer.mod_list.add("Mod A", str(a).replace("\\", "/").upper())
        cache: PerModCache[str] = PerModCache()
        self.assertEqual(cache.active(self.organizer, self.read), ["Mod A"])
        self.assertEqual(cache.active(self.organizer, self.read), ["Mod A"])
        self.assertEqual(self.reads, ["Mod A"])


class BepInExCheckerTest(unittest.TestCase):
    """The shared checker by itself (no game's folders) and Valheim's rules."""

    def fixed(self, checker, paths):
        tree = make_tree(paths)
        self.assertIs(checker.dataLooksValid(tree), FIXABLE, paths)
        return tree_paths(checker.fix(tree))

    def test_plain_checker(self) -> None:
        checker = BepInExModDataChecker()
        self.assertIsNone(checker.extra_file_target("x.dll"))
        self.assertIsNone(checker.extra_folder_target(make_tree([])))
        self.assertEqual(self.fixed(checker, ["Mod.dll", "mod.cfg"]),
                         ["BepInEx/config/mod.cfg", "BepInEx/plugins/Mod.dll"])
        self.assertEqual(self.fixed(checker, ["patchers/P.dll"]), ["BepInEx/patchers/P.dll"])
        # BepInEx's own files in upper case
        self.assertIs(checker.dataLooksValid(make_tree(["WINHTTP.DLL", "BEPINEX/core/BepInEx.dll"])), VALID)

    def test_inside_a_fixable_archive(self) -> None:
        checker = ValheimModDataChecker()
        tree = make_tree(["manifest.json", "icon.png", "Mod.dll", "Data/x.json"])
        self.assertIs(checker.dataLooksValid(shared.find_dir(tree, "Data")), FIXABLE)

    def test_game_specific_targets(self) -> None:
        checker = ValheimModDataChecker()
        self.assertEqual(self.fixed(checker, ["House.vbuild"]), ["AdvancedBuilder/Builds/House.vbuild"])
        self.assertEqual(self.fixed(checker, ["Builds/House.vbuild"]), ["AdvancedBuilder/Builds/House.vbuild"])
        self.assertEqual(self.fixed(checker, ["sharedassets2.assets"]), ["valheim_Data/sharedassets2.assets"])
        self.assertEqual(self.fixed(checker, ["Translations/English/mod.json"]),
                         ["BepInEx/plugins/Translations/English/mod.json"])

    def test_wrappers(self) -> None:
        checker = ValheimModDataChecker()
        self.assertEqual(self.fixed(checker, ["Mod v1.2/BepInEx/plugins/M.dll", "Mod v1.2/readme.txt"]),
                         ["BepInEx/plugins/M.dll", "readme.txt"])
        # an extra folder from the archive's name, twice: never the plugin's
        # own folder, it holds BepInEx/
        self.assertEqual(self.fixed(checker, ["Mod v1.2/Mod v1.2/BepInEx/plugins/M.dll", "Mod v1.2/readme.txt"]),
                         ["BepInEx/plugins/M.dll", "readme.txt"])
        # four wrappers are more than anyone packs: left to the manual installer
        self.assertIs(checker.dataLooksValid(make_tree(["a/b/c/d/BepInEx/plugins/M.dll"])), INVALID)
        # a wrapper around nothing BepInEx loads: fix() leaves it
        tree = make_tree(["Wrapper/readme.txt"])
        self.assertIs(checker.dataLooksValid(tree), INVALID)
        self.assertEqual(tree_paths(checker.fix(tree)), ["Wrapper/readme.txt"])

    def test_junk_and_case(self) -> None:
        checker = ValheimModDataChecker()
        self.assertEqual(self.fixed(checker, ["__MACOSX/._Mod.dll", "Mod.DLL", ".DS_Store"]),
                         ["BepInEx/plugins/Mod.DLL"])
        self.assertEqual(self.fixed(checker, ["Plugins/Mod.dll"]), ["BepInEx/Plugins/Mod.dll"])
        self.assertIs(checker.dataLooksValid(make_tree(["BEPINEX/PLUGINS/Mod.dll"])), VALID)

    def test_thunderstore_files_stay_without_a_fix(self) -> None:
        checker = ValheimModDataChecker()
        tree = make_tree(["manifest.json", "icon.png", "README.md", "BepInEx/plugins/A.dll", "CHANGELOG.md"])
        self.assertIs(checker.dataLooksValid(tree), VALID)
        # with a real fix the package files go
        self.assertEqual(self.fixed(checker, ["manifest.json", "CHANGELOG.md", "LICENSE", "config/a.cfg"]),
                         ["BepInEx/config/a.cfg"])


def ser(text: str) -> bytes:
    data = text.encode("utf-8")
    return bytes([len(data)]) + data


def plugin_blob(guid: str) -> bytes:
    return b"\x01\x00" + ser(guid) + ser("Name") + ser("1.0.0") + b"\x00\x00"


def flags_blob(guid: str, flags: int) -> bytes:
    return b"\x01\x00" + ser(guid) + struct.pack("<i", flags) + b"\x00\x00"


def version_blob(guid: str) -> bytes:
    return b"\x01\x00" + ser(guid) + ser("1.2.3") + b"\x00\x00"


def metadata(attributes) -> tuple[bytes, bytes, bytes]:
    """#~, #Strings and #Blob of an assembly with these custom attributes:
    (namespace, type name, blob, owner, constructor), owner "typeref" or
    "typedef", constructor "memberref" or "methoddef"."""
    strings = bytearray(b"\0")
    offsets: dict[str, int] = {}

    def string(text: str) -> int:
        if text not in offsets:
            offsets[text] = len(strings)
            strings.extend(text.encode() + b"\0")
        return offsets[text]

    blobs = bytearray(b"\0")
    type_refs, member_refs, rows = b"", b"", b""
    for index, (namespace, name, blob, owner, ctor) in enumerate(attributes, start=1):
        type_refs += struct.pack("<HHH", 0, string(name), string(namespace))
        parent_tag = 1 if owner == "typeref" else 0
        member_refs += struct.pack("<HHH", (index << 3) | parent_tag, 0, 0)
        at = len(blobs)
        blobs.extend(bytes([len(blob)]) + blob)
        kind = (index << 3) | 3 if ctor == "memberref" else (index << 3) | 2
        rows += struct.pack("<HHH", 0, kind, at)
    count = len(attributes)
    head = bytes(6) + bytes([0, 1]) + struct.pack("<QQ", 1 << 0x01 | 1 << 0x0A | 1 << 0x0C, 0)
    head += struct.pack("<III", count, count, count)
    return head + type_refs + member_refs + rows, bytes(strings), bytes(blobs)


def pe_file(streams: list[tuple[str, bytes]], *, pe32plus: bool = False, signature: int = 0x424A5342,
            cli_rva: int = 0x2000, claimed_sizes: dict[str, int] | None = None) -> bytes:
    """A minimal PE file with a CLI header and these metadata streams."""
    claimed_sizes = claimed_sizes or {}
    section_rva, section_raw = 0x2000, 0x200
    cli = struct.pack("<IHHII", 72, 2, 5, section_rva + 72, 0).ljust(72, b"\0")
    version = b"v4.0.30319\0\0"
    root = struct.pack("<IHHII", signature, 1, 1, 0, len(version)) + version + struct.pack("<HH", 0, len(streams))
    headers_size = len(root) + sum(8 + ((len(name) + 4) & ~3) for name, _ in streams)
    headers, data = b"", b""
    for name, content in streams:
        offset = headers_size + len(data)
        size = claimed_sizes.get(name, len(content))
        raw_name = name.encode() + b"\0"
        headers += struct.pack("<II", offset, size) + raw_name.ljust((len(name) + 4) & ~3, b"\0")
        data += content + bytes(-len(content) % 4)
    body = cli + root + headers + data
    head = bytearray(section_raw)
    head[0:2] = b"MZ"
    pe = 0x80
    struct.pack_into("<I", head, 0x3C, pe)
    head[pe:pe + 4] = b"PE\0\0"
    optional_size = 240 if pe32plus else 224
    struct.pack_into("<HH", head, pe + 4, 0x8664 if pe32plus else 0x14C, 1)
    struct.pack_into("<H", head, pe + 20, optional_size)
    optional = pe + 24
    struct.pack_into("<H", head, optional, 0x20B if pe32plus else 0x10B)
    directories = optional + (112 if pe32plus else 96)
    struct.pack_into("<II", head, directories + 14 * 8, cli_rva, 72)
    table = optional + optional_size
    struct.pack_into("<IIII", head, table + 8, len(body), section_rva, len(body), section_raw)
    return bytes(head) + body


class DotnetTest(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="cov-dotnet-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def dll(self, data: bytes, name: str = "Plugin.dll") -> Path:
        path = self.tmp / name
        path.write_bytes(data)
        return path

    def assembly(self, attributes, **pe) -> dotnet.AssemblyInfo:
        tables, strings, blobs = metadata(attributes)
        data = pe_file([("#~", tables), ("#Strings", strings), ("#Blob", blobs), ("#GUID", bytes(16))], **pe)
        return dotnet.read_assembly(self.dll(data))

    def test_plugin_and_its_dependencies(self) -> None:
        attributes = [
            ("BepInEx", "BepInPlugin", plugin_blob("me.plugin"), "typeref", "memberref"),
            # (guid, DependencyFlags.HardDependency)
            ("BepInEx", "BepInDependency", flags_blob("com.jotunn.jotunn", 1), "typeref", "memberref"),
            # SoftDependency: used when there
            ("BepInEx", "BepInDependency", flags_blob("soft.one", 2), "typeref", "memberref"),
            # (guid, minimumVersion): always hard
            ("BepInEx", "BepInDependency", version_blob("needs.version"), "typeref", "memberref"),
            # requiring itself isn't a dependency
            ("BepInEx", "BepInDependency", flags_blob("me.plugin", 1), "typeref", "memberref"),
        ]
        expected = dotnet.AssemblyInfo(frozenset({"me.plugin"}), frozenset({"com.jotunn.jotunn", "needs.version"}))
        self.assertEqual(self.assembly(attributes), expected)
        # a 64-bit DLL has the longer optional header
        self.assertEqual(self.assembly(attributes, pe32plus=True), expected)

    def test_what_isnt_counted(self) -> None:
        attributes = [
            # the same name in another namespace
            ("System.CodeDom.Compiler", "BepInPlugin", plugin_blob("not.this"), "typeref", "memberref"),
            ("BepInEx", "BepInProcess", plugin_blob("valheim.exe"), "typeref", "memberref"),
            # an attribute class the assembly defines itself (MethodDef constructor)
            ("BepInEx", "BepInPlugin", plugin_blob("own.method"), "typeref", "methoddef"),
            # a MemberRef on a TypeDef
            ("BepInEx", "BepInPlugin", plugin_blob("own.type"), "typedef", "memberref"),
            # blobs a compiler never writes: no prolog, a null string, a string
            # past the blob, a bad compressed length, an empty GUID
            ("BepInEx", "BepInPlugin", b"\x02\x00" + ser("no.prolog"), "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", b"\x01\x00\xff\x00\x00", "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", b"\x01\x00\x30abc", "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", b"\x01\x00\xe0\x00\x00\x00", "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", b"\x01\x00\x00\x00\x00", "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", b"\x01\x00\x02\xff\xfe\x00\x00", "typeref", "memberref"),
            ("BepInEx", "BepInPlugin", plugin_blob("the.real.one"), "typeref", "memberref"),
        ]
        self.assertEqual(self.assembly(attributes), dotnet.AssemblyInfo(frozenset({"the.real.one"}), frozenset()))

    def test_non_ascii_guid(self) -> None:
        attributes = [("BepInEx", "BepInPlugin", plugin_blob("mod.ñandú"), "typeref", "memberref")]
        self.assertEqual(self.assembly(attributes).plugin_guids, frozenset({"mod.ñandú"}))

    def test_broken_files(self) -> None:
        good = metadata([("BepInEx", "BepInPlugin", plugin_blob("x"), "typeref", "memberref")])
        streams = [("#~", good[0]), ("#Strings", good[1]), ("#Blob", good[2])]
        cases = {
            "empty.dll": b"",
            "text.dll": "not a DLL at all".encode(),
            # MZ, and no PE header where it points
            "no_pe.dll": b"MZ" + bytes(0x3A) + struct.pack("<I", 0x40) + b"XX\0\0" + bytes(200),
            # its CLI header outside every section
            "outside.dll": pe_file(streams, cli_rva=0x9000),
            # no .NET metadata signature
            "no_metadata.dll": pe_file(streams, signature=0x12345678),
            # a stream that claims more than any plugin has
            "huge.dll": pe_file(streams, claimed_sizes={"#~": dotnet._STREAM_LIMIT + 1}),
            # cut short in the middle of its metadata
            "truncated.dll": pe_file(streams)[:0x200 + 90],
            # a native DLL: no CLI header
            "native.dll": pe_file(streams, cli_rva=0),
        }
        for name, data in cases.items():
            with self.subTest(name=name):
                self.assertEqual(dotnet.read_assembly(self.dll(data, name)), dotnet.EMPTY)
        self.assertEqual(dotnet.read_assembly(self.tmp / "missing.dll"), dotnet.EMPTY)
        # a folder named like a DLL
        (self.tmp / "folder.dll").mkdir()
        self.assertEqual(dotnet.read_assembly(self.tmp / "folder.dll"), dotnet.EMPTY)

    def test_low_level_readers(self) -> None:
        with self.assertRaises(ValueError):
            dotnet._packed(bytes([0xE0, 0, 0, 0]), 0)
        with self.assertRaises(ValueError):
            dotnet._ser_string(b"\x05ab", 0)
        self.assertEqual(dotnet._string_at(b"abc", 0), "abc")
        self.assertEqual(dotnet._string_at(b"a\xffb\0", 0), "a\ufffdb")
        tables, _, _ = metadata([("BepInEx", "BepInPlugin", plugin_blob("x"), "typeref", "memberref")])
        reader = dotnet._Tables(tables)
        self.assertEqual(reader.row_size(0x0C), 6)
        self.assertEqual(reader.coded("CustomAttributeType", 0), (-1, 0))
        self.assertEqual(reader.coded("CustomAttributeType", 7), (-1, 0))

    def test_wide_heaps_and_big_tables(self) -> None:
        # 4-byte heap indexes and a table past 65535 rows: the wider columns
        head = bytes(6) + bytes([7, 1]) + struct.pack("<QQ", 1 << 0x01, 0) + struct.pack("<I", 70_000)
        reader = dotnet._Tables(head + bytes(70_000 * 12))
        self.assertEqual(reader.row_size(0x01), 4 + 4 + 4)
        self.assertEqual(reader._width(0x01), 4)
        self.assertEqual(reader._width("TypeDefOrRef"), 4)

    def test_index_reads_once_per_file_state(self) -> None:
        tables, strings, blobs = metadata([("BepInEx", "BepInPlugin", plugin_blob("first"), "typeref", "memberref")])
        path = self.dll(pe_file([("#~", tables), ("#Strings", strings), ("#Blob", blobs)]))
        index = dotnet.AssemblyIndex()
        reads = []
        original = dotnet.read_assembly

        def counting(p):
            reads.append(p)
            return original(p)

        with mock.patch.object(dotnet, "read_assembly", counting):
            self.assertEqual(index.info(path).plugin_guids, frozenset({"first"}))
            self.assertEqual(index.info(path).plugin_guids, frozenset({"first"}))
            self.assertEqual(len(reads), 1)
            # the mod updated: another size, read again
            tables, strings, blobs = metadata([("BepInEx", "BepInPlugin", plugin_blob("second.guid"),
                                                "typeref", "memberref")])
            path.write_bytes(pe_file([("#~", tables), ("#Strings", strings), ("#Blob", blobs)]))
            self.assertEqual(index.info(path).plugin_guids, frozenset({"second.guid"}))
            self.assertEqual(len(reads), 2)
            self.assertEqual(index.info(self.tmp / "gone.dll"), dotnet.EMPTY)
        # dropped only when not looked at since the last call
        index.forget_others()
        self.assertEqual(len(index._cache), 1)
        index.forget_others()
        self.assertEqual(index._cache, {})

    def test_plugin_dlls(self) -> None:
        mod = self.tmp / "Mod"
        for relative in ("BepInEx/plugins/A.dll", "BepInEx/plugins/Sub/deeper/B.DLL", "BepInEx/plugins/readme.txt",
                         "BepInEx/config/c.cfg"):
            (mod / relative).parent.mkdir(parents=True, exist_ok=True)
            (mod / relative).write_text("x")
        self.assertEqual(sorted(p.name for p in dotnet.plugin_dlls(mod)), ["A.dll", "B.DLL"])
        self.assertEqual(dotnet.plugin_dlls(self.tmp / "nothing"), [])


if __name__ == "__main__":
    unittest.main()
