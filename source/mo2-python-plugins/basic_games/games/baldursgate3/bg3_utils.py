import functools
import threading
import shutil
import typing
from pathlib import Path
from time import sleep

from PyQt6.QtCore import (
    QCoreApplication,
    QDir,
    QEventLoop,
    QRunnable,
    Qt,
    QThread,
    QThreadPool,
    qInfo,
    qWarning,
)
from PyQt6.QtWidgets import QApplication, QMainWindow, QMessageBox, QProgressDialog

import mobase

loose_file_folders = {
    "Public",
    "Mods",
    "Generated",
    "Localization",
    "ScriptExtender",
}


def get_node_string(
    folder: str = "",
    md5: str = "",
    name: str = "",
    publish_handle: str = "0",
    uuid: str = "",
    version64: str = "0",
) -> str:
    return f"""
                        <node id="ModuleShortDesc">
                            <attribute id="Folder" type="LSString" value="{folder}"/>
                            <attribute id="MD5" type="LSString" value="{md5}"/>
                            <attribute id="Name" type="LSString" value="{name}"/>
                            <attribute id="PublishHandle" type="uint64" value="{publish_handle}"/>
                            <attribute id="UUID" type="guid" value="{uuid}"/>
                            <attribute id="Version64" type="int64" value="{version64}"/>
                        </node>"""


