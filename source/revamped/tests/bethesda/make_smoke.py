"""Fake game trees for the Bethesda smoke tests (MO2 Revamped): each of the 14
Bethesda games opened in MO2 with the old and the new game plugin.

    python -I make_smoke.py <root> <game> [<saves folder>]
    python -I make_smoke.py diff <old.json> <new.json> <old root> <new root> [<old bed> <new bed>]

The first form writes <root>/game (the game's exe and launcher are copies of
whoami.exe, the data folder has the masters and DLC the game definition lists,
as minimal plugin headers of the game's format) and <root>/instance (two mods,
profile Default with local saves and the game's INI files, ModOrganizer.ini).
The tree is valid for both the old plugins (source/upstream/modorganizer_super/
game_*) and the new ones (source/revamped/games/bethesda/games/*). Nothing
outside <root> is touched. With a saves folder (the deep pass), its files are
copied into the profile's saves and automatic archive invalidation is on.

The second form compares two mo2r_smoke_dump.py outputs with each root (and
the user's own folders) replaced by a placeholder, and prints the differences.
"""
import json
import os
import shutil
import struct
import sys

# 2010-10-19 00:00 UTC; every file gets an explicit time, a fixed offset of it,
# so the file-time games load in the same order on every tree
T0 = 1287446400
HOUR = 3600

# HEDR version and form version per format; Oblivion's records have no version
# field (a 20-byte header), Morrowind's are TES3
FORMATS = {
    "tes4_ob": (1.0, None),
    "tes4_sk": (0.94, 40),
    "tes4_sse": (1.71, 44),
    "tes4_fo3": (0.94, 15),
    "tes4_fnv": (1.34, 15),
    "tes4_fo4": (1.0, 131),
    "tes4_sf": (0.96, 555),
    "tes3": (1.3, None),
}

FO3_DLC = ["ThePitt.esm", "Anchorage.esm", "BrokenSteel.esm", "PointLookout.esm", "Zeta.esm"]
NV_DLC = ["DeadMoney.esm", "HonestHearts.esm", "OldWorldBlues.esm", "LonesomeRoad.esm",
          "GunRunnersArsenal.esm", "CaravanPack.esm", "ClassicPack.esm", "MercenaryPack.esm",
          "TribalPack.esm"]
FO4_DLC = ["DLCRobot.esm", "DLCworkshop01.esm", "DLCCoast.esm", "DLCWorkshop02.esm",
           "DLCWorkshop03.esm", "DLCNukaWorld.esm", "DLCUltraHighResolution.esm"]
SK_DLC = ["Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm"]

