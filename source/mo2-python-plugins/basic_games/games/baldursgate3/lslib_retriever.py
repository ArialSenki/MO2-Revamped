import hashlib
import json
import os
import re
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

    _api_url = "https://api.github.com/repos/Norbyte/lslib/releases/latest"
    _asset_re = re.compile(r"ExportTool-v[0-9][0-9.]*\.zip", re.IGNORECASE)
    _timeout = 20

    def _error(self, text: str) -> None:
        err = QMessageBox(self._utils.main_window)
        err.setIcon(QMessageBox.Icon.Critical)
        err.setWindowTitle(self._utils.tr("Baldur's Gate 3 Plugin"))
        err.setText(text)
        err.exec()

    def _ask(self, text: str) -> bool:
        msg_box = QMessageBox(self._utils.main_window)
        msg_box.setWindowTitle(
            self._utils.tr("Baldur's Gate 3 Plugin - Missing dependencies")
        )
        msg_box.setIcon(QMessageBox.Icon.Warning)
        msg_box.setText(text)
        download = msg_box.addButton(
            self._utils.tr("Download"), QMessageBox.ButtonRole.AcceptRole
        )
        msg_box.addButton(self._utils.tr("Exit"), QMessageBox.ButtonRole.RejectRole)
        msg_box.exec()
        return msg_box.clickedButton() is download

    def _local_archives(self) -> list[Path]:
        return sorted(
            p
            for p in self._utils.tools_dir.glob("*.zip")
            if self._asset_re.fullmatch(p.name)
        )

    def _has_all_tools(self) -> bool:
        return all(x.is_file() for x in self._needed_lslib_files)

    def download_lslib_if_missing(self, force: bool = False) -> bool:
        if not self._has_dotnet_8():
            self._warn_missing_dotnet()
            return False
        if not force and self._has_all_tools():
            return True
        self._utils.tools_dir.mkdir(exist_ok=True, parents=True)
        local = self._local_archives()
        # MO2 Revamped: files missing but the archive is here: unpacked again
        # without asking GitHub anything
        if not force and local:
            try:
                if self._install_tools(local[-1], True) and self._has_all_tools():
                    return True
            except Exception:  # noqa: BLE001
                qWarning(
                    f"LSLib archive {local[-1].name} unusable: {traceback.format_exc()}"
                )
        # MO2 Revamped: nothing is sent to GitHub before the user agrees (a
        # check the user started is that agreement)
        if not force and not self._ask(
            self._utils.tr(
                "LSLib tools are missing.\nThese are necessary for the plugin to "
                "create the load order file for BG3.\n\nDownload them from "
                "GitHub (github.com/Norbyte/lslib)?"
            )
        ):
            self._error(
                self._utils.tr(
                    "LSLib tools are required for the proper generation of the "
                    "modsettings.xml file, file will not be generated"
                )
            )
            return False
        try:
            asset = self._latest_asset()
        except Exception as e:  # noqa: BLE001
            qDebug(f"LSLib release lookup failed: {e}")
            self._error(
                self._utils.tr("Couldn't look up the latest LSLib release:\n{0}").format(e)
            )
            return False
        zip_path = self._utils.tools_dir / asset["name"]
        downloaded = False
        if zip_path.exists():
            if force:
                info = QMessageBox(self._utils.main_window)
                info.setIcon(QMessageBox.Icon.Information)
                info.setText(self._utils.tr("Latest version of LSLib already downloaded!"))
                info.exec()
        else:
            # an update the user checked for: asked once more; a first
            # download was agreed to above
            if force and (local or self._has_all_tools()):
                if not self._ask(self._utils.tr("LSLib update available.")):
                    return self._has_all_tools()
            try:
                if not self._download(asset, zip_path):
                    return False
            except Exception as e:  # noqa: BLE001
                qDebug(f"Download failed: {e}")
                self._error(
                    self._utils.tr("Failed to download LSLib tools:\n{0}").format(e)
                )
                return False
            downloaded = True
        try:
            if not self._install_tools(zip_path, not downloaded):
                return False
        except Exception:  # noqa: BLE001
            qDebug(f"Extraction failed: {traceback.format_exc()}")
            self._error(
                self._utils.tr("Failed to extract LSLib tools:\n{0}").format(
                    traceback.format_exc()
                )
            )
            return False
        if not self._has_all_tools():
            # MO2 Revamped: success only with Divine and its files in place
            missing = sorted(
                x.name for x in self._needed_lslib_files if not x.is_file()
            )
            self._error(
                self._utils.tr(
                    "{0} doesn't hold the LSLib tools this plugin needs "
                    "(missing: {1})."
                ).format(zip_path.name, ", ".join(missing))
            )
            return False
        if downloaded:
            for archive in self._local_archives():
                if archive != zip_path:
                    archive.unlink(missing_ok=True)
        return True

    def _latest_asset(self) -> dict:
        """MO2 Revamped: the release's ExportTool zip, found by name (the first
        asset isn't always it), with a timeout: a network that accepts and
        never answers froze MO2 before the game started."""
        request = urllib.request.Request(
            self._api_url, headers={"Accept": "application/vnd.github+json"}
        )
        with urllib.request.urlopen(request, timeout=self._timeout) as response:
            release = json.loads(response.read().decode("utf-8"))
        for asset in release.get("assets") or []:
            name = str(asset.get("name", ""))
            if (
                self._asset_re.fullmatch(name)
                and Path(name).name == name
                and asset.get("browser_download_url")
            ):
                return asset
        raise ValueError(f"no ExportTool zip in release {release.get('tag_name', '?')}")

    def _download(self, asset: dict, zip_path: Path) -> bool:
        """MO2 Revamped: in chunks, under another name first, with a cancel
        button and a timeout per read; the SHA-256 GitHub gives for the asset
        is checked when there is one."""
        part_path = zip_path.with_name(zip_path.name + ".part")
        progress = self._utils.create_progress_window(
            "Downloading LSLib", 100, cancelable=True
        )
        digest = str(asset.get("digest") or "")
        sha = hashlib.sha256()
        try:
            with urllib.request.urlopen(
                asset["browser_download_url"], timeout=self._timeout
            ) as response, open(part_path, "wb") as out:
                total = int(response.headers.get("Content-Length") or 0)
                done = 0
                while True:
                    chunk = response.read(1 << 16)
                    if not chunk:
                        break
                    out.write(chunk)
                    sha.update(chunk)
                    done += len(chunk)
                    if total > 0:
                        progress.setValue(min(done * 100 // total, 100))
                    QApplication.processEvents()
                    if progress.wasCanceled():
                        qWarning("LSLib download canceled by user")
                        return False
            if total and done != total:
                raise OSError(f"download cut at {done} of {total} bytes")
            if digest.lower().startswith("sha256:") and (
                sha.hexdigest() != digest.split(":", 1)[1].strip().lower()
            ):
                raise OSError("the downloaded file doesn't match GitHub's SHA-256")
            os.replace(part_path, zip_path)
            return True
        finally:
            progress.close()
            part_path.unlink(missing_ok=True)

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
                rel = _safe_member(info.filename[len(prefix) :], tools_dir, staging)
                if rel is None:
                    qWarning(f"LSLib archive entry skipped: {info.filename}")
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


def _safe_member(name: str, *roots: Path) -> Path | None:
    """MO2 Revamped: a zip entry's path below the tools folder, or None.
    "Packed/Tools//Windows/x.dll", a backslash path or "C:x.dll" don't count
    as absolute for pathlib on Windows and were written outside the folder."""
    parts = name.replace("\\", "/").split("/")
    if not parts or any(p in ("", ".", "..") or ":" in p for p in parts):
        return None
    rel = Path(*parts)
    if rel.is_absolute() or rel.drive or rel.root:
        return None
    for root in roots:
        base = root.resolve()
        if not (base / rel).resolve().is_relative_to(base):
            return None
    return rel
