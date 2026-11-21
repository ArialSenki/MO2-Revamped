# MO2 Python plugin for the Bethesda smoke tests (copied into a test bed only,
# never shipped): once the interface is up and the plugin list has refreshed,
# writes what mobase shows of the managed game, its plugin list, mod list and
# game features to MO2R_SMOKE_OUT/smoke.json, copies the profile's plugins.txt
# and loadorder.txt there, then writes `done`. Every call is wrapped: one that
# fails or doesn't exist is recorded as {"error": ...} and the rest goes on.
import json
import os
import shutil
import traceback

import mobase
from PyQt6.QtCore import QDir, QTimer

OUT = os.environ.get("MO2R_SMOKE_OUT", "")


def plain(value):
    """mobase / Qt values as JSON."""
    if value is None or isinstance(value, (bool, int, float, str)):
        return value
    if isinstance(value, (list, tuple, set)):
        items = [plain(v) for v in value]
        return sorted(items, key=str) if isinstance(value, set) else items
    if isinstance(value, dict):
        return {str(plain(k)): plain(v) for k, v in value.items()}
    for attr in ("absoluteFilePath", "absolutePath"):  # QFileInfo, QDir
        if hasattr(value, attr):
            try:
                return getattr(value, attr)()
            except Exception:
                pass
    if hasattr(value, "name") and hasattr(value, "value"):  # enums
        return f"{type(value).__name__}.{value.name}"
    if isinstance(value, mobase.VersionInfo):
        return str(value)
    return repr(value)


def call(fn, *args):
    try:
        return plain(fn(*args))
    except Exception as e:
        return {"error": f"{type(e).__name__}: {e}"}


def method(obj, name, *args):
    f = getattr(obj, name, None)
    if f is None:
        return {"error": f"no {name}"}
    return call(f, *args)


