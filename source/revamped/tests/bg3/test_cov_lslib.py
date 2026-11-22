"""LSLib: finding .NET, the download (with a fake GitHub, no network), the
tools unpacked from the archive, and Divine.exe run through a fake
subprocess. No window is shown: the message boxes are replaced."""

from __future__ import annotations

import hashlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

from PyQt6.QtWidgets import QMessageBox

from basic_games.games.baldursgate3 import dotnet, lslib, paks

TOOLS = ["CommandLineArgumentsParser.dll", "Divine.dll", "Divine.dll.config", "Divine.exe", "Divine.runtimeconfig.json"]


class FakeBox:
    """A QMessageBox that records its text and clicks the button the test says."""

    Icon = QMessageBox.Icon
    ButtonRole = QMessageBox.ButtonRole
    StandardButton = QMessageBox.StandardButton
    shown: list[str] = []
    accept = True

    def __init__(self, parent=None):
        self.text = ""
        self.buttons: list[tuple[object, object]] = []

    def setText(self, text):
        self.text = text

    def addButton(self, label, role=None):
        button = object()
        self.buttons.append((button, role if role is not None else label))
        return button

    def exec(self):
        FakeBox.shown.append(self.text)

    def clickedButton(self):
        for button, role in self.buttons:
            if (role == QMessageBox.ButtonRole.AcceptRole) == FakeBox.accept:
                return button
        return None

    def findChildren(self, cls):
        return []

    def __getattr__(self, name):
        # setIcon, setWindowTitle, setTextFormat...
        return lambda *a, **k: None


class FakeProgress:
    def __init__(self, cancel=False):
        self.cancel = cancel
        self._value = 0
        self.closed = False

    def setValue(self, value):
        self._value = value

    def value(self):
        return self._value

    def wasCanceled(self):
        return self.cancel

    def close(self):
        self.closed = True

    def show(self):
        pass


class Utils:
    def __init__(self, tools_dir: Path):
        self.tools_dir = tools_dir
        self.main_window = None
        self.cancel = False
        self.progress: list[FakeProgress] = []

    def tr(self, text):
        return text

    def create_progress_window(self, title, maximum, msg="", cancelable=True):
        progress = FakeProgress(self.cancel)
        self.progress.append(progress)
        return progress


def export_zip(path: Path, extra: dict[str, bytes] | None = None, tools=TOOLS) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("Packed/Tools/", b"")
        for name in tools:
            z.writestr(f"Packed/Tools/{name}", name.encode())
        z.writestr("Packed/readme.txt", b"not a tool")
        for name, data in (extra or {}).items():
            z.writestr(name, data)
    return path


def runtimeconfig(exe: Path, config) -> None:
    exe.parent.mkdir(parents=True, exist_ok=True)
    exe.write_bytes(b"MZ")
    text = config if isinstance(config, str) else json.dumps(config)
    exe.with_name(exe.stem + ".runtimeconfig.json").write_text(text, encoding="utf-8")


class Tmp(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-lslib-"))
        # a .NET install of the test's own, nothing of the PC's
        self.dotnet = self.tmp / "Program Files" / "dotnet"
        env = mock.patch.dict(os.environ, {"ProgramFiles": str(self.tmp / "Program Files")})
        env.start()
        os.environ.pop("DOTNET_ROOT", None)
        self.addCleanup(env.stop)

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)

    def runtime(self, framework: str, version: str, root: Path | None = None) -> None:
        (root or self.dotnet).joinpath("shared", framework, version).mkdir(parents=True, exist_ok=True)