# per game: gameName (ModOrganizer.ini), the exes in the game folder, the data
# folder, the plugin format, the game's plugins in Data (masters first, then
# DLC), its Creation Club file and the CC plugin it lists, whether light
# plugins exist, the load order kind ('star' = plugins.txt with '*', 'time' =
# plugins.txt of active plugins and file times, 'mw' = Morrowind.ini), the
# edition (game_edition) and the INI files of both plugins
GAMES = {
    "skyrim": dict(name="Skyrim", exes=["TESV.exe", "SkyrimLauncher.exe"], fmt="tes4_sk",
                   plugins=["Skyrim.esm", "Update.esm"] + SK_DLC +
                   ["HighResTexturePack01.esp", "HighResTexturePack02.esp", "HighResTexturePack03.esp"],
                   order="time", inis=["skyrim.ini", "skyrimprefs.ini"]),
    "skyrimse": dict(name="Skyrim Special Edition", exes=["SkyrimSE.exe", "SkyrimSELauncher.exe"],
                     fmt="tes4_sse", plugins=["Skyrim.esm", "Update.esm"] + SK_DLC,
                     ccc=("Skyrim.ccc", "ccBGSSSE001-Fish.esm"), light=True, order="star",
                     edition="Steam", inis=["skyrim.ini", "skyrimprefs.ini", "skyrimcustom.ini"]),
    "skyrimvr": dict(name="Skyrim VR", exes=["SkyrimVR.exe"], fmt="tes4_sse",
                     plugins=["Skyrim.esm", "Update.esm"] + SK_DLC + ["SkyrimVR.esm"],
                     ccc=("Skyrim.ccc", "ccBGSSSE001-Fish.esm"), order="star", edition="Regular",
                     inis=["skyrimvr.ini", "skyrimprefs.ini"]),
    "enderal": dict(name="Enderal", exes=["skse_loader.exe", "Enderal Launcher.exe", "TESV.exe"],
                    fmt="tes4_sk", plugins=["Skyrim.esm", "Enderal - Forgotten Stories.esm", "Update.esm"],
                    order="time", inis=["enderal.ini", "enderalprefs.ini"]),
    "enderalse": dict(name="Enderal Special Edition",
                      exes=["skse64_loader.exe", "Enderal Launcher.exe", "SkyrimSE.exe"], fmt="tes4_sse",
                      plugins=["Skyrim.esm", "Update.esm"] + SK_DLC +
                      ["Enderal - Forgotten Stories.esm", "SkyUI_SE.esp"],
                      light=True, order="star", edition="Steam", inis=["Enderal.ini", "EnderalPrefs.ini"]),
    "fallout3": dict(name="Fallout 3", exes=["Fallout3.exe", "FalloutLauncher.exe"], fmt="tes4_fo3",
                     plugins=["Fallout3.esm"] + FO3_DLC, order="time", edition="Regular",
                     inis=["fallout.ini", "falloutprefs.ini", "custom.ini", "FalloutCustom.ini",
                           "GECKCustom.ini", "GECKPrefs.ini"]),
    "falloutnv": dict(name="New Vegas", exes=["FalloutNV.exe", "FalloutNVLauncher.exe"], fmt="tes4_fnv",
                      plugins=["FalloutNV.esm"] + NV_DLC, order="time", edition="Steam",
                      inis=["fallout.ini", "falloutprefs.ini", "falloutcustom.ini", "GECKCustom.ini",
                            "GECKPrefs.ini"]),
    "ttw": dict(name="TTW", exes=["FalloutNV.exe", "FalloutNVLauncher.exe"], fmt="tes4_fnv",
                plugins=["FalloutNV.esm"] + NV_DLC + ["Fallout3.esm"] + FO3_DLC +
                ["TaleOfTwoWastelands.esm", "YUPTTW.esm"],
                order="time", edition="Steam",
                inis=["fallout.ini", "falloutprefs.ini", "falloutcustom.ini", "GECKCustom.ini",
                      "GECKPrefs.ini"]),
    "fallout4": dict(name="Fallout 4", exes=["Fallout4.exe", "Fallout4Launcher.exe"], fmt="tes4_fo4",
                     plugins=["Fallout4.esm"] + FO4_DLC,
                     ccc=("Fallout4.ccc", "ccBGSFO4001-PipBoy(Black).esl"), light=True, order="star",
                     edition="Regular", archive="ba2",
                     inis=["fallout4.ini", "fallout4prefs.ini", "fallout4custom.ini"]),
    "fallout4vr": dict(name="Fallout 4 VR", exes=["Fallout4VR.exe"], fmt="tes4_fo4",
                       plugins=["Fallout4.esm", "Fallout4_VR.esm"] + FO4_DLC,
                       ccc=("Fallout4.ccc", "ccBGSFO4001-PipBoy(Black).esm"), order="star",
                       edition="Regular", archive="ba2",
                       inis=["fallout4.ini", "fallout4custom.ini", "fallout4prefs.ini"]),
    "oblivion": dict(name="Oblivion", exes=["Oblivion.exe", "OblivionLauncher.exe"], fmt="tes4_ob",
                     plugins=["Oblivion.esm", "Update.esm", "DLCBattlehornCastle.esp", "DLCShiveringIsles.esp",
                              "Knights.esp", "DLCFrostcrag.esp", "DLCSpellTomes.esp", "DLCMehrunesRazor.esp",
                              "DLCOrrery.esp", "DLCThievesDen.esp", "DLCVileLair.esp", "DLCHorseArmor.esp"],
                     order="time", inis=["oblivion.ini", "oblivionprefs.ini"]),
    "nehrim": dict(name="Nehrim", exes=["NehrimLauncher.exe", "Oblivion.exe"], fmt="tes4_ob",
                   plugins=["Nehrim.esm", "Translation.esp"], order="time",
                   inis=["oblivion.ini", "oblivionprefs.ini"]),
    "morrowind": dict(name="Morrowind", exes=["Morrowind.exe", "Morrowind Launcher.exe"], fmt="tes3",
                      data="Data Files", plugins=["Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm"],
                      order="mw", inis=["Morrowind.ini"]),
    "starfield": dict(name="Starfield", exes=["Starfield.exe", "StarfieldLauncher.exe"], fmt="tes4_sf",
                      plugins=["Starfield.esm", "Constellation.esm", "OldMars.esm",
                               "BlueprintShips-Starfield.esm", "SFBGS007.esm", "SFBGS008.esm",
                               "SFBGS006.esm", "SFBGS003.esm", "SFBGS004.esm", "ShatteredSpace.esm"],
                      ccc=("Starfield.ccc", "SFBGS00A_SmokeCreation.esm"), light=True, order="star",
                      edition="Regular", archive="ba2",
                      inis=["StarfieldPrefs.ini", "StarfieldCustom.ini"]),
}

