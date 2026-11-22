"""The Elden Ring archive installer (eldenring_mo2_archive_installer) outside
MO2: which archives it takes, how it lays them out, what it stores on the mod
after the install, and its two windows driven by their own buttons (their
exec() returns at once; nothing is shown)."""

from __future__ import annotations

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import mobase
from PyQt6 import QtWidgets

from eldenring_mo2_archive_installer import installer as installer_module

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from harness import make_tree

Installer = installer_module.EldenRingArchiveInstaller


def tree_files(tree) -> list[str]:
    return sorted(Installer._file_paths(tree))


class FakeName:
    """mobase.GuessedString as install() uses it."""

    def __init__(self, value: str):
        self.value, self.updates = value, []

    def __str__(self) -> str:
        return self.value

    def update(self, value: str, quality) -> None:
        self.value = value
        self.updates.append((value, quality))


class FakeInstalledMod:
    def __init__(self, root: Path, settings: dict | None = None, store: bool = True):
        self.root, self.settings, self.store = root, dict(settings or {}), store
        self.stored: dict = {}
        self.cleared = False

    def name(self) -> str:
        return self.root.name

    def absolutePath(self) -> str:
        return str(self.root)

    def pluginSettings(self, _plugin: str) -> dict:
        return self.settings

    def setPluginSetting(self, _plugin: str, key: str, value) -> bool:
        self.stored[key] = value
        return self.store

    def clearPluginSettings(self, _plugin: str) -> None:
        self.cleared = True


class FakeOtherGame:
    def gameShortName(self) -> str:
        return "skyrimse"

    def name(self) -> str:
        return "Skyrim"


def mod_folder(files: dict[str, bytes]) -> Path:
    root = Path(tempfile.mkdtemp(prefix="er-cov-mod-")) / "My Mod"
    for relative, data in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    root.mkdir(parents=True, exist_ok=True)
    return root


class InstallerCase(unittest.TestCase):
    def setUp(self):
        self._appdata = os.environ.get("APPDATA")
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.game = game_for(self.organizer)
        self.installer = Installer()
        self.assertTrue(self.installer.init(self.organizer))

    def tearDown(self):
        if self._appdata is None:
            os.environ.pop("APPDATA", None)
        else:
            os.environ["APPDATA"] = self._appdata


class Identity(InstallerCase):
    def test_plugin_answers(self):
        self.assertEqual(self.installer.name(), "Elden Ring Archive Layout Installer")
        self.assertTrue(self.installer.localizedName())
        self.assertTrue(self.installer.description())
        self.assertEqual(self.installer.author(), "MO2 Revamped")
        self.assertEqual(self.installer.settings(), [])
        self.assertEqual(self.installer.priority(), 200)
        self.assertFalse(self.installer.isManualInstaller())
        self.assertTrue(self.installer.requirements())
        self.assertTrue(self.installer.version())
        self.assertTrue(self.installer.isActive())
        self.assertEqual(self.installer._game_plugin_name, "Elden Ring MO2 Support")
        # the ready message is said in its own games only
        self.installer._report_ready(None)

    def test_inactive_elsewhere(self):
        self.assertFalse(Installer().isActive())
        self.organizer.managed = None
        self.assertFalse(self.installer.isActive())
        self.organizer.managed = FakeOtherGame()
        self.assertFalse(self.installer.isActive())
        self.assertFalse(self.installer.isArchiveSupported(make_tree(["a.dll"])))
        self.installer._report_ready(None)

    def test_nightreign_texts(self):
        game_for(self.organizer, nightreign=True)
        self.assertTrue(self.installer._nightreign_active())
        self.assertEqual(self.installer.tr("d'Elden Ring and Elden-Ring"), "de Nightreign and Nightreign")
        self.organizer.managed = None
        self.assertFalse(self.installer._nightreign_active())
        self.assertEqual(self.installer.tr("Elden Ring"), "Elden Ring")

    def test_flags_read_back_from_meta_ini(self):
        for value, expected in (("false", False), (" 0 ", False), ("", False), ("true", True),
                                ("1", True), (True, True), (0, False), (None, False)):
            self.assertEqual(installer_module._flag(value), expected, repr(value))

    def test_entry_names(self):
        for name in ("mod", "DLLs", "a.b", "Ünïcode"):
            self.assertTrue(Installer._valid_entry_name(name), name)
        for name in ("", ".", "..", "x.", "x ", "a/b", "a\\b", "a:b", "a*b", "tab\t", "CON", "com1.txt",
                     "Lpt9 .dll", "nul"):
            self.assertFalse(Installer._valid_entry_name(name), name)


