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
        # MO2 Revamped: the runtime Divine's own runtimeconfig.json names, once
        # it's downloaded (a newer LSLib may want a newer .NET)
        divine = self._utils.tools_dir / "Divine.exe"
        if divine.with_name("Divine.runtimeconfig.json").is_file():
            return bg3_utils.missing_dotnet_major(divine) is None
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
                        # MO2 Revamped: downloaded under another name first, so
                        # a download cut halfway isn't taken next time for the
                        # finished archive; the window closes whatever happens
                        part_path = zip_path.with_name(zip_path.name + ".part")
                        try:
                            urllib.request.urlretrieve(
                                assets["browser_download_url"],
                                str(part_path),
                                reporthook,
                            )
                            os.replace(part_path, zip_path)
                        finally:
                            progress.close()
                            part_path.unlink(missing_ok=True)
                        downloaded = True
                        for archive in old_archives:
                            archive.unlink(missing_ok=True)
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
        if old_archives:
            zip_path = sorted(old_archives)[-1]
        verify_only = bool(old_archives) or not downloaded
        try:
            return self._install_tools(zip_path, verify_only)
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

    def _install_tools(self, zip_path: Path, verify_only: bool) -> bool:
        """MO2 Revamped: the archive's Packed/Tools/ files into the tools folder.

        They were moved one by one onto the files already there, which failed
        on the second extraction ("Destination path ... Tools already exists",
        the archive's own folder entry) and left the progress window open. Now
        the new files are unpacked into a folder of their own first and swapped
        in only once all are there; the old tools stay as they were if that
        fails or is cancelled, and are put back if a swap fails halfway (a
        Divine.exe still running). Verifying skips the files already in place."""
        tools_dir = self._utils.tools_dir
        prefix = "Packed/Tools/"
        if verify_only:
            dialog_message = "Ensuring all necessary LSLib files have been extracted from archive..."
            win_title = "Verifying LSLib files"
        else:
            dialog_message = "Extracting/Updating LSLib files..."
            win_title = "Extracting LSLib"
        staging = tools_dir.with_name(tools_dir.name + ".new")
        backup = tools_dir.with_name(tools_dir.name + ".old")
        with zipfile.ZipFile(zip_path, "r") as zip_ref:
            members: list[tuple[zipfile.ZipInfo, Path]] = []
            for info in zip_ref.infolist():
                if info.is_dir() or not info.filename.startswith(prefix):
                    continue
                rel = Path(info.filename[len(prefix) :])
                if rel.is_absolute() or ".." in rel.parts or not rel.parts:
                    continue
                target = tools_dir / rel
                if verify_only:
                    try:
                        if target.stat().st_size == info.file_size:
                            continue
                    except OSError:
                        pass
                members.append((info, rel))
            qDebug(f"LSLib files to extract: {','.join(i.filename for i, _ in members)}")
            if members:
                shutil.rmtree(staging, ignore_errors=True)
                x_progress = self._utils.create_progress_window(
                    win_title, len(members) + 1, msg=dialog_message
                )
                try:
                    for info, rel in members:
                        dest = staging / rel
                        dest.parent.mkdir(parents=True, exist_ok=True)
                        with zip_ref.open(info) as src, open(dest, "wb") as out:
                            shutil.copyfileobj(src, out, 1 << 20)
                        x_progress.setValue(x_progress.value() + 1)
                        QApplication.processEvents()
                        if x_progress.wasCanceled():
                            qWarning("processing canceled by user")
                            return False
                    self._swap_in(staging, backup, [rel for _, rel in members])
                    x_progress.setValue(len(members) + 1)
                finally:
                    x_progress.close()
                    shutil.rmtree(staging, ignore_errors=True)
        # what earlier versions of this plugin left: the archive's folders
        # (Packed/, and Tools/ when it was moved in whole)
        shutil.rmtree(tools_dir / "Packed", ignore_errors=True)
        stray = tools_dir / "Tools"
        if stray.is_dir() and not any(stray.iterdir()):
            stray.rmdir()
        return True

    def _swap_in(self, staging: Path, backup: Path, files: list[Path]) -> None:
        tools_dir = self._utils.tools_dir
        shutil.rmtree(backup, ignore_errors=True)
        moved: list[Path] = []
        try:
            for rel in files:
                target = tools_dir / rel
                if target.exists():
                    (backup / rel).parent.mkdir(parents=True, exist_ok=True)
                    os.replace(target, backup / rel)
                moved.append(rel)
                target.parent.mkdir(parents=True, exist_ok=True)
                os.replace(staging / rel, target)
        except OSError:
            # the old files back, as they were
            for rel in moved:
                target = tools_dir / rel
                old = backup / rel
                if old.exists():
                    os.replace(old, target)
                else:
                    target.unlink(missing_ok=True)
            raise
        finally:
            shutil.rmtree(backup, ignore_errors=True)
