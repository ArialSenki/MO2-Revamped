# pyright: reportUnboundVariable=false

import glob
import importlib
import os
import pathlib
import site
import sys
import typing

from .basic_game import BasicGame
from .basic_game_ini import BasicIniGame

site.addsitedir(os.path.join(os.path.dirname(__file__), "lib"))


BasicGame.setup()


def createPlugins():
    # List of game class from python:
    game_plugins: typing.List[BasicGame] = []

    # We are going to list all game plugins:
    curpath = os.path.abspath(os.path.dirname(__file__))
    # MO2 Revamped: from MO2 ae4ae8b
    escaped_games_path = glob.escape(os.path.join(curpath, "games"))

    # MO2 Revamped: with the release's manifest and the instance's game from
    # MO2, only that game's module is imported; see lazy_games
    from . import lazy_games

    instance_game = lazy_games.instance_game()
    manifest = (
        lazy_games.read_manifest(os.path.join(curpath, "games"))
        if instance_game
        else None
    )
    listed = manifest.get("modules", {}) if manifest else {}

    # List all the .ini files:
    for file in glob.glob(os.path.join(escaped_games_path, "*.ini")):
        game_plugins.append(BasicIniGame(file))

    # List all the python plugins:
    for file in glob.glob(os.path.join(escaped_games_path, "*.py")):
        module_p = os.path.relpath(file, os.path.join(curpath, "games"))
        if module_p == "__init__.py":
            continue

        stubs = _stubs_for(lazy_games, listed.get(module_p), file, module_p, instance_game)
        if stubs is not None:
            game_plugins.extend(stubs)
            continue

        # Import the module:
        try:
            module = importlib.import_module(".games." + module_p[:-3], __package__)
        except ImportError as e:
            print("Failed to import module {}: {}".format(module_p, e), file=sys.stderr)
        except Exception as e:
            print("Failed to import module {}: {}".format(module_p, e), file=sys.stderr)

        # Lookup game plugins:
        for name in dir(module):
            if hasattr(module, name):
                obj = getattr(module, name)
                if (
                    isinstance(obj, type)
                    and issubclass(obj, BasicGame)
                    and obj is not BasicGame
                ):
                    try:
                        game_plugins.append(obj())
                    except Exception as e:
                        print(
                            "Failed to instantiate {}: {}".format(name, e),
                            file=sys.stderr,
                        )

    # A game can bring its own tools in games/<name>/plugins:
    tools = manifest.get("tools", {}) if manifest else {}
    for path in pathlib.Path(curpath, "games").rglob("plugins/__init__.py"):
        folder = path.parent.parent.name
        # MO2 Revamped: only the instance's game's tools, when the manifest
        # says which game they belong to
        if folder in tools and instance_game.casefold() not in {
            g.casefold() for g in tools[folder]
        }:
            continue
        module_path = "." + os.path.relpath(path.parent, curpath).replace(os.sep, ".")
        try:
            module = importlib.import_module(module_path, __package__)
            game_plugins.extend(module.createPlugins())
        except Exception as e:
            print("Failed to load {}: {}".format(module_path, e), file=sys.stderr)

    return game_plugins


def _stubs_for(lazy_games, entry, file, module_p, instance_game):
    """MO2 Revamped: the stub game plugins of a module, or None to import it
    (no manifest entry, a changed file, or a module of the instance's game)."""
    if not entry or not instance_game:
        return None
    try:
        if lazy_games.file_hash(file) != entry.get("sha256"):
            return None
        classes = entry["classes"]
        wanted = instance_game.casefold()
        if any(c.get("answers", {}).get("gameName", "").casefold() == wanted for c in classes):
            return None
        return [lazy_games.make_stub_class(".games." + module_p[:-3], c)() for c in classes]
    except Exception as e:
        print("Failed to use the manifest for {}: {}".format(module_p, e), file=sys.stderr)
        return None
