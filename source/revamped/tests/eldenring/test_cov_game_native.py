"""The Native DLL profile and the forced loads (native_profile.py,
forced_loads.py): hand-edited profiles that are refused or partly skipped,
the installer's route index, DLLs found in odd mod layouts, and what MO2 is
asked to load."""

from __future__ import annotations

import json
import os
import unittest
from pathlib import Path
from unittest import mock

from basic_games.games.eldenring import forced_loads

from fake_organizer import FakeOrganizer, appdata_folder, game_for
from test_cov_game_fakes import use_mod_settings

HEADER = "[RevampedNativeProfile]\nversion = 2\n"


def names(loads) -> list[str]:
    return [Path(load.library()).name for load in loads
            if Path(load.library()).name not in ("eldenring_mo2_bridge.dll", "eldenring_mo2_me3_compat.dll")]


class ProfileBase(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()
        self.organizer.add_mod("A", ["DLLs/a.dll", "DLLs/sub/deep.dll", "parts/x.dcx"])
        self.game = game_for(self.organizer)

    def resolve(self, body: str, encoding: str = "utf-8"):
        self.organizer.write_profile_ini("")
        (self.organizer.profile_dir / "eldenring_mo2_startup.ini").write_bytes((HEADER + body).encode(encoding))
        return self.game._resolve_revamped_native_profile(self.organizer.profile())


class RefusedProfiles(ProfileBase):
    def test_errors(self):
        cases = {
            "unknown key": "bogus_1 = x\n",
            "no index": "native = DLLs/a.dll\n",
            "index zero": "native_0 = DLLs/a.dll\nmod_0 = A\n",
            "same entry twice": "native_1 = DLLs/a.dll\nnative_01 = DLLs/a.dll\nmod_1 = A\n",
            "not a boolean": "native_1 = DLLs/a.dll\nmod_1 = A\nenabled_1 = yes\n",
            "optional twice": "native_1 = DLLs/a.dll\nmod_1 = A\noptional_1 = true\noptional_01 = false\n",
            "two mods": "native_1 = DLLs/a.dll\nmod_1 = A\nmod_01 = B\n",
            "two initializers": "native_1 = DLLs/a.dll\nmod_1 = A\ninitializer_1 = X\ninitializer_01 = Y\n",
            "no natives": "mod = A\n",
            "legacy without a mod": "native_1 = DLLs/a.dll\n",
            "legacy mod off": "mod = Missing\nnative_1 = DLLs/a.dll\n",
            "absolute path": "native_1 = C:/x/a.dll\nmod_1 = A\n",
            "parent folder": "native_1 = ../a.dll\nmod_1 = A\n",
            "not a DLL": "native_1 = DLLs/a.txt\nmod_1 = A\n",
            "listed twice": "native_1 = DLLs/a.dll\nmod_1 = A\nnative_2 = dlls\\A.DLL\nmod_2 = a\n",
            "no path": "mod_1 = A\n",
            "bad initializer": "native_1 = DLLs/a.dll\nmod_1 = A\ninitializer_1 = 1bad\n",
            "non-ASCII initializer": "native_1 = DLLs/a.dll\nmod_1 = A\ninitializer_1 = Iniciaçión\n",
        }
        for label, body in cases.items():
            with self.subTest(label):
                data = self.resolve(body)
                self.assertTrue(data and data["error"], data)
                self.assertEqual(data["entries"], [])

    def test_rooted_path_stays_in_its_mod(self):
        # "/home/user/a.dll" (a path from Linux) has no drive, so it isn't
        # absolute on Windows: only a DLL of that name inside the mod's DLLs loads
        data = self.resolve("native_1 = /home/user/a.dll\nmod_1 = A\n")
        self.assertIsNone(data["error"])
        self.assertEqual([e["virtual_path"] for e in data["entries"]], ["DLLs/a.dll"])
        self.assertEqual(self.resolve("native_1 = /home/user/none.dll\nmod_1 = A\n")["entries"], [])

    def test_not_utf8_is_an_error_not_no_profile(self):
        data = self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n; ñ\n", "cp1252")
        self.assertTrue(data["error"])

    def test_bom_is_read(self):
        data = self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n", "utf-8-sig")
        self.assertIsNone(data["error"])
        self.assertEqual([e["route"] for e in data["entries"]], ["DLLs/a.dll"])

    def test_no_section_or_file(self):
        self.assertIsNone(self.game._resolve_revamped_native_profile(self.organizer.profile()))
        self.organizer.write_profile_ini("[Startup]\nboot_boost = 1\n")
        self.assertIsNone(self.game._resolve_revamped_native_profile(self.organizer.profile()))


class ResolvedProfiles(ProfileBase):
    def test_backslashes_and_case(self):
        data = self.resolve("native_1 = dlls\\A.DLL\nmod_1 = a\ninitializer_1 = Start\n")
        self.assertIsNone(data["error"])
        self.assertEqual(data["entries"][0]["initializer"], "Start")
        self.assertEqual(data["entries"][0]["mod_name"], "A")

    def test_legacy_profile(self):
        data = self.resolve("mod = A\nnative_1 = DLLs/a.dll\n")
        self.assertIsNone(data["error"])
        self.assertEqual(data["scope_mod"], "A")
        self.assertEqual(data["scope_priority"], 0)

    def test_skipped_entries(self):
        self.organizer.add_mod("B", ["DLLs/steam_api64.dll", "DLLs/nrsc.dll", "DLLs/dinput8.dll"])
        data = self.resolve(
            "native_1 = DLLs/steam_api64.dll\nmod_1 = B\n"
            "native_2 = DLLs/nrsc.dll\nmod_2 = B\n"
            "native_3 = DLLs/dinput8.dll\nmod_3 = B\n"
            "native_4 = DLLs/gone.dll\nmod_4 = B\noptional_4 = true\n"
            "native_5 = DLLs/gone2.dll\nmod_5 = B\n"
            "native_6 = DLLs/a.dll\nmod_6 = Off\n"
            "native_7 = DLLs/a.dll\nmod_7 = A\nenabled_7 = false\n"
            "native_8 = DLLs/sub/deep.dll\nmod_8 = A\ninitializer_8 =\n")
        self.assertIsNone(data["error"])
        self.assertEqual([e["route"] for e in data["entries"]], ["DLLs/sub/deep.dll"])
        self.assertIsNone(data["entries"][0]["initializer"])
        self.assertIn(("a", "a.dll"), data["disabled_libraries"])

    def test_found_by_name_under_dlls(self):
        self.organizer.add_mod("C", ["DLLs/Moved/tool.dll"])
        data = self.resolve("native_1 = natives/tool.dll\nmod_1 = C\n")
        self.assertEqual(data["entries"][0]["virtual_path"], "DLLs/Moved/tool.dll")

    def test_two_dlls_of_the_same_name(self):
        self.organizer.add_mod("C", ["DLLs/one/tool.dll", "DLLs/two/tool.dll"])
        data = self.resolve("native_1 = tool/tool.dll\nmod_1 = C\n")
        self.assertIn("multiple", data["error"])

    def test_native_folder_maps_to_dlls(self):
        self.organizer.add_mod("C", ["DLLs/tool/x.dll"])
        data = self.resolve("native_1 = native/tool/x.dll\nmod_1 = C\n")
        self.assertEqual(data["entries"][0]["virtual_path"], "DLLs/tool/x.dll")

    def test_nightreign_movement_needs_its_assets(self):
        self.organizer.add_mod("NRM", ["DLLs/NightreignMovement.dll"])
        body = "native_1 = DLLs/NightreignMovement.dll\nmod_1 = NRM\ninitializer_1 = NrmInitialize\n"
        self.assertIn("assets", self.resolve(body)["error"])
        (self.organizer.mods / "NRM" / "DLLs" / "Assets").mkdir()
        self.assertIsNone(self.resolve(body)["error"])

    def test_winner_and_overridden_copies(self):
        # B wins DLLs/a.dll: one load, from B, at A's entry
        self.organizer.add_mod("B", ["DLLs/a.dll"])
        data = self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n"
                            "native_2 = DLLs/a.dll\nmod_2 = B\ninitializer_2 = FromB\n")
        self.assertEqual(len(data["entries"]), 1)
        self.assertEqual(data["entries"][0]["mod_name"], "B")
        self.assertEqual(data["entries"][0]["initializer"], "FromB")

    def test_winner_switched_off(self):
        self.organizer.add_mod("B", ["DLLs/a.dll"])
        data = self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n"
                            "native_2 = DLLs/a.dll\nmod_2 = B\nenabled_2 = false\n")
        self.assertEqual(data["entries"], [])

    def test_file_origins_unavailable(self):
        with mock.patch.object(self.organizer, "getFileOrigins", side_effect=RuntimeError("busy")):
            self.assertIn("origin", self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n")["error"])
        with mock.patch.object(self.organizer, "getFileOrigins", return_value=[]):
            self.assertEqual(self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n")["entries"], [])

    def test_route_index(self):
        root = self.organizer.mods / "A"
        (root / "DLLs" / "Renamed").mkdir()
        (root / "DLLs" / "Renamed" / "real.dll").write_bytes(b"MZ")
        (root / "Other" / "z").mkdir(parents=True)
        (root / "Other" / "z" / "solo.dll").write_bytes(b"MZ")
        index = json.dumps({
            "dlls": ["DLLs/Renamed/real.dll", "Other/z/solo.dll", "../escape.dll", "C:/abs.dll", 5, "DLLs/missing.dll", "x.txt"],
            "source_to_installed": {"Archive\\Mod.dll": "DLLs/Renamed/real.dll", "": "x.dll",
                                    "bad": "../x.dll", "n": 3},
        })
        settings = {"A": {self.game.InstalledNativeRouteIndexSetting: index}}
        with use_mod_settings(self.organizer, settings):
            data = self.resolve("native_1 = archive/mod.dll\nmod_1 = A\n")
            self.assertEqual(data["entries"][0]["virtual_path"], "DLLs/Renamed/real.dll")
            # by the end of an installed route
            data = self.resolve("native_1 = Renamed/real.dll\nmod_1 = A\n")
            self.assertEqual(data["entries"][0]["virtual_path"], "DLLs/Renamed/real.dll")
            # an installed route outside DLLs, found by its end
            data = self.resolve("native_1 = z/solo.dll\nmod_1 = A\n")
            self.assertEqual(data["entries"][0]["virtual_path"], "Other/z/solo.dll")

    def test_route_index_broken(self):
        for value in ("{", "[1]", json.dumps({"dlls": "x", "source_to_installed": []})):
            with self.subTest(value=value):
                with use_mod_settings(self.organizer, {"A": {self.game.InstalledNativeRouteIndexSetting: value}}):
                    data = self.resolve("native_1 = DLLs/a.dll\nmod_1 = A\n")
                self.assertIsNone(data["error"])
                self.assertEqual(len(data["entries"]), 1)

    def test_route_index_with_two_matches(self):
        root = self.organizer.mods / "A"
        for folder in ("x", "y"):
            (root / "Other" / folder).mkdir(parents=True)
            (root / "Other" / folder / "same.dll").write_bytes(b"MZ")
        index = json.dumps({"dlls": ["Other/x/same.dll", "Other/y/same.dll"]})
        with use_mod_settings(self.organizer, {"A": {self.game.InstalledNativeRouteIndexSetting: index}}):
            self.assertIn("multiple", self.resolve("native_1 = same.dll\nmod_1 = A\n")["error"])


class LoadOrder(ProfileBase):
    def test_disabled_dll_not_loaded_from_its_mod(self):
        self.organizer.add_mod("B", ["DLLs/b.dll"])
        self.resolve("native_1 = DLLs/b.dll\nmod_1 = B\nenabled_1 = false\n"
                     "native_2 = DLLs/a.dll\nmod_2 = A\n")
        self.assertEqual(names(self.game.executableForcedLoads()), ["deep.dll", "a.dll"])

    def test_legacy_scope_replaces_the_mods_dlls(self):
        self.resolve("mod = A\nnative_1 = DLLs/sub/deep.dll\n")
        self.assertEqual(names(self.game.executableForcedLoads()), ["deep.dll"])


class ForcedLoads(unittest.TestCase):
    def setUp(self):
        appdata_folder()
        self.organizer = FakeOrganizer()

    def test_no_mods_no_bridge_options(self):
        game = game_for(self.organizer)
        self.organizer.write_profile_ini("[Startup]\nskip_intro_logos = false\nboot_boost = false\n")
        self.assertEqual(game.executableForcedLoads(), [])

    def test_no_mods_with_bridge_options(self):
        game = game_for(self.organizer)
        self.organizer.write_profile_ini("[Startup]\nstart_minimized = true\n")
        loads = game.executableForcedLoads()
        self.assertEqual(names(loads), [])

    def test_other_game(self):
        game = game_for(self.organizer)
        self.organizer.managed = None
        self.assertEqual(game.executableForcedLoads(), [])

    def test_native_loading_off(self):
        self.organizer.add_mod("A", ["DLLs/a.dll", "proxy.dll"])
        self.organizer.settings["load_native_dlls"] = False
        self.assertEqual(names(game_for(self.organizer).executableForcedLoads()), [])

    def test_root_dlls_and_legacy_folders(self):
        self.organizer.add_mod("Proxy", ["dinput8.dll", "helper.dll", "steam_api64.dll", "sub/not_root.dll"])
        self.organizer.add_mod("Legacy", ["mods/old.dll", "MO2_DLLs/older.dll", "external_dlls/x.txt"])
        self.organizer.add_mod("Pres", ["preserved.dll"])
        settings = {"Pres": {"preserve_archive_layout": "true"}}
        with use_mod_settings(self.organizer, settings):
            loads = names(game_for(self.organizer).executableForcedLoads())
        # root loaders first, then the other root DLLs, then managed ones
        self.assertEqual(loads[:3], ["dinput8.dll", "helper.dll", "preserved.dll"])
        self.assertEqual(sorted(loads[3:]), ["old.dll", "older.dll"])

    def test_root_dll_won_by_a_higher_mod(self):
        self.organizer.add_mod("Low", ["proxy.dll"])
        high = self.organizer.add_mod("High", ["proxy.dll"])
        loads = [Path(load.library()) for load in game_for(self.organizer).executableForcedLoads()
                 if Path(load.library()).name == "proxy.dll"]
        self.assertEqual([load.parent for load in loads], [high.resolve()])

    def test_mod_that_cannot_be_inspected(self):
        self.organizer.add_mod("Broken", ["proxy.dll"])
        with use_mod_settings(self.organizer, {}, {"Broken": RuntimeError("no tree")}):
            self.assertEqual(names(game_for(self.organizer).executableForcedLoads()), [])

    def test_other_games_seamless_is_never_loaded(self):
        self.organizer.add_mod("Wrong", ["DLLs/nrsc.dll", "nrsc.dll"])
        self.assertEqual(names(game_for(self.organizer).executableForcedLoads()), [])
        night = FakeOrganizer()
        night.add_mod("Wrong", ["DLLs/ersc.dll"])
        self.assertEqual(names(game_for(night, nightreign=True).executableForcedLoads()), [])

    def test_file_list_errors(self):
        self.organizer.add_mod("A", ["DLLs/a.dll", "proxy.dll"])
        game = game_for(self.organizer)
        with mock.patch.object(self.organizer, "findFiles", side_effect=RuntimeError("busy")), \
                mock.patch.object(self.organizer, "getFileOrigins", side_effect=RuntimeError("busy")):
            self.assertEqual(names(game.executableForcedLoads()), [])

    def test_physical_paths_from_mo2(self):
        # MO2 can answer findFiles with the files' real paths
        root = self.organizer.add_mod("A", ["DLLs/a.dll", "mods/b.dll"])
        game = game_for(self.organizer)
        real = [str(root / "DLLs" / "a.dll"), str(root / "mods" / "b.dll"), "Z:/elsewhere/c.dll",
                "DLLs/readme.txt", "tool.dll"]
        with mock.patch.object(self.organizer, "findFiles",
                               side_effect=lambda directory, _pattern: real if directory == "DLLs" else []):
            # a path outside a DLL folder is never taken for one
            self.assertEqual(names(game.executableForcedLoads()), ["a.dll"])

    def test_missing_bridge_and_me3_compat(self):
        self.organizer.add_mod("A", ["DLLs/reforged.dll"])
        game = game_for(self.organizer)
        with mock.patch.object(forced_loads, "GAMES_DIR", Path(self.organizer.base) / "no-games"), \
                mock.patch("basic_games.games.eldenring.environment.GAMES_DIR",
                           Path(self.organizer.base) / "no-games"):
            loads = game.executableForcedLoads()
        self.assertEqual([Path(load.library()).name for load in loads], ["reforged.dll"])

    def test_seamless_switched_off_in_the_profile(self):
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        self.organizer.add_mod("A", ["DLLs/a.dll"])
        self.organizer.write_profile_ini(HEADER + "native_1 = SeamlessCoop/ersc.dll\nmod_1 = Seamless\n"
                                         "enabled_1 = false\nnative_2 = DLLs/a.dll\nmod_2 = A\n")
        game = game_for(self.organizer)
        self.assertEqual(names(game.executableForcedLoads()), ["a.dll"])
        self.assertEqual(game._seamless_initializer(), [])
        self.organizer.settings["load_native_dlls"] = False
        self.assertEqual(game._seamless_initializer(), [])

    def test_convergence_coop_extension(self):
        root = self.organizer.add_mod("Convergence", ["me3/convergence.me3"])
        game = game_for(self.organizer)
        self.assertEqual(game._convergence_coop_extension(), "cnvco2")
        (root / "ersc_settings.ini").write_bytes(b"\xef\xbb\xbf[SAVE]\nsave_file_extension = cnv2\n")
        self.assertEqual(game._convergence_coop_extension(), "cnv2")
        for value in ("co2", "../x", ""):
            (root / "ersc_settings.ini").write_text(f"[SAVE]\nsave_file_extension = {value}\n")
            self.assertEqual(game._convergence_coop_extension(), "cnvco2")
        (root / "ersc_settings.ini").write_bytes("[SAVE]\nsave_file_extension = ñ\n".encode("cp1252"))
        self.assertEqual(game._convergence_coop_extension(), "cnvco2")
        self.organizer.active.discard("Convergence")
        self.assertIsNone(game._convergence_coop_extension())

    def test_convergence_copy_failure_uses_the_mod_dll(self):
        seamless = self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        self.organizer.add_mod("Convergence", ["me3/convergence.me3"])
        game = game_for(self.organizer)
        with mock.patch.object(type(game), "_convergence_seamless_copy", side_effect=PermissionError("denied")):
            self.assertEqual(game._seamless_load_dll(), seamless / "SeamlessCoop" / "ersc.dll")

    def test_convergence_copy_without_settings_and_unchanged(self):
        self.organizer.add_mod("Seamless", ["SeamlessCoop/ersc.dll"])
        self.organizer.add_mod("Convergence", ["me3/convergence.me3"])
        game = game_for(self.organizer)
        copy = game._seamless_load_dll()
        settings = copy.with_name("ersc_settings.ini")
        self.assertEqual(settings.read_text(), "[SAVE]\nsave_file_extension = cnvco2\n")
        stamp = settings.stat().st_mtime_ns
        self.assertEqual(game._seamless_load_dll(), copy)
        self.assertEqual(settings.stat().st_mtime_ns, stamp)

    def test_runtime_dll_names(self):
        check = type(game_for(self.organizer))._is_game_runtime_dll
        for name in ("STEAM_API64.dll", "oo2core_9_win64.dll", "me3_mod_host.dll", "libzstd.dll"):
            self.assertTrue(check(name), name)
        self.assertFalse(check("oo2core_9.dll"))

    def test_resolved_mod_root_cache(self):
        cls = type(game_for(self.organizer))
        cls._mod_root_reads = {}
        try:
            path = str(self.organizer.mods)
            self.assertEqual(cls._resolved_mod_root(path), Path(path).resolve())
            cls._mod_root_reads[path] = None
            self.assertIsNone(cls._resolved_mod_root(path))
        finally:
            cls._mod_root_reads = None


if __name__ == "__main__":
    unittest.main()