class ArchiveChecks(InstallerCase):
    def test_supported_archives(self):
        supported = self.installer.isArchiveSupported
        self.assertTrue(supported(make_tree(["MyMod/MyMod.dll"])))
        self.assertTrue(supported(make_tree(["parts/a.PARTSBND.DCX"])))
        self.assertFalse(supported(make_tree(["readme.txt", "noextension"])))
        # a scripted installer is left to MO2's FOMOD installer
        self.assertFalse(supported(make_tree(["fomod/ModuleConfig.xml", "a.dll"])))
        self.assertFalse(supported(make_tree(["Top/fomod/ModuleConfig.xml", "Top/a.dll"])))
        # not when the folder with it has company
        self.assertTrue(supported(make_tree(["Top/fomod/ModuleConfig.xml", "Top/a.dll", "b.dll"])))

    def test_deep_wrappers_stop_at_the_limit(self):
        deep = "/".join(f"w{i}" for i in range(20)) + "/a.dll"
        self.assertFalse(self.installer._contains_supported_payload(make_tree([deep])))
        self.assertEqual(self.installer._game_files_in(make_tree([deep.replace("a.dll", "regulation.bin")])), set())
        self.assertEqual(self.installer._seamless_dlls_in(make_tree([deep])), set())

    def test_seamless_and_game_files(self):
        tree = make_tree(["Wrap/SeamlessCoop/ERSC.dll", "Wrap/SeamlessCoop/nrsc.dll",
                          "Wrap/SeamlessCoop/sub/", "Game/eldenring.exe", "regulation.bin"])
        self.assertEqual(self.installer._seamless_dlls_in(tree), {"ersc.dll", "nrsc.dll"})
        self.assertEqual(self.installer._game_files_in(tree), {"eldenring.exe", "regulation.bin"})

    def test_duplicate_dll_names(self):
        tree = make_tree(["A/x.dll", "B/X.DLL", "B/y.dll", "C/y.dll", "z.dll"])
        self.assertEqual(self.installer._duplicate_dll_names(tree), ["x.dll", "y.dll"])
        self.assertEqual(self.installer._duplicate_dll_names(make_tree(["a/x.dll"])), [])

    def test_option_folders(self):
        options = self.installer._option_folders
        self.assertEqual(options(make_tree(["Option A/parts/a.dcx", "Option B/parts/b.dcx"])),
                         ["Option A", "Option B"])
        # inside the archive's own top folder
        self.assertEqual(options(make_tree(["Wrap/Option A/a.dll", "Wrap/Option B/b.dll", "Wrap/docs/"])),
                         ["Option A", "Option B"])
        # game folders are a layout, not options
        self.assertEqual(options(make_tree(["parts/a.dcx", "chr/b.dcx"])), [])
        self.assertEqual(options(make_tree(["Wrap/parts/a.dcx"])), [])
        self.assertEqual(options(make_tree(["Only/a.dll", "readme.txt"])), [])
        deep = "/".join(f"w{i}" for i in range(20)) + "/a.dll"
        self.assertEqual(options(make_tree([deep])), [])