class DotnetTest(Tmp):
    def test_runtimeconfig_versions_and_roll_forward(self):
        exe = self.tmp / "app" / "BG3ModManager.exe"
        config = {"runtimeOptions": {"framework": {"name": "Microsoft.WindowsDesktop.App", "version": "8.0.0"}}}
        runtimeconfig(exe, config)
        self.assertEqual(dotnet.missing_dotnet_major(exe), 8)
        self.runtime("Microsoft.WindowsDesktop.App", "9.0.1")
        # 9 isn't 8 without rollForward
        self.assertEqual(dotnet.missing_dotnet_major(exe), 8)
        config["runtimeOptions"]["rollForward"] = "LatestMajor"
        runtimeconfig(exe, config)
        self.assertIsNone(dotnet.missing_dotnet_major(exe))
        config["runtimeOptions"]["framework"]["version"] = "10.0.0"
        runtimeconfig(exe, config)
        self.assertEqual(dotnet.missing_dotnet_major(exe), 10)

    def test_frameworks_list_with_bom_and_dotnet_root(self):
        exe = self.tmp / "app" / "Divine.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"MZ")
        config = {"runtimeOptions": {"frameworks": [
            {"name": "Microsoft.NETCore.App", "version": "8.0.0"},
            {"name": "", "version": "x"},
            "not a dict",
            {"name": "Microsoft.WindowsDesktop.App", "version": "8.0.0"},
        ]}}
        exe.with_name("Divine.runtimeconfig.json").write_bytes(b"\xef\xbb\xbf" + json.dumps(config).encode())
        root = self.tmp / "custom-dotnet"
        self.runtime("Microsoft.NETCore.App", "8.0.11", root)
        # a file and a folder that isn't a version are ignored
        (root / "shared" / "Microsoft.NETCore.App" / "9.txt").write_text("x")
        self.runtime("Microsoft.NETCore.App", "preview", root)
        with mock.patch.dict(os.environ, {"DOTNET_ROOT": str(root)}):
            self.assertEqual(dotnet.missing_dotnet_major(exe), 8)
            self.runtime("Microsoft.WindowsDesktop.App", "8.0.11", root)
            self.assertIsNone(dotnet.missing_dotnet_major(exe))

    def test_broken_runtimeconfig_is_not_a_warning(self):
        exe = self.tmp / "app" / "Divine.exe"
        for text in ("{not json", '{"runtimeOptions": 5}', '{"runtimeOptions": {"frameworks": 5}}', "[]", '{"other": 1}'):
            runtimeconfig(exe, text)
            self.assertIsNone(dotnet.missing_dotnet_major(exe), text)
        # not UTF-8 at all
        exe.with_name("Divine.runtimeconfig.json").write_bytes(b"\xff\xfe{\x00")
        self.assertIsNone(dotnet.missing_dotnet_major(exe))

    def test_without_runtimeconfig(self):
        exe = self.tmp / "app" / "BG3ModManager.exe"
        exe.parent.mkdir(parents=True)
        exe.write_bytes(b"MZ")
        # a single-file build: the .NET 8 desktop runtime it always used
        self.assertEqual(dotnet.missing_dotnet_major(exe), 8)
        self.runtime("Microsoft.WindowsDesktop.App", "8.0.1")
        self.assertIsNone(dotnet.missing_dotnet_major(exe))
        shutil.rmtree(self.dotnet)
        # self-contained: its runtime is next to it
        (exe.parent / "hostfxr.dll").write_bytes(b"x")
        self.assertIsNone(dotnet.missing_dotnet_major(exe))


class RetrieverBase(Tmp):
    def setUp(self) -> None:
        super().setUp()
        self.utils = Utils(self.tmp / "plugin" / "tools")
        self.retriever = lslib.LSLibRetriever(self.utils)
        FakeBox.shown = []
        FakeBox.accept = True
        box = mock.patch.object(lslib, "QMessageBox", FakeBox)
        box.start()
        self.addCleanup(box.stop)
        lslib.LSLibRetriever._declined = False
        lslib.LSLibRetriever._dotnet_warned = False
        self.addCleanup(setattr, lslib.LSLibRetriever, "_declined", False)
        self.addCleanup(setattr, lslib.LSLibRetriever, "_dotnet_warned", False)
        self.runtime("Microsoft.NETCore.App", "8.0.0")

    def no_network(self):
        self.retriever._latest_asset = lambda: self.fail("GitHub was asked")
        self.retriever._download = lambda *a: self.fail("a download started")

    def tools_present(self) -> None:
        self.utils.tools_dir.mkdir(parents=True, exist_ok=True)
        for name in TOOLS:
            (self.utils.tools_dir / name).write_bytes(name.encode())


