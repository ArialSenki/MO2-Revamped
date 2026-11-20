"""The BG3 tools in MO2's Tools menu. Their names are basic_games' (daescha),
so instances keep their settings."""

from __future__ import annotations

import json
import os
from pathlib import Path

import mobase
from PyQt6.QtGui import QIcon, QPixmap

from ..util import GAME_NAME, PLUGIN_NAME, log_info, log_warning, tr


def _bg3_game(organizer: mobase.IOrganizer):
    game = organizer.managedGame()
    if game is not None and game.name() == PLUGIN_NAME and hasattr(game, "launch"):
        return game
    return None


class BG3ToolPlugin(mobase.IPluginTool):
    desc = sub_name = ""
    icon_name = ""

    def __init__(self):
        super().__init__()
        self._icon: QIcon | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def version(self):
        return mobase.VersionInfo(2, 0, 0)

    def author(self):
        return "MO2 Revamped"

    def name(self):
        return f"BG3 Tools: {self.sub_name}"

    def displayName(self):
        return f"BG3 Tools/{tr(self.sub_name)}"

    def tooltip(self):
        return self.description()

    def requirements(self) -> list[mobase.IPluginRequirement]:
        return [mobase.PluginRequirementFactory.gameDependency(GAME_NAME)]

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def icon(self) -> QIcon:
        if self._icon is None:
            from . import icons

            pixmap = QPixmap()
            pixmap.loadFromData(getattr(icons, self.icon_name), "SVG")
            self._icon = QIcon(pixmap)
        return self._icon

    def description(self) -> str:
        return tr(self.desc)

    def setParentWidget(self, widget) -> None:
        self._parent = widget


class BG3ToolCheckForLsLibUpdates(BG3ToolPlugin):
    icon_name = "download"
    sub_name = "Check For LsLib Updates"
    desc = (
        "Look for a new LSLib release on GitHub and offer to download it. "
        "Only this tool and a download you agree to contact GitHub."
    )

    def display(self):
        game = _bg3_game(self._organizer)
        if game is not None:
            game.lslib().download_lslib_if_missing(True)


class BG3ToolReparsePakMetadata(BG3ToolPlugin):
    icon_name = "refresh"
    sub_name = "Reparse Pak Metadata"
    desc = "Forget what was read from the mods' paks and read them all again now."

    def display(self):
        game = _bg3_game(self._organizer)
        if game is None:
            return
        launch = game.launch()
        launch.cache.clear()
        if launch._read(launch._active(), launch._divine(), "Reading the mods' paks") is not None:
            launch.cache.save()
            game.invalidate_problems()


class BG3ToolConvertJsonsToYaml(BG3ToolPlugin):
    icon_name = "exchange"
    sub_name = "Convert JSONS to YAML"
    desc = "Convert all jsons in active mods to yaml immediately."

    def display(self):
        game = _bg3_game(self._organizer)
        if game is None:
            return
        log_info("converting all json files to yaml")
        mods = game.active_mods()
        progress = game.progress_window("Converting all json files to yaml", len(mods) + 1)
        progress.show()
        # the window closes however this ends
        try:
            for done, mod in enumerate(mods, 1):
                convert_jsons_in_dir_to_yaml(Path(mod.absolutePath()))
                progress.setValue(done)
                game.process_events()
                if progress.wasCanceled():
                    log_warning("conversion canceled by user")
                    return
            convert_jsons_in_dir_to_yaml(game.overwrite_path)
            progress.setValue(len(mods) + 1)
        finally:
            progress.close()


class BG3ToolLoadOrder(BG3ToolPlugin):
    icon_name = "refresh"
    sub_name = "Load Order"
    desc = (
        "The load order the game will get, with each mod's problems: missing "
        "or late dependencies, the Script Extender, conflicts."
    )

    def display(self):
        game = _bg3_game(self._organizer)
        if game is None:
            return
        from .order_view import LoadOrderDialog

        LoadOrderDialog(game, self._organizer, getattr(self, "_parent", None)).exec()


def convert_jsons_in_dir_to_yaml(path: Path):
    # written by the plugin's own YAML writer: MO2's Python has no PyYAML.
    # A file that can't be read is skipped and named in the log.
    from .. import mini_yaml

    for file in list(path.rglob("*.json")):
        converted_path = file.with_suffix(".yaml")
        try:
            if not converted_path.exists() or os.path.getmtime(file) > os.path.getmtime(
                converted_path
            ):
                with open(file, "r", encoding="utf-8-sig") as json_file:
                    data = json.load(json_file)
                text = mini_yaml.dumps(data)
                with open(converted_path, "w", encoding="utf-8") as yaml_file:
                    yaml_file.write(text)
                log_info(f"Converted {file} to YAML")
        except OSError as e:
            log_warning(f"Error accessing file {converted_path}: {e}")
        except Exception as e:  # noqa: BLE001 - this file is skipped, the rest go on
            log_warning(f"Couldn't convert {file} to YAML: {e}")