MO2_HEADER = "# This file was automatically generated by Mod Organizer."


def tes4(fmt, master=False, light=False):
    version, form = FORMATS[fmt]
    hedr = b"HEDR" + struct.pack("<H", 12) + struct.pack("<fiI", version, 1, 0x800)
    # Starfield's light flag is 0x100 (0x200 is its overlay flag)
    flags = (1 if master else 0) | ((0x100 if fmt == "tes4_sf" else 0x200) if light else 0)
    if form is None:  # Oblivion: type, size, flags, form id, version control
        return b"TES4" + struct.pack("<IIII", len(hedr), flags, 0, 0) + hedr
    return b"TES4" + struct.pack("<IIIIHH", len(hedr), flags, 0, 0, form, 0) + hedr


def tes3(master=False):
    # HEDR: version, file type (1 = master), author[32], description[256], records
    hedr = struct.pack("<fI32s256sI", 1.3, 1 if master else 0, b"MO2R smoke", b"", 1)
    sub = b"HEDR" + struct.pack("<I", len(hedr)) + hedr
    return b"TES3" + struct.pack("<III", len(sub), 0, 0) + sub


def plugin(fmt, name, light=False):
    master = name.lower().endswith((".esm", ".esl"))
    if fmt == "tes3":
        return tes3(master)
    return tes4(fmt, master, light or name.lower().endswith(".esl"))


def write(path, data, mtime):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    os.utime(path, (mtime, mtime))


def at(hours):
    return T0 + hours * HOUR