class DotnetCheckTest(RetrieverBase):
    def test_missing_dotnet_warns_once(self):
        shutil.rmtree(self.dotnet)
        self.no_network()
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertEqual(len(FakeBox.shown), 1)
        self.assertIn(".NET 8", FakeBox.shown[0])

    def test_downloaded_divine_says_which_dotnet(self):
        self.tools_present()
        config = {"runtimeOptions": {"framework": {"name": "Microsoft.NETCore.App", "version": "9.0.0"}}}
        (self.utils.tools_dir / "Divine.runtimeconfig.json").write_text(json.dumps(config))
        self.assertFalse(self.retriever._has_dotnet_8())
        self.runtime("Microsoft.NETCore.App", "9.0.2")
        self.assertTrue(self.retriever._has_dotnet_8())


class DownloadFlowTest(RetrieverBase):
    def test_tools_there_need_nothing(self):
        self.tools_present()
        self.no_network()
        self.assertTrue(self.retriever.download_lslib_if_missing())
        self.assertEqual(FakeBox.shown, [])

    def test_archive_here_is_unpacked_without_asking(self):
        export_zip(self.utils.tools_dir / "ExportTool-v1.20.3.zip")
        self.no_network()
        self.assertTrue(self.retriever.download_lslib_if_missing())
        self.assertEqual(sorted(p.name for p in self.utils.tools_dir.iterdir() if p.suffix != ".zip"), sorted(TOOLS))
        self.assertFalse((self.utils.tools_dir / "Packed").exists())
        self.assertEqual(FakeBox.shown, [])

    def test_unusable_archive_then_declined(self):
        self.utils.tools_dir.mkdir(parents=True)
        (self.utils.tools_dir / "ExportTool-v1.0.zip").write_bytes(b"cut download")
        self.no_network()
        FakeBox.accept = False
        self.assertFalse(self.retriever.download_lslib_if_missing(reason="Mod: a.pak"))
        self.assertIn("Mod: a.pak", FakeBox.shown[0])
        # asked once per session
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertEqual(len(FakeBox.shown), 1)

    def test_release_lookup_failure_is_shown(self):
        def fail():
            raise OSError("offline")

        self.retriever._latest_asset = fail
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertIn("offline", FakeBox.shown[-1])

    def test_download_installs_and_drops_older_archives(self):
        self.utils.tools_dir.mkdir(parents=True)
        export_zip(self.utils.tools_dir / "ExportTool-v1.9.zip", tools=TOOLS[:1])
        asset = {"name": "ExportTool-v1.20.zip", "browser_download_url": "https://github.com/x"}
        self.retriever._latest_asset = lambda: asset
        self.retriever._download = lambda a, zip_path: export_zip(zip_path) and True
        self.assertTrue(self.retriever.download_lslib_if_missing())
        self.assertEqual([p.name for p in self.utils.tools_dir.glob("*.zip")], ["ExportTool-v1.20.zip"])
        self.assertTrue((self.utils.tools_dir / "Divine.exe").is_file())

    def test_download_canceled_or_failing(self):
        asset = {"name": "ExportTool-v2.0.zip", "browser_download_url": "https://github.com/x"}
        self.retriever._latest_asset = lambda: asset
        self.retriever._download = lambda a, p: False
        self.assertFalse(self.retriever.download_lslib_if_missing())

        def broken(a, p):
            raise OSError("cut")

        self.retriever._download = broken
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertIn("cut", FakeBox.shown[-1])

    def test_archive_without_the_tools(self):
        asset = {"name": "ExportTool-v2.0.zip", "browser_download_url": "https://github.com/x"}
        self.retriever._latest_asset = lambda: asset
        self.retriever._download = lambda a, p: export_zip(p, tools=["Divine.exe"]) and True
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertIn("missing:", FakeBox.shown[-1])
        self.assertIn("Divine.dll", FakeBox.shown[-1])

    def test_archive_that_isnt_a_zip(self):
        asset = {"name": "ExportTool-v2.0.zip", "browser_download_url": "https://github.com/x"}
        self.retriever._latest_asset = lambda: asset

        def write(a, p):
            p.write_bytes(b"not a zip")
            return True

        self.retriever._download = write
        self.assertFalse(self.retriever.download_lslib_if_missing())
        self.assertIn("Failed to extract", FakeBox.shown[-1])

    def test_check_for_updates(self):
        self.tools_present()
        zip_path = export_zip(self.utils.tools_dir / "ExportTool-v2.0.zip")
        asset = {"name": zip_path.name, "browser_download_url": "https://github.com/x"}
        self.retriever._latest_asset = lambda: asset
        self.retriever._download = lambda *a: self.fail("downloaded again")
        # the latest is here already: said so, and checked
        self.assertTrue(self.retriever.download_lslib_if_missing(force=True))
        self.assertIn("already downloaded", FakeBox.shown[-1])
        # a newer one: asked; "Not now" keeps the tools there
        asset["name"] = "ExportTool-v3.0.zip"
        FakeBox.accept = False
        self.assertTrue(self.retriever.download_lslib_if_missing(force=True))
        self.assertIn("update available", FakeBox.shown[-1])
        self.assertFalse((self.utils.tools_dir / "ExportTool-v3.0.zip").exists())


