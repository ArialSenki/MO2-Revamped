"""Writes plugins/basic_games/games/manifest.json for a release stage.

basic_games' loader (lazy_games.py) imports only the instance's game module;
for every other game it makes a stub from this manifest. Each module is
imported here the way the loader imports it, with the stage's own mobase and
PyQt6, and for every game class it finds the manifest keeps:

- attributes: the values of the attributes BasicGame reads (GameName,
  GameSteamId, GameBinary, ...), as the object has them after __init__;
- answers: what name(), gameName(), validShortNames()... return;
- version and settings: version() and settings(), which MO2 asks every plugin
  for when it loads them;
- overrides: the public methods the class defines over BasicGame's, which the
  stub hands to the real class once something calls them.

A module that can't be described (a value JSON can't hold, an import or
construction error) is left out: the loader imports it as before. The same
goes for a module whose file changes after the manifest is written.

Usage: python -I make_basic_games_manifest.py <stage folder>
"""

import builtins
import glob
import hashlib
import importlib
import inspect
import json
import os
import pathlib
import sys
from pathlib import Path

STATIC_GETTERS = (
    "name",
    "author",
    "description",
    "gameName",
    "gameShortName",
    "validShortNames",
    "gameNexusName",
    "nexusGameID",
    "nexusModOrganizerID",
)


def jsonable(value):
    if isinstance(value, Path):
        return str(value)
    if isinstance(value, tuple):
        value = list(value)
    if isinstance(value, list):
        return [jsonable(v) for v in value]
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    raise TypeError(f"{type(value).__name__} can't go in the manifest")


def describe(obj, BasicGame) -> dict:
    cls = type(obj)
    exposed = sorted(
        m._exposed_name for m in vars(obj._mappings).values() if hasattr(m, "_exposed_name")
    )
    attributes = {n: jsonable(getattr(obj, n)) for n in exposed if hasattr(obj, n)}
    answers = {g: jsonable(getattr(obj, g)()) for g in STATIC_GETTERS}
    settings = [
        [s.key, s.description, jsonable(s.default_value)] for s in obj.settings()
    ]
    overrides = sorted(
        name
        for name, value in inspect.getmembers(cls)
        if not name.startswith("_")
        and (inspect.isfunction(value) or inspect.ismethod(value))
        and value is not getattr(BasicGame, name, None)
    )
    return {
        "class": cls.__name__,
        "attributes": attributes,
        "answers": answers,
        "version": obj.version().canonicalString(),
        "settings": settings,
        "overrides": overrides,
    }


def main() -> int:
    stage = os.path.abspath(sys.argv[1])
    plugins = os.path.join(stage, "plugins")
    os.add_dll_directory(stage)
    os.add_dll_directory(os.path.join(stage, "dlls"))
    os.environ["QT_PLUGIN_PATH"] = stage
    sys.dont_write_bytecode = True
    # after this Python's own standard library (the stage's CPython extensions,
    # _ctypes and the rest, are for the bundled interpreter), and without its
    # site-packages: PyQt6 and mobase come from the stage only
    sys.path = [p for p in sys.path if "site-packages" not in p.lower()]
    sys.path += [os.path.join(plugins, "plugin_python", "libs"), plugins, os.path.join(plugins, "data")]

    from PyQt6.QtWidgets import QApplication, QMessageBox

    import mobase

    # BasicGame.setup() reports store-scan errors in a message box: on a build
    # machine they're only printed
    QMessageBox.critical = staticmethod(lambda *args, **kwargs: print("basic_games:", *args[1:3]))

    # plugin_python makes mobase visible to every plugin
    builtins.mobase = mobase
    app = QApplication(["manifest"])  # noqa: F841 (game classes may need one)

    import basic_games
    from basic_games.basic_game import BasicGame

    games_dir = os.path.join(os.path.dirname(basic_games.__file__), "games")
    modules, skipped = {}, []
    for file in glob.glob(os.path.join(glob.escape(games_dir), "*.py")):
        module_p = os.path.relpath(file, games_dir)
        if module_p == "__init__.py":
            continue
        try:
            module = importlib.import_module("basic_games.games." + module_p[:-3])
            classes = []
            # the loader's own search: every BasicGame subclass the module has
            for name in dir(module):
                obj = getattr(module, name)
                if isinstance(obj, type) and issubclass(obj, BasicGame) and obj is not BasicGame:
                    try:
                        game = obj()
                    except Exception:
                        continue  # the loader skips it too
                    classes.append(describe(game, BasicGame))
            with open(file, "rb") as f:
                digest = hashlib.sha256(f.read()).hexdigest()
            modules[module_p] = {"sha256": digest, "classes": classes}
        except Exception as error:
            skipped.append(f"{module_p}: {type(error).__name__}: {error}")

    # games/<folder>/plugins: the tools of the game in games/game_<folder>.py
    tools = {}
    for path in pathlib.Path(games_dir).rglob("plugins/__init__.py"):
        folder = path.parent.parent.name
        entry = modules.get(f"game_{folder}.py")
        if entry and entry["classes"]:
            tools[folder] = [c["answers"]["gameName"] for c in entry["classes"]]

    manifest = {"format": 1, "modules": modules, "tools": tools}
    with open(os.path.join(games_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=1, sort_keys=True, ensure_ascii=False)
        f.write("\n")
    games = sum(len(m["classes"]) for m in modules.values())
    print(f"manifest: {len(modules)} modules, {games} games, {len(tools)} tool folders")
    for line in skipped:
        print("not in the manifest (imported at every start):", line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