class BG3Utils:
    _mod_settings_xml_start = """\
<?xml version="1.0" encoding="UTF-8"?>
<save>
    <version major="4" minor="8" revision="0" build="500"/>
    <region id="ModuleSettings">
        <node id="root">
            <children>
                <node id="Mods">
                    <children>""" + get_node_string(
        folder="GustavX",
        name="GustavX",
        uuid="cb555efe-2d9e-131f-8195-a89329d218ea",
        version64="36028797018963968",
    )
    _mod_settings_xml_end = """
                    </children>
                </node>
            </children>
        </node>
    </region>
</save>"""

    def __init__(self, name: str):
        self.main_window = None
        self._name = name
        from . import lslib_retriever, pak_parser

        self.lslib_retriever = lslib_retriever.LSLibRetriever(self)
        self._pak_parser = pak_parser.BG3PakParser(self)

    def init(self, organizer: mobase.IOrganizer):
        self._organizer = organizer

    @functools.cached_property
    def autobuild_paks(self):
        return bool(self.get_setting("autobuild_paks"))

    @functools.cached_property
    def extract_full_package(self):
        return bool(self.get_setting("extract_full_package"))

    @functools.cached_property
    def remove_extracted_metadata(self):
        return bool(self.get_setting("remove_extracted_metadata"))

    @functools.cached_property
    def force_load_dlls(self):
        return bool(self.get_setting("force_load_dlls"))

    @functools.cached_property
    def log_diff(self):
        return bool(self.get_setting("log_diff"))

    @functools.cached_property
    def convert_yamls_to_json(self):
        return bool(self.get_setting("convert_yamls_to_json"))

    @functools.cached_property
    def log_dir(self):
        return create_dir_if_needed(Path(self._organizer.basePath()) / "logs")

    @functools.cached_property
    def modsettings_backup(self):
        return create_dir_if_needed(self.plugin_data_path / "temp" / "modsettings.lsx")

    # MO2 Revamped: not cached, it follows the profile
    @property
    def modsettings_path(self):
        return create_dir_if_needed(
            Path(self._organizer.profilePath()) / "modsettings.lsx"
        )

    @functools.cached_property
    def plugin_data_path(self) -> Path:
        """Gets the path to the data folder for the current plugin."""
        return create_dir_if_needed(
            Path(self._organizer.pluginDataPath(), self._name).absolute()
        )

    @functools.cached_property
    def tools_dir(self):
        return create_dir_if_needed(self.plugin_data_path / "tools")

    @functools.cached_property
    def overwrite_path(self):
        return create_dir_if_needed(Path(self._organizer.overwritePath()))

    def active_mods(self) -> list[mobase.IModInterface]:
        modlist = self._organizer.modList()
        return [
            modlist.getMod(mod_name)
            for mod_name in filter(
                lambda mod: modlist.state(mod) & mobase.ModState.ACTIVE,
                modlist.allModsByProfilePriority(),
            )
        ]

    def _set_setting(self, key: str, value: mobase.MoVariant):
        self._organizer.setPluginSetting(self._name, key, value)

    def get_setting(self, key: str) -> mobase.MoVariant:
        return self._organizer.pluginSetting(self._name, key)

    def tr(self, trstr: str) -> str:
        return QCoreApplication.translate(self._name, trstr)

    def create_progress_window(
        self, title: str, max_progress: int, msg: str = "", cancelable: bool = True
    ) -> QProgressDialog:
        progress = QProgressDialog(
            self.tr(msg if msg else title),
            self.tr("Cancel") if cancelable else None,
            0,
            max_progress,
            self.main_window,
        )
        progress.setWindowTitle(self.tr(f"BG3 Plugin: {title}"))
        progress.setWindowModality(Qt.WindowModality.ApplicationModal)
        progress.show()
        return progress

    def on_user_interface_initialized(self, window: QMainWindow) -> None:
        self.main_window = window

    def on_settings_changed(
        self,
        plugin_name: str,
        setting: str,
        old: mobase.MoVariant,
        new: mobase.MoVariant,
    ) -> None:
        if self._name != plugin_name:
            return
        if setting in {
            "extract_full_package",
            "autobuild_paks",
            "remove_extracted_metadata",
            "force_load_dlls",
            "log_diff",
            "convert_yamls_to_json",
        } and hasattr(self, setting):
            delattr(self, setting)

    def construct_modsettings_xml(
        self,
        exec_path: str = "",
        working_dir: typing.Optional[QDir] = None,
        args: str = "",
        force_reparse_metadata: bool = False,
    ) -> bool:
        if (
            "bin/bg3" not in exec_path
            or not self.lslib_retriever.download_lslib_if_missing()
        ):
            return True
        active_mods = self.active_mods()
        progress = self.create_progress_window(
            "Generating modsettings.xml", len(active_mods)
        )
        threadpool = QThreadPool.globalInstance()
        if threadpool is None:
            return False
        metadata: dict[str, str] = {}
        failed: list[str] = []
        lock = threading.Lock()

        # MO2 Revamped: a pak that can't be read is counted as done (and
        # named afterwards) instead of keeping the wait going until it times out
        def retrieve_mod_metadata_in_new_thread(mod: mobase.IModInterface):
            def run():
                try:
                    result = self._pak_parser.get_metadata_for_files_in_mod(
                        mod, force_reparse_metadata
                    )
                    with lock:
                        metadata.update(result)
                except Exception as e:  # noqa: BLE001
                    qWarning(f"couldn't read the paks of {mod.name()}: {e}")
                    with lock:
                        failed.append(mod.name())
                        metadata[mod.name()] = ""

            return run

        for mod in active_mods:
            if progress.wasCanceled():
                qWarning("processing canceled by user")
                return False
            threadpool.start(QRunnable.create(retrieve_mod_metadata_in_new_thread(mod)))
        count = 0
        num_active_mods = len(active_mods)
        # big paks take a while on their first parse: up to 3 s each, 30 s at
        # least (the progress window can still be cancelled)
        total_intervals_to_wait = (num_active_mods * 30) + 300
        def done_count() -> int:
            with lock:
                return len(metadata)

        while done_count() < num_active_mods:
            progress.setValue(done_count())
            QApplication.processEvents(QEventLoop.ProcessEventsFlag.AllEvents, 100)
            count += 1
            if count == total_intervals_to_wait or progress.wasCanceled():
                with lock:
                    remaining_mods = {mod.name() for mod in active_mods} - set(metadata)
                qWarning(f"processing did not finish in time for: {remaining_mods}")
                progress.close()
                break
            QThread.msleep(100)
        canceled = progress.wasCanceled()
        progress.setValue(num_active_mods)
        QApplication.processEvents(QEventLoop.ProcessEventsFlag.AllEvents, 100)
        progress.close()
        # what the threads finished so far; any still running don't change it
        with lock:
            finished = dict(metadata)
            failed_now = list(failed)
        left_out = sorted(
            set(failed_now) | ({mod.name() for mod in active_mods} - set(finished))
        )
        if left_out and not canceled:
            QMessageBox.warning(
                self.main_window,
                QApplication.translate("BG3Utils", "Some mods aren't in the load order"),
                QApplication.translate(
                    "BG3Utils",
                    "These mods couldn't be read in time, so the game won't load them "
                    "this time:\n\n{0}\n\nStart the game again; if a mod stays out, "
                    "reinstall it.",
                ).format("\n".join(left_out)),
            )
        qInfo(f"writing mod load order to {self.modsettings_path}")
        self.modsettings_path.parent.mkdir(parents=True, exist_ok=True)
        self.modsettings_path.write_text(
            (
                self._mod_settings_xml_start
                + "".join(
                    finished[mod.name()]
                    for mod in active_mods
                    if mod.name() in finished
                )
                + self._mod_settings_xml_end
            ),
            encoding="utf-8",
        )
        qInfo(
            f"backing up generated file {self.modsettings_path} to {self.modsettings_backup}, "
            f"check the backup after the executable runs for differences with the file used by the game if you encounter issues"
        )
        self.modsettings_backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy(self.modsettings_path, self.modsettings_backup)
        sleep(0.5)
        return True

    def on_mod_installed(self, mod: mobase.IModInterface) -> None:
        if self.lslib_retriever.download_lslib_if_missing():
            self._pak_parser.get_metadata_for_files_in_mod(mod, True)


def create_dir_if_needed(path: Path) -> Path:
    if "." not in path.name[1:]:
        path.mkdir(parents=True, exist_ok=True)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
    return path