class DllRoutes(InstallerCase):
    def test_tree_routes(self):
        tree = make_tree(["a.dll", "DLLs/sub/B.DLL", "DLLs/b.ini"])
        self.assertEqual(sorted(Installer._collect_tree_dll_routes(tree)), ["DLLs/sub/B.DLL", "a.dll"])

    def test_installed_routes(self):
        root = mod_folder({"DLLs/a.dll": b"MZ", "DLLs/sub/B.DLL": b"MZ", "_ReforgedOnline/x.dll": b"MZ",
                           "parts/_ReforgedOnline/y.dll": b"MZ", "readme.txt": b""})
        self.assertEqual(Installer._collect_installed_dll_routes(root),
                         ["DLLs/a.dll", "DLLs/sub/B.DLL", "parts/_ReforgedOnline/y.dll"])
        self.assertEqual(Installer._collect_installed_dll_routes(root / "missing"), [])
        self.assertEqual(Installer._collect_installed_dll_routes(root / "readme.txt"), [])

    def test_installed_routes_skip_links(self):
        root = mod_folder({"DLLs/a.dll": b"MZ"})
        outside = mod_folder({"evil.dll": b"MZ"})
        try:
            os.symlink(outside / "evil.dll", root / "DLLs" / "link.dll")
            os.symlink(outside, root / "linked", target_is_directory=True)
        except OSError:
            self.skipTest("symbolic links need developer mode here")
        self.assertEqual(Installer._collect_installed_dll_routes(root), ["DLLs/a.dll"])

    def test_source_to_installed_mapping(self):
        installed = ["DLLs/a.dll", "DLLs/natives/b.dll", "DLLs/c/c.dll", "DLLs/d1/d.dll", "DLLs/d2/d.dll",
                     "DLLs/e.dll"]
        mapping = Installer._map_source_dll_routes(
            ["a.dll", "\\mod\\native\\b.dll", "Wrap/c/c.dll", "x/d.dll", "natives/e.dll", "gone.dll"],
            installed,
        )
        self.assertEqual(mapping, {
            "a.dll": "DLLs/a.dll",
            "mod/native/b.dll": "DLLs/natives/b.dll",
            "Wrap/c/c.dll": "DLLs/c/c.dll",
            "natives/e.dll": "DLLs/e.dll",
        })
        # one name twice in the archive is never guessed
        self.assertEqual(Installer._map_source_dll_routes(["A/f.dll", "B/f.dll"], ["DLLs/f.dll"]), {})
        self.assertEqual(Installer._map_source_dll_routes(["A/f.dll"], ["DLLs/x/f.dll"]),
                         {"A/f.dll": "DLLs/x/f.dll"})


