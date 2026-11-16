import functools
import html
import os
import threading
import time
import shutil
import typing
from pathlib import Path
from xml.sax.saxutils import escape

from PyQt6.QtCore import (
    QCoreApplication,
    QDir,
    QEventLoop,
    QRunnable,
    Qt,
    QThreadPool,
    qInfo,
    qWarning,
)
from PyQt6.QtWidgets import (
    QApplication,
    QLabel,
    QMainWindow,
    QMessageBox,
    QProgressDialog,
)

import mobase

from ...basic_features.utils import root_builder_enabled

loose_file_folders = {
    "Public",
    "Mods",
    "Generated",
    "Localization",
    "ScriptExtender",
}

# MO2 Revamped: game folders that Wabbajack lists (made with the unofficial BG3
# plugin, whose data folder is Data/) leave at a mod's top level; they're mapped
# to <game>/Data at run time (see BG3FileMapper)
top_level_data_folders = loose_file_folders | {"Video"}

# MO2 Revamped: Script Extender's per-mod config folder of the unofficial BG3
# plugin (SE_CONFIG/<mod>/... -> <documents>/Script Extender/<mod>/...) and its
# folder for the paks (PAK_FILES/*.pak -> <documents>/Mods)
se_config_folder = "SE_CONFIG"
pak_files_folder = "PAK_FILES"

# MO2 Revamped: BG3 Mod Manager is a .NET 8 desktop program
_bg3mm_binary = "bg3modmanager.exe"
_dotnet_download_url = "https://dotnet.microsoft.com/download/dotnet/{0}.0"


