import json
import os
from pathlib import Path

from PyQt6.QtCore import qInfo, qWarning
from PyQt6.QtWidgets import QApplication

from .. import mini_yaml
from .bg3_tool_plugin import BG3ToolPlugin
from .icons import exchange


class BG3ToolConvertJsonsToYaml(BG3ToolPlugin):
    icon_bytes = exchange
    sub_name = "Convert JSONS to YAML"
    desc = "Convert all jsons in active mods to yaml immediately."

    def display(self):
        from ...game_baldursgate3 import BG3Game

        game_plugin = self._organizer.managedGame()
        if not isinstance(game_plugin, BG3Game):
            return
        utils = game_plugin.utils
        qInfo("converting all json files to yaml")
        active_mods = utils.active_mods()
        progress = utils.create_progress_window(
            "Converting all json files to yaml", len(active_mods) + 1
        )
        # MO2 Revamped: the window closes however this ends (an error left it
        # at 98 %, a cancel left it open)
        try:
            for mod in active_mods:
                _convert_jsons_in_dir_to_yaml(Path(mod.absolutePath()))
                progress.setValue(progress.value() + 1)
                QApplication.processEvents()
                if progress.wasCanceled():
                    qWarning("conversion canceled by user")
                    return
            _convert_jsons_in_dir_to_yaml(utils.overwrite_path)
            progress.setValue(len(active_mods) + 1)
            QApplication.processEvents()
        finally:
            progress.close()


def _convert_jsons_in_dir_to_yaml(path: Path):
    # MO2 Revamped: written by the plugin's own YAML writer (mini_yaml); MO2's
    # Python has no PyYAML, which stopped the tool at its first file. A file
    # that can't be read is skipped and named in the log, the rest go on.
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
                qInfo(f"Converted {file} to YAML")
        except OSError as e:
            qWarning(f"Error accessing file {converted_path}: {e}")
        except Exception as e:  # noqa: BLE001
            # MO2 Revamped: not valid JSON, or nested too deep (RecursionError):
            # this file is skipped, the other files and mods still convert
            qWarning(f"Couldn't convert {file} to YAML: {e}")