class InstallationEvents(InstallerCase):
    def test_start_reads_the_mod_choices(self):
        mod = FakeInstalledMod(mod_folder({}), {
            "preserve_archive_layout": "true", "omit_txt_files": "false", "omit_md_files": True,
            "recognize_mod_engine_paths": "0", "reforged_online": "1",
        })
        self.organizer.settings["_archive_layout_override"] = True
        self.installer.onInstallationStart("a.7z", True, mod)
        self.assertTrue(self.installer._default_preserve)
        self.assertFalse(self.installer._default_omit_txt)
        self.assertTrue(self.installer._default_omit_md)
        self.assertFalse(self.installer._default_recognize_mod_engine_paths)
        self.assertTrue(self.installer._default_reforged_online)
        self.assertEqual(self.installer._default_mod_name, "My Mod")
        self.assertFalse(self.organizer.settings["_archive_layout_override"])
        self.installer.onInstallationStart("b.7z", False, None)
        self.assertFalse(self.installer._default_preserve)
        self.assertIsNone(self.installer._default_mod_name)

    def test_end_stores_the_choice_and_the_dll_index(self):
        root = mod_folder({"DLLs/a.dll": b"MZ", "DLLs/sub/b.dll": b"MZ"})
        mod = FakeInstalledMod(root)
        self.installer._pending_choice = (False, True, False, True)
        self.installer._pending_source_dll_routes = ["a.dll", "Wrap/sub/b.dll"]
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertEqual(mod.stored["preserve_archive_layout"], False)
        self.assertEqual(mod.stored["omit_txt_files"], True)
        index = json.loads(mod.stored["installed_native_route_index"])
        self.assertEqual(index["layout"], "mo2-standard")
        self.assertEqual(index["dlls"], ["DLLs/a.dll", "DLLs/sub/b.dll"])
        self.assertEqual(index["source_to_installed"], {"a.dll": "DLLs/a.dll", "Wrap/sub/b.dll": "DLLs/sub/b.dll"})
        self.assertTrue(self.organizer.settings["_archive_layout_cache_epoch"])
        self.assertIsNone(self.installer._pending_choice)

    def test_end_when_the_mod_refuses_the_settings(self):
        mod = FakeInstalledMod(mod_folder({}), store=False)
        self.installer._pending_choice = (True, False, False, True)
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertIn("installed_native_route_index", mod.stored)
        self.assertEqual(json.loads(mod.stored["installed_native_route_index"])["layout"], "keep-original")

    def test_end_of_an_install_made_another_way(self):
        mod = FakeInstalledMod(mod_folder({}))
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertTrue(mod.cleared)
        other = FakeInstalledMod(mod_folder({}))
        self.installer.onInstallationEnd(mobase.InstallResult.CANCELED, other)
        self.assertFalse(other.cleared)

    def test_an_error_after_the_install_never_reaches_mo2(self):
        mod = FakeInstalledMod(mod_folder({}))
        mod.setPluginSetting = mock.Mock(side_effect=RuntimeError("mod removed"))
        self.installer._pending_choice = (True, False, False, True)
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertIsNone(self.installer._pending_choice)

    def test_reforged_online_release_is_linked(self):
        root = mod_folder({"regulation.bin": b"reg", "parts/a.dcx": b"a", "DLLs/reforged.dll": b"MZ"})
        layout = {"online": "_ReforgedOnline", "mod_entries": ["regulation.bin", "parts", "gone"],
                  "natives": ["reforged.dll"]}
        mod = FakeInstalledMod(root)
        self.installer._pending_choice = (False, False, False, True)
        self.installer._pending_reforged_layout = {"keep": True, "layout": layout}
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        online = root / "_ReforgedOnline"
        self.assertEqual((online / "mod" / "parts" / "a.dcx").read_bytes(), b"a")
        self.assertTrue(os.path.samefile(online / "internals/modengine/dll/reforged.dll", root / "DLLs/reforged.dll"))
        self.assertTrue(mod.stored["reforged_online"])
        # the release is not indexed as the mod's own DLLs
        self.assertEqual(json.loads(mod.stored["installed_native_route_index"])["dlls"], ["DLLs/reforged.dll"])
        # again over an update: a file that changed is linked anew, others stay
        (root / "regulation.bin").unlink()
        (root / "regulation.bin").write_bytes(b"new")
        self.assertEqual(Installer._link_reforged_release(root, layout), 0)
        self.assertEqual((online / "mod" / "regulation.bin").read_bytes(), b"new")

    def test_reforged_files_that_cannot_be_placed_are_counted(self):
        root = mod_folder({"DLLs/reforged.dll": b"MZ"})
        layout = {"online": "_ReforgedOnline", "mod_entries": [], "natives": ["reforged.dll", "missing.dll"]}
        self.assertEqual(Installer._link_reforged_release(root, layout), 1)
        # the window that says so is a message box: recorded here
        mod = FakeInstalledMod(root)
        self.installer._pending_choice = (False, False, False, True)
        self.installer._pending_reforged_layout = {"keep": True, "layout": layout}
        with mock.patch.object(installer_module.QtWidgets.QMessageBox, "warning") as warning:
            self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertEqual(warning.call_count, 1)

    def test_offline_reforged_over_an_online_install_drops_the_release(self):
        root = mod_folder({"_ReforgedOnline/mod/regulation.bin": b"x", "regulation.bin": b"x"})
        mod = FakeInstalledMod(root)
        self.installer._pending_choice = (False, False, False, True)
        self.installer._pending_reforged_layout = {"keep": False, "layout": None}
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, mod)
        self.assertFalse((root / "_ReforgedOnline").exists())
        # one arranged by hand with a folder of that name keeps it
        root = mod_folder({"_ReforgedOnline/mine.txt": b"x"})
        self.installer._pending_choice = (True, False, False, True)
        self.installer._pending_reforged_layout = {"keep": False, "layout": None, "arranged_online": True}
        self.installer.onInstallationEnd(mobase.InstallResult.SUCCESS, FakeInstalledMod(root))
        self.assertTrue((root / "_ReforgedOnline").exists())

    def test_cache_epoch_without_organizer_or_failing(self):
        Installer()._bump_archive_layout_cache_epoch()
        with mock.patch.object(self.organizer, "setPluginSetting", side_effect=RuntimeError("gone")):
            self.installer._bump_archive_layout_cache_epoch()