def get_node_string(
    folder: str = "",
    md5: str = "",
    name: str = "",
    publish_handle: str = "0",
    uuid: str = "",
    version64: str = "0",
) -> str:
    # MO2 Revamped: the values as XML wants them ("Tattoo & Makeup" broke the
    # file the game reads)
    folder, md5, name, publish_handle, uuid, version64 = (
        escape(str(v), {'"': "&quot;"})
        for v in (folder, md5, name, publish_handle, uuid, version64)
    )
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
            not runs_the_game(exec_path)
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
        # MO2 Revamped: the paks found now are the file mapper's for this start
        keep_pak_scans(True)
        metadata: dict[str, str] = {}
        failed: list[str] = []
        lock = threading.Lock()

        parser = self._pak_parser
        from . import pak_parser as pak_parser_module

        def failed_to_read(mod: mobase.IModInterface, e: Exception) -> None:
            qWarning(f"couldn't read the paks of {mod.name()}: {e}")
            with lock:
                failed.append(mod.name())
                metadata[mod.name()] = ""

        # MO2 Revamped: a pak that can't be read is counted as done (and
        # named afterwards) instead of keeping the wait going until it times out
        def retrieve_mod_metadata_in_new_thread(mod, files, config):
            def run():
                try:
                    # meta.ini read again now: the thread may start a while
                    # later, after MO2 saved it (a Nexus check)
                    fresh = pak_parser_module.read_meta_ini(
                        Path(mod.absolutePath()) / "meta.ini"
                    )
                    result = parser.metadata_from(
                        mod, files, fresh, force_reparse_metadata
                    )
                    with lock:
                        metadata[mod.name()] = result
                except Exception as e:  # noqa: BLE001
                    failed_to_read(mod, e)

            return run

        # MO2 Revamped: the mods whose paks this plugin read before are read
        # right here (meta.ini only); only those that need Divine go to the
        # worker threads, which a start with everything known doesn't use
        num_active_mods = len(active_mods)
        next_paint = 0.0
        for index, mod in enumerate(active_mods):
            if time.monotonic() >= next_paint:
                progress.setValue(index)
                QApplication.processEvents()
                next_paint = time.monotonic() + 0.05
            if progress.wasCanceled():
                qWarning("processing canceled by user")
                keep_pak_scans(False)
                forget_pak_scans()
                return False
            try:
                files, config = parser.prepare(mod)
                if parser.needs_divine(files, config, force_reparse_metadata):
                    threadpool.start(
                        QRunnable.create(
                            retrieve_mod_metadata_in_new_thread(mod, files, config)
                        )
                    )
                    continue
                result = parser.metadata_from(mod, files, config, force_reparse_metadata)
                with lock:
                    metadata[mod.name()] = result
            except Exception as e:  # noqa: BLE001
                failed_to_read(mod, e)
        # big paks take a while on their first parse: 6 s each, 60 s at least
        # (the progress window can still be cancelled)
        deadline = time.monotonic() + num_active_mods * 6 + 60

        def done_count() -> int:
            with lock:
                return len(metadata)

        # MO2 Revamped: short waits that end as soon as the threads do
        # (waitForDone lets them run, it releases the GIL), the window still
        # painting meanwhile
        while done_count() < num_active_mods:
            progress.setValue(done_count())
            QApplication.processEvents(QEventLoop.ProcessEventsFlag.AllEvents, 10)
            if time.monotonic() > deadline or progress.wasCanceled():
                with lock:
                    remaining_mods = {mod.name() for mod in active_mods} - set(metadata)
                qWarning(f"processing did not finish in time for: {remaining_mods}")
                progress.close()
                break
            threadpool.waitForDone(25)
        # the scans are the mapper's from here on; nothing else adds to them
        keep_pak_scans(False)
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
        # MO2 Revamped: no fixed half-second wait here; the file is written
        # and closed already
        return True

    def root_builder_enabled(self) -> bool:
        return root_builder_enabled(self._organizer)

    def check_dotnet_before_run(
        self,
        exec_path: str = "",
        working_dir: typing.Optional[QDir] = None,
        args: str = "",
    ) -> bool:
        """MO2 Revamped: BG3 Mod Manager doesn't start without the .NET Desktop
        Runtime its runtimeconfig.json names; say so before it runs, with the
        link to get it. A self-contained build has no such file and needs
        nothing."""
        if Path(exec_path).name.casefold() != _bg3mm_binary:
            return True
        major = missing_dotnet_major(Path(exec_path))
        if major is None:
            return True
        qWarning(f".NET {major} Desktop Runtime not found, BG3 Mod Manager needs it")
        url = _dotnet_download_url.format(major)
        box = QMessageBox(self.main_window)
        box.setIcon(QMessageBox.Icon.Warning)
        box.setWindowTitle(
            QApplication.translate("BG3Utils", "BG3 Mod Manager needs .NET {0}").replace(
                "{0}", str(major)
            )
        )
        text = QApplication.translate(
            "BG3Utils",
            "BG3 Mod Manager needs the .NET {1} Desktop Runtime, which isn't "
            "installed on this PC, so it will close with an error as it starts."
            "\n\nInstall the .NET Desktop Runtime {1} for Windows x64 from {0}, then "
            "run BG3 Mod Manager again.",
        )
        link = f'<a href="{url}">{url}</a>'
        box.setTextFormat(Qt.TextFormat.RichText)
        box.setText(
            html.escape(text)
            .replace("\n", "<br>")
            .replace("{1}", str(major))
            .replace("{0}", link)
        )
        box.setTextInteractionFlags(Qt.TextInteractionFlag.TextBrowserInteraction)
        run_anyway = box.addButton(
            QApplication.translate("BG3Utils", "Start anyway"),
            QMessageBox.ButtonRole.AcceptRole,
        )
        cancel = box.addButton(QMessageBox.StandardButton.Cancel)
        box.setDefaultButton(cancel)
        box.setEscapeButton(cancel)
        # QLabel opens the link itself with TextBrowserInteraction only when
        # openExternalLinks is on
        for label in box.findChildren(QLabel):
            label.setOpenExternalLinks(True)
        box.exec()
        return box.clickedButton() is run_anyway

    def on_mod_installed(self, mod: mobase.IModInterface) -> None:
        # MO2 Revamped: a pak that can't be read doesn't make the installation
        # look failed; the next start reads it again
        try:
            if self.lslib_retriever.download_lslib_if_missing():
                self._pak_parser.get_metadata_for_files_in_mod(mod, True)
        except Exception as e:  # noqa: BLE001
            qWarning(f"couldn't read the paks of the new mod {mod.name()}: {e}")


# MO2 Revamped: the programs that start the game with the mods' load order;
# the Larian Launcher starts bin/bg3*.exe itself
_game_binaries = {"bg3.exe", "bg3_dx11.exe", "larilauncher.exe"}


