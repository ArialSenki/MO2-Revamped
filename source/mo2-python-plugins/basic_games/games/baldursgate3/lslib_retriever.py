import json
import os
import shutil
import traceback
import urllib.request
import zipfile
from functools import cached_property
from pathlib import Path

from PyQt6.QtCore import qDebug, qWarning
from PyQt6.QtWidgets import QApplication, QMessageBox

from . import bg3_utils


class LSLibRetriever:
    _dotnet_warned = False

    def __init__(self, utils: bg3_utils.BG3Utils):
        self._utils = utils

    @cached_property
    def _needed_lslib_files(self):
        return {
            self._utils.tools_dir / x
            for x in {
                "CommandLineArgumentsParser.dll",
                "Divine.dll",
                "Divine.dll.config",
                "Divine.exe",
                "Divine.runtimeconfig.json",
            }
        }

    def _has_dotnet_8(self) -> bool:
        # Divine.exe only runs on the .NET 8 runtime
        roots = (
            os.environ.get("DOTNET_ROOT"),
            os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "dotnet"),
        )
        return any(
            any(Path(root, "shared", "Microsoft.NETCore.App").glob("8.*"))
            for root in roots
            if root
        )

    def _warn_missing_dotnet(self) -> None:
        if LSLibRetriever._dotnet_warned:
            return
        LSLibRetriever._dotnet_warned = True
        msg_box = QMessageBox(self._utils.main_window)
        msg_box.setIcon(QMessageBox.Icon.Warning)
        msg_box.setWindowTitle(
            self._utils.tr("Baldur's Gate 3 Plugin - Missing dependencies")
        )
        msg_box.setText(
            self._utils.tr(
                "LSLib needs the .NET 8 Runtime (x64), which is not installed.<br>"
                "The game will start, but your mods won't be in its load order.<br><br>"
                '<a href="https://dotnet.microsoft.com/download/dotnet/8.0">'
                "Download .NET 8</a>"
            )
        )
        msg_box.exec()

    def download_lslib_if_missing(self, force: bool = False) -> bool:
        if not self._has_dotnet_8():
            self._warn_missing_dotnet()
            return False
        if not force and all(x.exists() for x in self._needed_lslib_files):
            return True
        try:
            self._utils.tools_dir.mkdir(exist_ok=True, parents=True)
            downloaded = False

            def reporthook(block_num: int, block_size: int, total_size: int) -> None:
                if total_size > 0:
                    progress.setValue(
                        min(int(block_num * block_size * 100 / total_size), 100)
                    )
                    QApplication.processEvents()

            with urllib.request.urlopen(
                "https://api.github.com/repos/Norbyte/lslib/releases/latest"
            ) as response:
                assets = json.loads(response.read().decode("utf-8"))["assets"][0]
                zip_path = self._utils.tools_dir / assets["name"]
                if not zip_path.exists():
                    old_archives = list(self._utils.tools_dir.glob("*.zip"))
                    msg_box = QMessageBox(self._utils.main_window)
                    msg_box.setWindowTitle(
                        self._utils.tr("Baldur's Gate 3 Plugin - Missing dependencies")
                    )
                    if old_archives:
                        msg_box.setText(self._utils.tr("LSLib update available."))
                    else:
                        msg_box.setText(
                            self._utils.tr(
                                "LSLib tools are missing.\nThese are necessary for the plugin to create the load order file for BG3."
                            )
                        )
                    msg_box.addButton(
                        self._utils.tr("Download"),
                        QMessageBox.ButtonRole.DestructiveRole,
                    )
                    exit_btn = msg_box.addButton(
                        self._utils.tr("Exit"), QMessageBox.ButtonRole.ActionRole
                    )
                    msg_box.setIcon(QMessageBox.Icon.Warning)
                    msg_box.exec()

                    if msg_box.clickedButton() == exit_btn:
                        if not old_archives:
                            err = QMessageBox(self._utils.main_window)
                            err.setIcon(QMessageBox.Icon.Critical)
                            err.setText(
                                "LSLib tools are required for the proper generation of the modsettings.xml file, file will not be generated"
                            )
                            err.exec()
                            return False
                    else:
                        progress = self._utils.create_progress_window(
                            "Downloading LSLib", 100, cancelable=False
                        )
                        urllib.request.urlretrieve(
                            assets["browser_download_url"], str(zip_path), reporthook
                        )
                        progress.close()
                        downloaded = True
                        for archive in old_archives:
                            archive.unlink()
                        old_archives = []
                else:
                    old_archives = []
                    new_msg = QMessageBox(self._utils.main_window)
                    new_msg.setIcon(QMessageBox.Icon.Information)
                    new_msg.setText(
                        self._utils.tr("Latest version of LSLib already downloaded!")
                    )
                    new_msg.exec()

        except Exception as e:
            qDebug(f"Download failed: {e}")
            err = QMessageBox(self._utils.main_window)
            err.setIcon(QMessageBox.Icon.Critical)
            err.setText(
                self._utils.tr(
                    f"Failed to download LSLib tools:\n{traceback.format_exc()}"
                )
            )
            err.exec()
            return False
        try:
            if old_archives:
                zip_path = sorted(old_archives)[-1]
            if old_archives or not downloaded:
                dialog_message = "Ensuring all necessary LSLib files have been extracted from archive..."
                win_title = "Verifying LSLib files"
            else:
                dialog_message = "Extracting/Updating LSLib files..."
                win_title = "Extracting LSLib"
            with zipfile.ZipFile(zip_path, "r") as zip_ref:
                names = list(
                    filter(
                        lambda name: name.startswith("Packed/Tools"), zip_ref.namelist()
                    )
                )
                qDebug(f"found files: {','.join(names)}")
                x_progress = self._utils.create_progress_window(
                    win_title, len(names), msg=dialog_message
                )
                for file in names:
                    shutil.move(
                        zip_ref.extract(file, self._utils.tools_dir),
                        self._utils.tools_dir,
                    )
                    x_progress.setValue(x_progress.value() + 1)
                    QApplication.processEvents()
                    if x_progress.wasCanceled():
                        x_progress.close()
                        qWarning("processing canceled by user")
                        return False
                x_progress.close()
                shutil.rmtree(self._utils.tools_dir / "Packed", ignore_errors=True)
        except Exception as e:
            qDebug(f"Extraction failed: {e}")
            err = QMessageBox(self._utils.main_window)
            err.setIcon(QMessageBox.Icon.Critical)
            err.setText(
                self._utils.tr(
                    f"Failed to extract LSLib tools:\n{traceback.format_exc()}"
                )
            )
            err.exec()
            return False
        return True