class Layouts(InstallerCase):
    def test_text_files_are_removed_outside_game_folders(self):
        tree = make_tree(["readme.txt", "notes/info.TXT", "action/script.txt", "load.txt", "a.dll", "docs/x.md"])
        removed = Installer._remove_files_with_extension(tree, ".txt")
        self.assertEqual(removed, 2)
        self.assertEqual(tree_files(tree), ["a.dll", "action/script.txt", "docs/x.md", "load.txt"])

    def test_apply_both_layouts(self):
        tree, checker = self.installer._apply_layout(
            make_tree(["Wrap/MyMod.dll", "Wrap/readme.txt", "Wrap/notes.md"]), False, True, True, False
        )
        self.assertEqual(tree_files(tree), ["DLLs/MyMod.dll"])
        tree, _checker = self.installer._apply_layout(
            make_tree(["Wrap/MyMod.dll", "Wrap/readme.txt"]), True, False, False, False
        )
        self.assertIn("MyMod.dll", tree_files(tree))

    def test_summary_of_a_copy(self):
        source = make_tree(["MyMod/MyMod.dll", "MyMod/MyMod.ini", "MyMod/regulation.bin",
                            "MyMod/parts/a.dcx", "MyMod/readme.txt"])
        summary = self.installer._layout_summary(source, False, False, False, False)
        self.assertEqual(summary["dlls"], 1)
        self.assertEqual(summary["dlls_routed"], 1)
        self.assertEqual(summary["dll_settings"], 1)
        self.assertTrue(summary["regulation"])
        self.assertEqual(summary["game"], 1)
        # the archive itself is untouched
        self.assertEqual(len(tree_files(source)), 5)
        with mock.patch.object(Installer, "_apply_layout", side_effect=RuntimeError("odd archive")):
            self.assertIsNone(self.installer._layout_summary(source, False, False, False, False))

    def test_classify_misplaced_game_folders(self):
        summary = Installer._classify_layout_paths(
            ["Wrap/parts/a.dcx", "_ReforgedOnline/x", "notes.txt", "DLLs/a.dll", "Other/b.dll"], True
        )
        self.assertEqual(summary["misplaced"], 1)
        self.assertEqual(summary["misplaced_in"], "Wrap")
        self.assertEqual(summary["online"], 1)
        self.assertEqual(summary["dlls"], 2)
        self.assertEqual(summary["dlls_routed"], 1)
        self.assertEqual(summary["other"], 2)

    def test_left_out_and_arranged(self):
        whole = ["A/x.dll", "B/x.dll", "readme.txt", "parts/a.dcx"]
        self.assertEqual(Installer._left_out_paths(whole, ["DLLs/A/x.dll", "parts/a.dcx"]),
                         ["B/x.dll", "readme.txt"])
        archive = make_tree(whole)
        base = make_tree(["DLLs/a.dll"])
        arranged = Installer._arranged_tree(
            archive, base, archive, [("DLLs/mine.dll", "DLLs/a.dll"), ("Docs/readme.txt", "whole:readme.txt")]
        )
        self.assertEqual(tree_files(arranged), ["DLLs/mine.dll", "Docs/readme.txt"])
        with self.assertRaises(RuntimeError):
            Installer._arranged_tree(archive, base, archive, [("x", "missing")])

    def test_glyphs(self):
        glyph = Installer._file_glyph
        self.assertEqual(glyph("ER0000.sl2"), glyph("ER0000.co2.bak"))
        self.assertNotEqual(glyph("x.bak"), glyph("ER0000.sl2"))
        self.assertEqual(glyph("a.tpfbdt"), glyph("regulation.bin"))
        self.assertEqual(glyph("a.partsbnd"), glyph("a.zip"))
        self.assertEqual(glyph("x.DLL"), glyph("y.dll"))
        self.assertNotEqual(glyph("noextension"), glyph("x.dll"))
        for name in ("a.exe", "a.ini", "a.lua", "a.png", "a.wem", "a.mp4", "a.gfx", "a.lnk", "a.md", "a.prev"):
            self.assertIsInstance(glyph(name), int)
        self.assertFalse(Installer._glyph_icon(0xE8B7).isNull())

    def test_other_mods_and_names(self):
        self.organizer.add_mod("Reg A", ["regulation.bin"])
        self.organizer.add_mod("Reg off", ["regulation.bin"], active=False)
        self.organizer.add_mod("My Mod", ["regulation.bin"])
        self.installer._default_mod_name = "My Mod"
        self.assertEqual(self.installer._other_mods_with_regulation(), ["Reg A"])
        self.assertTrue(self.installer._mod_name_taken("  reg a "))
        self.assertFalse(self.installer._mod_name_taken("new"))
        broken = Installer()
        self.assertEqual(broken._other_mods_with_regulation(), [])
        self.assertFalse(broken._mod_name_taken("x"))