# MO2 Revamped: a mod's paks as the load order found them just before the
# program starts, kept for the file mapper of the same start (one walk of each
# mod's folder instead of two); see scan_mod_paks and mod_paks
_pak_scans: dict[str, tuple[float, list[Path]]] = {}
_pak_scan_lifetime = 15.0
_keeping_pak_scans = False


def keep_pak_scans(keep: bool) -> None:
    """MO2 Revamped: scans are kept only while the load order is made for a
    start, and dropped when that starts again or the mapper is done."""
    global _keeping_pak_scans
    _keeping_pak_scans = keep
    if keep:
        _pak_scans.clear()


def forget_pak_scans() -> None:
    _pak_scans.clear()


def scan_mod_paks(modpath: Path) -> list[Path]:
    """MO2 Revamped: a mod's paks, wherever they are in it, read from disk now.
    They all go to the documents' Mods folder by name, so of two with one name
    (two FOMOD options' folders) only the last in path order is the mod's, for
    the file mapper and the load order alike."""
    by_name: dict[str, Path] = {}
    found: list[Path] = []
    for root, _, files in os.walk(modpath):
        found.extend(Path(root, f) for f in files if f.casefold().endswith(".pak"))
    for pak in sorted(found):
        by_name[pak.name.casefold()] = pak
    paks = sorted(by_name.values())
    if _keeping_pak_scans:
        _pak_scans[str(modpath)] = (time.monotonic(), paks)
    return paks


def mod_paks(modpath: Path) -> list[Path]:
    """MO2 Revamped: the paks the load order read for this start (taken once),
    or read from disk."""
    scan = _pak_scans.pop(str(modpath), None)
    if scan is not None and time.monotonic() - scan[0] < _pak_scan_lifetime:
        return scan[1]
    return scan_mod_paks(modpath)


def runs_the_game(exec_path: str) -> bool:
    return Path(exec_path).name.casefold() in _game_binaries


def _dotnet_roots() -> list[Path]:
    roots = [
        Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "dotnet",
    ]
    if os.environ.get("DOTNET_ROOT"):
        roots.insert(0, Path(os.environ["DOTNET_ROOT"]))
    return roots


def _installed_majors(framework: str) -> set[int]:
    majors: set[int] = set()
    for root in _dotnet_roots():
        try:
            for d in (root / "shared" / framework).iterdir():
                head = d.name.split(".", 1)[0]
                if d.is_dir() and head.isdigit():
                    majors.add(int(head))
        except OSError:
            continue
    return majors


def missing_dotnet_major(exe: Path) -> typing.Optional[int]:
    """MO2 Revamped: the major version of a .NET framework that the program's
    runtimeconfig.json asks for and this PC doesn't have, or None (all there,
    or a self-contained build). With rollForward Major/LatestMajor a newer
    major does too."""
    import json

    config = exe.with_name(exe.stem + ".runtimeconfig.json")
    if not config.is_file():
        # a self-contained build carries the runtime next to it; a single-file
        # build has its runtimeconfig inside the exe: the .NET 8 it always used
        if any(exe.with_name(dll).is_file() for dll in ("hostfxr.dll", "coreclr.dll")):
            return None
        return None if 8 in _installed_majors("Microsoft.WindowsDesktop.App") else 8
    try:
        options = json.loads(config.read_text(encoding="utf-8-sig"))["runtimeOptions"]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    frameworks = options.get("frameworks") or [options.get("framework") or {}]
    roll = str(options.get("rollForward", "")).casefold()
    for fw in frameworks:
        name = fw.get("name", "") if isinstance(fw, dict) else ""
        head = str(fw.get("version", "")).split(".", 1)[0] if name else ""
        if not head.isdigit():
            continue
        want = int(head)
        have = _installed_majors(name)
        ok = (
            any(m >= want for m in have)
            if roll in ("major", "latestmajor")
            else want in have
        )
        if not ok:
            return want
    return None


def create_dir_if_needed(path: Path) -> Path:
    if "." not in path.name[1:]:
        path.mkdir(parents=True, exist_ok=True)
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
    return path