class InstallToolsTest(RetrieverBase):
    def test_unsafe_entries_are_skipped(self):
        zip_path = export_zip(self.tmp / "ExportTool-v1.zip", extra={
            "Packed/Tools/../evil.dll": b"x",
            "Packed/Tools//Windows/x.dll": b"x",
            "Packed/Tools/C:x.dll": b"x",
            "Packed/Tools/sub\\..\\..\\evil2.dll": b"x",
            "Packed/Tools/sub/ok.dll": b"ok",
        })
        self.assertTrue(self.retriever._install_tools(zip_path, False))
        self.assertEqual((self.utils.tools_dir / "sub" / "ok.dll").read_bytes(), b"ok")
        self.assertFalse((self.utils.tools_dir.parent / "evil.dll").exists())
        self.assertFalse((self.utils.tools_dir.parent / "evil2.dll").exists())
        self.assertFalse(self.utils.tools_dir.with_name("tools.new").exists())

    def test_verify_skips_files_in_place_and_cleans_old_folders(self):
        zip_path = export_zip(self.tmp / "ExportTool-v1.zip")
        self.tools_present()
        (self.utils.tools_dir / "Divine.dll").write_bytes(b"different size!!")
        (self.utils.tools_dir / "Packed" / "Tools").mkdir(parents=True)
        (self.utils.tools_dir / "Tools").mkdir()
        self.assertTrue(self.retriever._install_tools(zip_path, True))
        self.assertEqual((self.utils.tools_dir / "Divine.dll").read_bytes(), b"Divine.dll")
        self.assertFalse((self.utils.tools_dir / "Packed").exists())
        self.assertFalse((self.utils.tools_dir / "Tools").exists())
        # all in place: no progress window at all
        self.utils.progress.clear()
        self.assertTrue(self.retriever._install_tools(zip_path, True))
        self.assertEqual(self.utils.progress, [])

    def test_cancel_keeps_the_old_tools(self):
        zip_path = export_zip(self.tmp / "ExportTool-v1.zip")
        self.tools_present()
        (self.utils.tools_dir / "Divine.exe").write_bytes(b"old")
        self.utils.cancel = True
        self.assertFalse(self.retriever._install_tools(zip_path, False))
        self.assertEqual((self.utils.tools_dir / "Divine.exe").read_bytes(), b"old")
        self.assertTrue(self.utils.progress[-1].closed)
        self.assertFalse(self.utils.tools_dir.with_name("tools.new").exists())

    def test_failed_swap_puts_the_old_files_back(self):
        zip_path = export_zip(self.tmp / "ExportTool-v1.zip", tools=["Divine.dll", "Divine.exe", "New.dll"])
        self.tools_present()
        (self.utils.tools_dir / "Divine.dll").write_bytes(b"old dll")
        real = os.replace

        def replace(src, dst):
            # Divine.exe still running: it can't be replaced
            if Path(src).parent.name == "tools.new" and Path(src).name == "New.dll":
                raise PermissionError("in use")
            return real(src, dst)

        with mock.patch.object(lslib.os, "replace", replace):
            with self.assertRaises(PermissionError):
                self.retriever._install_tools(zip_path, False)
        self.assertEqual((self.utils.tools_dir / "Divine.dll").read_bytes(), b"old dll")
        self.assertEqual((self.utils.tools_dir / "Divine.exe").read_bytes(), b"Divine.exe")
        self.assertFalse((self.utils.tools_dir / "New.dll").exists())
        self.assertFalse(self.utils.tools_dir.with_name("tools.old").exists())