class Install(InstallerCase):
    def choose(self, *choices, online: bool = False):
        answers = list(choices)

        def ask(_name, _tree, _package=None):
            self.installer._chosen_reforged_online = online
            answer = answers.pop(0)
            if isinstance(answer, Exception):
                raise answer
            return answer

        return mock.patch.object(self.installer, "_ask_archive_layout", side_effect=ask)

    def test_without_organizer(self):
        self.assertEqual(Installer().install(FakeName("x"), make_tree(["a.dll"]), "", 0),
                         mobase.InstallResult.NOT_ATTEMPTED)

    def test_cancel_and_window_errors(self):
        with self.choose(None):
            self.assertEqual(self.installer.install(FakeName("x"), make_tree(["a.dll"]), "", 0),
                             mobase.InstallResult.CANCELED)
        with self.choose(RuntimeError("odd")):
            self.assertEqual(self.installer.install(FakeName("x"), make_tree(["a.dll"]), "", 0),
                             mobase.InstallResult.FAILED)

    def test_standard_layout_with_a_new_name(self):
        name = FakeName("Archive Name")
        with self.choose((False, True, False, True, "  My Mod  ")):
            tree = self.installer.install(name, make_tree(["Wrap/MyMod.dll", "Wrap/readme.txt"]), "1.0", 1)
        self.assertEqual(tree_files(tree), ["DLLs/MyMod.dll"])
        self.assertEqual(str(name), "My Mod")
        self.assertEqual(self.installer._pending_choice, (False, True, False, True))
        self.assertEqual(self.installer._pending_source_dll_routes, ["Wrap/MyMod.dll"])
        self.assertFalse(self.organizer.settings["_archive_layout_override"])

    def test_empty_name_keeps_the_archive_name(self):
        name = FakeName("Archive Name")
        with self.choose((True, False, False, True, "   ")):
            self.installer.install(name, make_tree(["MyMod.dll"]), "", 0)
        self.assertEqual(name.updates, [])
        self.assertTrue(self.organizer.settings["_archive_layout_override"])

    def test_layout_error_fails_the_install(self):
        with self.choose((False, False, False, True, "x")), \
                mock.patch.object(Installer, "_apply_layout", side_effect=RuntimeError("bad tree")):
            self.assertEqual(self.installer.install(FakeName("x"), make_tree(["a.dll"]), "", 0),
                             mobase.InstallResult.FAILED)

    def test_manual_back_then_install(self):
        arranged = make_tree(["DLLs/a.dll"])
        with self.choose(("manual", True, True, False, "Hand"), (False, False, False, True, "Hand")), \
                mock.patch.object(self.installer, "_ask_manual_layout", return_value=None) as manual:
            self.installer.install(FakeName("x"), make_tree(["a.dll"]), "", 0)
        self.assertEqual(manual.call_count, 1)
        # back in the window, as it was left
        self.assertTrue(self.installer._default_preserve)
        name = FakeName("x")
        with self.choose(("manual", False, False, False, "Hand")), \
                mock.patch.object(self.installer, "_ask_manual_layout", return_value=arranged):
            result = self.installer.install(name, make_tree(["a.dll"]), "", 0)
        self.assertIs(result, arranged)
        self.assertEqual(str(name), "Hand")
        self.assertEqual(self.installer._pending_choice, (True, False, False, True))
        with self.choose(("manual", False, False, False, "Hand")), \
                mock.patch.object(self.installer, "_ask_manual_layout", side_effect=RuntimeError("x")):
            self.assertEqual(self.installer.install(FakeName("x"), make_tree(["a.dll"]), "", 0),
                             mobase.InstallResult.FAILED)

    def test_reforged_online_and_by_hand(self):
        paths = ["ERR/mod/regulation.bin", "ERR/mod/parts/a.dcx", "ERR/internals/modengine/dll/reforged.dll",
                 "ERR/internals/launcher/ReforgedLauncher.exe"]
        with self.choose((False, False, False, True, "ERR"), online=True):
            tree = self.installer.install(FakeName("ERR"), make_tree(paths), "", 0)
        self.assertIn("DLLs/reforged.dll", tree_files(tree))
        layout = self.installer._pending_reforged_layout
        self.assertTrue(layout["keep"])
        self.assertEqual(layout["layout"]["online"], "_ReforgedOnline")
        arranged = make_tree(["_ReforgedOnline/x.txt"])
        with self.choose(("manual", False, False, False, "ERR")), \
                mock.patch.object(self.installer, "_ask_manual_layout", return_value=arranged):
            self.installer.install(FakeName("ERR"), make_tree(paths), "", 0)
        self.assertTrue(self.installer._pending_reforged_layout["arranged_online"])