def make(root, key, saves=None):
    g = GAMES[key]
    fmt = g["fmt"]
    game, inst = os.path.join(root, "game"), os.path.join(root, "instance")
    data = os.path.join(game, g.get("data", "Data"))
    os.makedirs(data)
    for exe in g["exes"]:
        shutil.copy(os.path.join(os.environ["SystemRoot"], "System32", "whoami.exe"), os.path.join(game, exe))

    game_plugins = list(g["plugins"])
    for i, name in enumerate(game_plugins):
        write(os.path.join(data, name), plugin(fmt, name), at(i))
    cc = None
    if "ccc" in g:
        ccc, cc = g["ccc"]
        write(os.path.join(data, cc), plugin(fmt, cc, light=g.get("light", False)), at(len(game_plugins)))
        with open(os.path.join(game, ccc), "w", newline="\r\n") as f:
            f.write(cc + "\n")
        game_plugins.append(cc)

    # two mods: a master (a light one where the game has them) and an .esp with
    # an archive and an inactive .esp next to it; the master is listed first
    ext = g.get("archive", "bsa")
    second = "SmokeLight.esl" if g.get("light") else "SmokeMaster.esm"
    archive = "SmokePlugin.bsa" if key == "morrowind" else f"SmokePlugin - Main.{ext}"
    mods = [("Smoke Plugin", [("SmokePlugin.esp", plugin(fmt, "SmokePlugin.esp"), at(100)),
                              ("SmokeInactive.esp", plugin(fmt, "SmokeInactive.esp"), at(101)),
                              (archive, b"", at(100))]),
            ("Smoke Master", [(second, plugin(fmt, second), at(99))])]
    for mod, files in mods:
        for name, blob, mtime in files:
            write(os.path.join(inst, "mods", mod, name), blob, mtime)

    # load order: the game's plugins, the master, the .esp, the inactive .esp;
    # the last DLC (where there is one beyond the masters) inactive
    order = game_plugins + [second, "SmokePlugin.esp", "SmokeInactive.esp"]
    inactive = {"SmokeInactive.esp"}
    if len(g["plugins"]) > 2:
        inactive.add(g["plugins"][-1])
    active = [p for p in order if p not in inactive]

    prof = os.path.join(inst, "profiles", "Default")
    os.makedirs(os.path.join(prof, "saves"))
    with open(os.path.join(prof, "modlist.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("# This file was automatically generated by Mod Organizer.\n"
                "".join(f"+{mod}\n" for mod in ("Smoke Master", "Smoke Plugin")))
    # plugins.txt in the system code page, loadorder.txt in UTF-8, as MO2 writes them
    with open(os.path.join(prof, "plugins.txt"), "w", encoding="mbcs", newline="\r\n") as f:
        f.write(MO2_HEADER + "\n")
        if g["order"] == "star":
            f.write("".join(("*" if p in active else "") + p + "\n" for p in order))
        else:
            f.write("".join(p + "\n" for p in active))
    with open(os.path.join(prof, "loadorder.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(MO2_HEADER + "\n" + "".join(p + "\n" for p in order))
    for name in ("plugins.txt", "loadorder.txt"):
        os.utime(os.path.join(prof, name), (at(200), at(200)))
    if saves:
        for name in sorted(os.listdir(saves)):
            if os.path.isfile(os.path.join(saves, name)):
                shutil.copy2(os.path.join(saves, name), os.path.join(prof, "saves", name))
    with open(os.path.join(prof, "settings.ini"), "w") as f:
        f.write("[General]\nLocalSaves=true\nLocalSettings=true\nAutomaticArchiveInvalidation="
                + ("true" if saves else "false") + "\n")
    for ini in g["inis"]:
        with open(os.path.join(prof, ini), "w", newline="\r\n") as f:
            if g["order"] == "mw":
                # Morrowind's plugin list is its INI's [Game Files]
                f.write("[Game Files]\n" + "".join(f"GameFile{i}={p}\n" for i, p in enumerate(active)))
    if g["order"] == "mw":
        shutil.copy(os.path.join(prof, "Morrowind.ini"), os.path.join(game, "Morrowind.ini"))

    with open(os.path.join(inst, "ModOrganizer.ini"), "w") as f:
        f.write("[General]\ngameName=" + g["name"] + "\ngamePath=@ByteArray(" + game.replace("\\", "\\\\") + ")\n"
                + (f"game_edition={g['edition']}\n" if "edition" in g else "")
                + "selected_profile=@ByteArray(Default)\nfirst_start=false\n")
    print(f"{key}: {len(game_plugins)} game plugins, {len(order) - len(game_plugins)} mod plugins")


# ---------------------------------------------------------------- diff

def normaliser(root, bed=None):
    subs = []
    for path, tag in ((root, "<ROOT>"), (bed, "<BED>"),
                      (os.environ.get("LOCALAPPDATA", ""), "<LOCALAPPDATA>"),
                      (os.path.join(os.environ.get("USERPROFILE", ""), "Documents"), "<DOCUMENTS>"),
                      (os.environ.get("USERPROFILE", ""), "<USERPROFILE>")):
        if path:
            p = os.path.abspath(path)
            for form in (p, p.replace("\\", "/")):
                subs.append((form.lower(), tag))

    def norm(value):
        if isinstance(value, str):
            low = value.lower()
            for form, tag in subs:
                i = low.find(form)
                while i >= 0:
                    value = value[:i] + tag + value[i + len(form):]
                    low = value.lower()
                    i = low.find(form, i + len(tag))
            return value
        if isinstance(value, list):
            return [norm(v) for v in value]
        if isinstance(value, dict):
            return {norm(k): norm(v) for k, v in value.items()}
        return value
    return norm


def flatten(value, prefix, out):
    if isinstance(value, dict):
        for k, v in value.items():
            flatten(v, f"{prefix}.{k}" if prefix else k, out)
    else:
        out[prefix] = value
    return out


def diff(old_json, new_json, old_root, new_root, old_bed=None, new_bed=None):
    with open(old_json, encoding="utf-8") as f:
        old = normaliser(old_root, old_bed)(json.load(f))
    with open(new_json, encoding="utf-8") as f:
        new = normaliser(new_root, new_bed)(json.load(f))
    a, b = flatten(old, "", {}), flatten(new, "", {})
    lines = []
    for key in sorted(set(a) | set(b)):
        if key.startswith("meta."):
            continue
        va, vb = a.get(key, "<absent>"), b.get(key, "<absent>")
        if va != vb:
            lines.append(f"  {key}: old={json.dumps(va, ensure_ascii=False)} new={json.dumps(vb, ensure_ascii=False)}")
    print("\n".join(lines) if lines else "  identical")
    return 1 if lines else 0


def main():
    if len(sys.argv) in (6, 8) and sys.argv[1] == "diff":
        sys.exit(diff(*sys.argv[2:]))
    if len(sys.argv) not in (3, 4) or sys.argv[2] not in GAMES:
        raise SystemExit(__doc__ + "\ngames: " + ", ".join(GAMES))
    root = sys.argv[1]
    if os.path.exists(root):
        shutil.rmtree(root)
    make(root, sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else None)


if __name__ == "__main__":
    main()