class FakeResponse(io.BytesIO):
    def __init__(self, data: bytes, url: str, length: int | None = None):
        super().__init__(data)
        self._url = url
        self.headers = {"Content-Length": str(len(data) if length is None else length)}

    def geturl(self):
        return self._url

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class GitHubTest(RetrieverBase):
    def urlopen(self, data: bytes, url: str = "https://objects.githubusercontent.com/f", length=None):
        calls = []

        def fake(request, timeout=None):
            calls.append((getattr(request, "full_url", request), timeout))
            return FakeResponse(data, url, length)

        patcher = mock.patch("urllib.request.urlopen", fake)
        patcher.start()
        self.addCleanup(patcher.stop)
        return calls

    def test_latest_asset_found_by_name(self):
        release = {"tag_name": "v1.20", "assets": [
            {"name": "Source.zip", "browser_download_url": "https://github.com/a"},
            {"name": "ExportTool-v1.20.zip"},  # no URL
            {"name": "ExportTool-v1.20.zip", "browser_download_url": "https://github.com/b"},
        ]}
        calls = self.urlopen(json.dumps(release).encode())
        self.assertEqual(self.retriever._latest_asset()["browser_download_url"], "https://github.com/b")
        self.assertEqual(calls[0][1], lslib.LSLibRetriever._timeout)

    def test_release_without_the_zip(self):
        self.urlopen(json.dumps({"tag_name": "v9", "assets": None}).encode())
        with self.assertRaisesRegex(ValueError, "v9"):
            self.retriever._latest_asset()

    def asset(self, data: bytes, url="https://github.com/Norbyte/lslib/x.zip", digest=None):
        return {"name": "ExportTool-v1.zip", "browser_download_url": url,
                "digest": digest if digest is not None else "sha256:" + hashlib.sha256(data).hexdigest().upper()}

    def test_download_checked_and_put_in_place(self):
        data = b"zip bytes" * 10000
        self.urlopen(data)
        zip_path = self.utils.tools_dir / "ExportTool-v1.zip"
        zip_path.parent.mkdir(parents=True)
        self.assertTrue(self.retriever._download(self.asset(data), zip_path))
        self.assertEqual(zip_path.read_bytes(), data)
        self.assertFalse(zip_path.with_name(zip_path.name + ".part").exists())
        self.assertTrue(self.utils.progress[-1].closed)

    def test_download_refusals(self):
        data = b"zip bytes"
        zip_path = self.utils.tools_dir / "ExportTool-v1.zip"
        zip_path.parent.mkdir(parents=True)
        cases = [
            (self.asset(data, url="http://github.com/x"), "https://github.com/x", None, "not a GitHub"),
            (self.asset(data, digest=""), "https://github.com/x", None, "no SHA-256"),
            (self.asset(data), "https://evil.example/x", None, "redirected"),
            (self.asset(data), "https://github.com/x", 999, "cut at"),
            (self.asset(data, digest="sha256:00"), "https://github.com/x", None, "doesn't match"),
        ]
        for asset, final_url, length, why in cases:
            self.urlopen(data, final_url, length)
            with self.assertRaisesRegex(OSError, why):
                self.retriever._download(asset, zip_path)
            self.assertFalse(zip_path.exists(), why)
            self.assertFalse(zip_path.with_name(zip_path.name + ".part").exists(), why)

    def test_download_canceled(self):
        data = b"zip bytes"
        self.urlopen(data)
        self.utils.cancel = True
        zip_path = self.utils.tools_dir / "ExportTool-v1.zip"
        zip_path.parent.mkdir(parents=True)
        self.assertFalse(self.retriever._download(self.asset(data), zip_path))
        self.assertFalse(zip_path.exists())