class DrivenDialog(QtWidgets.QDialog):
    """A QDialog whose exec() runs the test's hook instead of an event loop."""

    hooks: list = []

    def exec(self) -> int:
        # no event loop at all: timers other tests queued (the game plugin's
        # message boxes) must not run here
        if DrivenDialog.hooks:
            DrivenDialog.hooks.pop(0)(self)
        return self.result()


def click(dialog, object_name: str) -> None:
    button = dialog.findChild(QtWidgets.QAbstractButton, object_name)
    assert button is not None, object_name
    button.click()


def click_text(dialog, text: str) -> None:
    for button in dialog.findChildren(QtWidgets.QPushButton):
        if button.text() == text:
            button.click()
            return
    raise AssertionError(text)


class Windows(InstallerCase):
    def setUp(self):
        super().setUp()
        patcher = mock.patch.object(installer_module.QtWidgets, "QDialog", DrivenDialog)
        patcher.start()
        self.addCleanup(patcher.stop)
        DrivenDialog.hooks = []
        self.addCleanup(lambda: setattr(DrivenDialog, "hooks", []))

    def test_install_with_the_standard_layout(self):
        def hook(dialog):
            click(dialog, "archivePreviewButton")
            click(dialog, "archiveAdvancedButton")
            click(dialog, "archiveSkipTxtCheck")
            click(dialog, "archivePreserveLayoutRadio")
            click(dialog, "archiveStandardLayoutRadio")
            dialog.findChild(QtWidgets.QLineEdit, "archiveModNameEdit").setText("Chosen")
            click(dialog, "archiveLayoutChoiceButton")

        DrivenDialog.hooks = [hook]
        choice = self.installer._ask_archive_layout(
            "My Mod", make_tree(["Wrap/MyMod.dll", "Wrap/readme.txt", "Wrap/parts/a.dcx"]))
        self.assertEqual(choice, (False, True, False, True, "Chosen"))

    def test_an_empty_name_is_refilled_then_cancel(self):
        def hook(dialog):
            edit = dialog.findChild(QtWidgets.QLineEdit, "archiveModNameEdit")
            edit.setText("   ")
            click(dialog, "archiveLayoutChoiceButton")
            assert edit.text() == "My Mod"
            dialog.reject()

        DrivenDialog.hooks = [hook]
        self.assertIsNone(self.installer._ask_archive_layout("My Mod", make_tree(["a.dll"])))

    def test_known_packages_and_odd_archives(self):
        self.organizer.add_mod("Other Regulation", ["regulation.bin"])
        self.organizer.add_mod("Taken", ["x.dll"])
        cases = [
            (["SeamlessCoop/ersc.dll", "SeamlessCoop/ersc_settings.ini", "ersc_launcher.exe"], "seamless"),
            (["me3/convergence.me3", "mod/regulation.bin"], "convergence"),
            (["Drag files into here/parts/a.dcx"], "frzn"),
            (["A/x.dll", "B/x.dll", "Option 1/parts/a.dcx", "Option 2/parts/b.dcx", "regulation.bin",
              "Game/eldenring.exe"], None),
            ([f"f{i}.dll" for i in range(300)], None),
        ]
        for paths, package in cases:
            with self.subTest(package=package, count=len(paths)):
                DrivenDialog.hooks = [lambda dialog: (click(dialog, "archivePreviewButton"),
                                                      click(dialog, "archiveLayoutChoiceButton"))]
                choice = self.installer._ask_archive_layout("Taken", make_tree(paths), package)
                self.assertIsNotNone(choice)

    def test_reforged_window_online_switch(self):
        paths = ["ERR/mod/regulation.bin", "ERR/internals/modengine/dll/reforged.dll",
                 "ERR/internals/launcher/ReforgedLauncher.exe"]
        self.installer._default_reforged_online = True

        def hook(dialog):
            box = dialog.findChild(QtWidgets.QCheckBox, "archiveReforgedOnlineCheck")
            assert box is not None and box.isChecked()
            assert not dialog.findChild(QtWidgets.QAbstractButton, "archiveManualButton").isEnabled()
            box.click()
            click(dialog, "archiveManualButton")

        DrivenDialog.hooks = [hook]
        choice = self.installer._ask_archive_layout("ERR", make_tree(paths), "reforged")
        self.assertEqual(choice[0], "manual")
        self.assertFalse(self.installer._chosen_reforged_online)

    def test_reinstall_shows_the_mod_choices(self):
        self.installer._default_preserve = True
        self.installer._default_omit_md = True
        DrivenDialog.hooks = [lambda dialog: click(dialog, "archiveLayoutChoiceButton")]
        choice = self.installer._ask_archive_layout("My Mod", make_tree(["Wrap/a.dll", "Wrap/b.md"]))
        self.assertEqual(choice[:3], (True, False, True))

    def test_arrange_by_hand(self):
        archive = make_tree(["Wrap/MyMod.dll", "Wrap/MyMod.ini", "Wrap/readme.txt", "Wrap/Optional/parts/a.dcx"])

        def hook(dialog):
            tree = dialog.findChild(QtWidgets.QTreeWidget, "archivePreviewTree")
            first = tree.topLevelItem(0)
            tree.setCurrentItem(first)
            first.setSelected(True)
            for text in ("Move to DLLs", "Move to the top", "New folder", "Rename"):
                click_text(dialog, text)
            click_text(dialog, "Reset")
            click(dialog, "archiveLayoutChoiceButton")

        state: dict = {}
        # back: the window keeps the arrangement for when it opens again (the
        # install window comes back between the two)
        DrivenDialog.hooks = [lambda dialog: dialog.reject()]
        self.assertIsNone(self.installer._ask_manual_layout(archive, "My Mod", False, False, False, state))
        self.assertIsNotNone(state["snapshot"])
        DrivenDialog.hooks = [hook]
        arranged = self.installer._ask_manual_layout(archive, "My Mod", False, False, False, state)
        self.assertIn("DLLs/MyMod.dll", tree_files(arranged))

    def test_use_a_folder_as_the_top(self):
        archive = make_tree(["Main/parts/a.dcx", "Main/chr/b.dcx", "Extra/c.dll"])

        def hook(dialog):
            tree = dialog.findChild(QtWidgets.QTreeWidget, "archivePreviewTree")
            for index in range(tree.topLevelItemCount()):
                item = tree.topLevelItem(index)
                if item.childCount():
                    tree.clearSelection()
                    item.setSelected(True)
                    click_text(dialog, "Use as the top")
                    break
            click(dialog, "archiveLayoutChoiceButton")

        DrivenDialog.hooks = [hook]
        arranged = self.installer._ask_manual_layout(archive, "My Mod", True, False, False, {})
        self.assertTrue(tree_files(arranged))


if __name__ == "__main__":
    unittest.main()