class SmokeDump(mobase.IPlugin):
    def __init__(self):
        super().__init__()
        self._org = None

    def init(self, organizer):
        self._org = organizer
        if OUT:
            organizer.onUserInterfaceInitialized(lambda w: QTimer.singleShot(5000, self._dump))
        return True

    def _game(self, g):
        out = {}
        for name in ("name", "author", "description", "version", "gameName", "displayGameName",
                     "gameShortName", "gameNexusName", "validShortNames", "primarySources",
                     "lootGameName", "nexusGameID", "nexusModOrganizerID", "steamAPPId",
                     "binaryName", "getLauncherName", "gameDirectory", "dataDirectory",
                     "secondaryDataDirectories", "documentsDirectory", "savesDirectory",
                     "primaryPlugins", "enabledPlugins", "DLCPlugins", "CCPlugins", "iniFiles",
                     "loadOrderMechanism", "sortMechanism", "gameVariants", "isInstalled",
                     "gameVersion", "getSupportURL"):
            out[name] = method(g, name)
        out["looksValid"] = call(lambda: g.looksValid(g.gameDirectory()))

        def exes():
            return [{"title": e.title(), "binary": plain(e.binary()), "arguments": list(e.arguments()),
                     "workingDirectory": plain(e.workingDirectory()), "steamAppID": e.steamAppID()}
                    for e in g.executables()]
        out["executables"] = call(exes)

        def forced():
            return [{"process": f.process(), "library": f.library(), "enabled": f.enabled(),
                     "forced": f.forced()} for f in g.executableForcedLoads()]
        out["executableForcedLoads"] = call(forced)

        def settings():
            return {s.key: self._org.pluginSetting(g.name(), s.key) for s in g.settings()}
        out["settings"] = call(settings)
        return out

    def _plugins(self):
        pl = self._org.pluginList()
        names = sorted(pl.pluginNames(), key=lambda n: (pl.priority(n), n.lower()))
        rows = {}
        for n in names:
            row = {}
            for m in ("state", "priority", "loadOrder", "isMasterFlagged", "hasMasterExtension",
                      "isLightFlagged", "hasLightExtension", "isMediumFlagged", "hasNoRecords",
                      "masters", "origin"):
                row[m] = method(pl, m, n)
            rows[n] = row
        return {"order": names, "plugins": rows}

    def _mods(self):
        ml = self._org.modList()
        try:
            names = list(ml.allModsByProfilePriority())
        except Exception:
            names = list(ml.allMods())
        rows = {}
        for n in names:
            row = {"state": method(ml, "state", n), "priority": method(ml, "priority", n)}
            mod = None
            try:
                mod = ml.getMod(n)
            except Exception as e:
                row["getMod"] = {"error": str(e)}
            if mod is not None:
                for m in ("isForeign", "isSeparator", "isBackup", "isOverwrite", "gameName",
                          "absolutePath", "version", "nexusId"):
                    row[m] = method(mod, m)
            rows[n] = row
        return {"order": names, "mods": rows}

    def _features(self):
        gf = self._org.gameFeatures()
        prof = self._org.profile()
        out = {}

        def feature(cls_name):
            cls = getattr(mobase, cls_name, None)
            if cls is None:
                return None, {"error": f"mobase has no {cls_name}"}
            try:
                f = gf.gameFeature(cls)
            except Exception as e:
                return None, {"error": f"{type(e).__name__}: {e}"}
            return f, {"present": f is not None}

        f, r = feature("BSAInvalidation")
        if f is not None:
            r["isInvalidationBSA(SmokePlugin - Main.bsa)"] = call(f.isInvalidationBSA, "SmokePlugin - Main.bsa")
        out["BSAInvalidation"] = r

        f, r = feature("DataArchives")
        if f is not None:
            r["vanillaArchives"] = call(f.vanillaArchives)
            r["archives"] = call(f.archives, prof)
        out["DataArchives"] = r

        f, r = feature("GamePlugins")
        if f is not None:
            r["lightPluginsAreSupported"] = call(f.lightPluginsAreSupported)
            r["mediumPluginsAreSupported"] = call(f.mediumPluginsAreSupported)
            r["getLoadOrder"] = call(f.getLoadOrder)
        out["GamePlugins"] = r

        f, r = feature("LocalSavegames")
        if f is not None:
            r["mappings"] = call(lambda: [str(m.source) + " -> " + str(m.destination)
                                          for m in f.mappings(QDir(prof.absolutePath() + "/saves"))])
        out["LocalSavegames"] = r

        mods = {}
        try:
            for n in ("Smoke Plugin", "Smoke Master"):
                m = self._org.modList().getMod(n)
                if m is not None:
                    mods[n] = m.fileTree()
        except Exception as e:
            out["fileTree error"] = str(e)

        f, r = feature("ModDataChecker")
        if f is not None:
            for n, tree in mods.items():
                r[f"dataLooksValid({n})"] = call(f.dataLooksValid, tree)
        out["ModDataChecker"] = r

        f, r = feature("ModDataContent")
        if f is not None:
            r["getAllContents"] = call(lambda: [f"{c.id}:{c.name}:{c.isOnlyForFilter()}"
                                                for c in f.getAllContents()])
            for n, tree in mods.items():
                r[f"getContentsFor({n})"] = call(f.getContentsFor, tree)
        out["ModDataContent"] = r

        f, r = feature("SaveGameInfo")
        out["SaveGameInfo"] = r

        f, r = feature("ScriptExtender")
        if f is not None:
            for m in ("binaryName", "loaderName", "loaderPath", "pluginPath", "savegameExtension",
                      "isInstalled", "getExtenderVersion"):
                r[m] = method(f, m)
        out["ScriptExtender"] = r

        f, r = feature("UnmanagedMods")
        if f is not None:
            r["mods(official)"] = call(f.mods, True)
            r["mods(all)"] = call(f.mods, False)

            def details():
                return {m: {"displayName": f.displayName(m), "referenceFile": plain(f.referenceFile(m)),
                            "secondaryFiles": plain(f.secondaryFiles(m))} for m in f.mods(False)}
            r["details"] = call(details)
        out["UnmanagedMods"] = r
        return out

    def _profile(self):
        p = self._org.profile()
        out = {"name": method(p, "name"), "absolutePath": method(p, "absolutePath"),
               "localSavesEnabled": method(p, "localSavesEnabled"),
               "localSettingsEnabled": method(p, "localSettingsEnabled"),
               "invalidationActive": call(lambda: p.invalidationActive()[0])}
        try:
            out["iniFiles"] = {i: p.absoluteIniFilePath(i) for i in self._org.managedGame().iniFiles()}
        except Exception as e:
            out["iniFiles"] = {"error": str(e)}
        # the INIs as MO2 left them (local saves write into one)
        try:
            out["iniText"] = {}
            for i in self._org.managedGame().iniFiles():
                path = p.absoluteIniFilePath(i)
                if os.path.exists(path):
                    with open(path, encoding="mbcs", errors="replace") as f:
                        out["iniText"][i] = f.read()
        except Exception as e:
            out["iniText"] = {"error": str(e)}
        try:
            out["files"] = sorted(os.listdir(p.absolutePath()))
        except Exception as e:
            out["files"] = {"error": str(e)}
        return out

    def _dump(self):
        os.makedirs(OUT, exist_ok=True)
        result = {}
        for key, fn in (("game", lambda: self._game(self._org.managedGame())),
                        ("profile", self._profile), ("pluginList", self._plugins),
                        ("modList", self._mods), ("features", self._features)):
            try:
                result[key] = fn()
            except Exception:
                result[key] = {"error": traceback.format_exc()}
        result["meta"] = {"appVersion": call(lambda: str(self._org.appVersion()))}
        with open(os.path.join(OUT, "smoke.json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=1, ensure_ascii=False)
        prof = self._org.profilePath()
        for name in ("plugins.txt", "loadorder.txt"):
            if os.path.exists(os.path.join(prof, name)):
                shutil.copyfile(os.path.join(prof, name), os.path.join(OUT, name))
        with open(os.path.join(OUT, "done"), "w") as f:
            f.write("ok")

    def name(self):
        return "MO2R Smoke Dump"

    def author(self):
        return "MO2 Revamped"

    def description(self):
        return "Test-only plugin."

    def version(self):
        return mobase.VersionInfo(1, 0, 0)

    def settings(self):
        return []


def createPlugin():
    return SmokeDump()