class DivineTest(Tmp):
    def setUp(self) -> None:
        super().setUp()
        self.tools = self.tmp / "tools"
        self.tools.mkdir()
        self.divine = lslib.Divine(self.tools)
        self.calls: list[list[str]] = []
        self.listing = (0, "")

    def fake_run(self, command, **kwargs):
        self.calls.append(command)
        action = command[command.index("-a") + 1]
        if action == "list-package":
            code, out = self.listing
            return subprocess.CompletedProcess(command, code, out, "")
        if action == "extract-single-file":
            Path(command[command.index("-d") + 1]).write_bytes(b"<meta/>")
            return subprocess.CompletedProcess(command, 0, "", "")
        if action == "create-package":
            return subprocess.CompletedProcess(command, 0 if "good" in command[-1] else 2, "", "failed")
        raise AssertionError(command)

    def test_available(self):
        self.assertFalse(self.divine.available())
        (self.tools / "Divine.exe").write_bytes(b"MZ")
        self.assertTrue(self.divine.available())

    def test_read_lists_and_extracts_meta(self):
        self.listing = (0, "Mods\\X\\meta.lsx\t123\t456\nPublic/X/a.txt\t1\n\n")
        with mock.patch.object(lslib.subprocess, "run", self.fake_run):
            names, meta = self.divine.read(self.tmp / "x.pak")
        self.assertEqual(names, ["Mods/X/meta.lsx", "Public/X/a.txt"])
        self.assertEqual(meta, b"<meta/>")
        self.assertEqual(self.calls[0][:5], [str(self.tools / "Divine.exe"), "-g", "bg3", "-l", "info"])

    def test_read_without_meta(self):
        self.listing = (0, "Public/Game/x.dds\n")
        with mock.patch.object(lslib.subprocess, "run", self.fake_run):
            self.assertEqual(self.divine.read(self.tmp / "x.pak"), (["Public/Game/x.dds"], None))
        self.assertEqual(len(self.calls), 1)

    def test_read_failure_is_damaged(self):
        self.listing = (1, "boom")
        with mock.patch.object(lslib.subprocess, "run", self.fake_run):
            with self.assertRaises(paks.DamagedPak):
                self.divine.read(self.tmp / "x.pak")

    def test_pack(self):
        old = self.tmp / "out" / "good.pak"
        old.parent.mkdir()
        old.write_bytes(b"old")
        with mock.patch.object(lslib.subprocess, "run", self.fake_run):
            self.assertTrue(self.divine.pack(self.tmp / "ws", old))
            self.assertFalse(old.exists())
            self.assertFalse(self.divine.pack(self.tmp / "ws", self.tmp / "new" / "bad.pak"))
        self.assertTrue((self.tmp / "new").is_dir())


class HelpersTest(unittest.TestCase):
    def test_safe_member(self):
        root = Path(tempfile.gettempdir()) / "bg3-tools-x"
        self.assertEqual(lslib._safe_member("sub/a.dll", root), Path("sub/a.dll"))
        self.assertEqual(lslib._safe_member("sub\\a.dll", root), Path("sub/a.dll"))
        for bad in ("", "../a.dll", "a/../../b", "/a.dll", "a//b", "C:a.dll", "./a", "sub/C:/x"):
            self.assertIsNone(lslib._safe_member(bad, root), bad)

    def test_version_key(self):
        self.assertEqual(lslib._version_key("ExportTool-v1.20.3.zip"), (1, 20, 3))
        self.assertEqual(lslib._version_key("exporttool-V2.zip"), (2,))
        self.assertEqual(lslib._version_key("ExportTool.zip"), ())


if __name__ == "__main__":
    unittest.main()
