# MO2 Python plugin for the download and install stress run (copied into a test
# bed only, never shipped). Drives downloads from a local server and installs,
# answers MO2's modal dialogs as a user would, and writes MO2R_DL_OUT/result.json.
import hashlib
import json
import os
import subprocess
import time
import traceback

import mobase
from PyQt6.QtCore import Q_ARG, QMetaObject, Qt, QTimer
from PyQt6.QtWidgets import (QAbstractButton, QAbstractItemView, QApplication, QDialog,
                             QMessageBox)

OUT = os.environ.get("MO2R_DL_OUT", "")
BASE = os.environ.get("MO2R_DL_BASE", "")      # http://127.0.0.1:<port>
SRV = os.environ.get("MO2R_DL_SRV", "")        # the server's folder, to compare
RATE = 400_000


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


class DlStress(mobase.IPlugin):
    def __init__(self):
        super().__init__()
        self._org = None
        self._r = {"errors": [], "dialogs": [], "phases": {}}
        self._answer = []      # button texts to prefer, in order
        self._gen = None

    def init(self, organizer):
        self._org = organizer
        if OUT:
            organizer.onUserInterfaceInitialized(lambda w: QTimer.singleShot(6000, self._start))
        return True

    # -- helpers ---------------------------------------------------------
    def _note(self, phase, key, value):
        self._r["phases"].setdefault(phase, {})[key] = value
        self._partial()

    def _partial(self):
        # what is known so far, in case MO2 stops answering
        try:
            os.makedirs(OUT, exist_ok=True)
            with open(os.path.join(OUT, "partial.json"), "w", encoding="utf-8") as f:
                json.dump(self._r, f, indent=1, ensure_ascii=False, default=str)
        except OSError:
            pass

    def _err(self, text):
        self._r["errors"].append(text)

    def _view(self):
        for w in QApplication.topLevelWidgets():
            v = w.findChild(QAbstractItemView, "downloadView")
            if v is not None:
                return v
        return None

    def _signal(self, name, *args):
        view = self._view()
        qargs = [Q_ARG(type(a), a) for a in args]
        return QMetaObject.invokeMethod(view, name, Qt.ConnectionType.DirectConnection, *qargs)

    def _paths(self):
        dm = self._org.downloadManager()
        out = []
        for i in range(400):
            try:
                p = dm.downloadPath(i)
            except RuntimeError:
                break
            if not p:
                break
            out.append(p)
        return out

    def _index(self, name):
        for i, p in enumerate(self._paths()):
            if os.path.basename(p).lower() == name.lower():
                return i
        return -1

    def _dldir(self):
        return self._org.downloadsPath()

    def _start_dl(self, url):
        return self._org.downloadManager().startDownloadURLs([url])

    def _intact(self, phase, name, source):
        path = os.path.join(self._dldir(), name)
        ok = os.path.isfile(path) and sha(path) == sha(os.path.join(SRV, source))
        self._note(phase, f"{name} intact", ok)
        if not ok:
            self._err(f"{phase}: {name} missing or not the server's file")
        return ok

    # modal dialogs: recorded, then answered with the preferred button
    def _watch(self):
        w = QApplication.activeModalWidget()
        if w is not None and w.isVisible():
            text = ""
            if isinstance(w, QMessageBox):
                text = w.text()
            buttons = [b for b in w.findChildren(QAbstractButton) if b.isVisible() and b.text()]
            names = [b.text().replace("&", "") for b in buttons]
            key = (w.windowTitle(), text[:120], tuple(names))
            entry = {"key": list(key), "at": time.strftime("%H:%M:%S"),
                     "class": type(w).__name__}
            chosen = None
            # restyled dialogs draw their own buttons: by object name first
            for want in self._answer:
                found = w.findChild(QAbstractButton, want)
                if found is not None and found.isVisible():
                    chosen = found
                    break
            for want in [] if chosen else self._answer + ["Install", "Instalar", "OK", "Aceptar", "Yes", "Sí",
                                        "Close", "Cerrar"]:
                for b, n in zip(buttons, names):
                    if n.lower() == want.lower():
                        chosen = b
                        break
                if chosen:
                    break
            entry["answer"] = (chosen.objectName() or chosen.text()) if chosen is not None else "rejected"
            self._r["dialogs"].append(entry)
            self._partial()
            try:
                if chosen is not None:
                    chosen.click()
                elif isinstance(w, QDialog):
                    w.reject()
            except RuntimeError:
                pass
        QTimer.singleShot(250, self._watch)

    # -- the run ---------------------------------------------------------
    def _start(self):
        QTimer.singleShot(250, self._watch)
        self._gen = self._run()
        self._step()

    def _step(self):
        try:
            wait = next(self._gen)
        except StopIteration:
            self._finish()
            return
        except Exception:
            self._err("plugin: " + traceback.format_exc()[-600:])
            self._finish()
            return
        QTimer.singleShot(wait, self._step)

    def _wait_ready(self, names, timeout_s=60):
        t = time.time()
        while time.time() - t < timeout_s:
            if all(os.path.isfile(os.path.join(self._dldir(), n)) and
                   not os.path.isfile(os.path.join(self._dldir(), n + ".unfinished")) for n in names):
                return True
            yield 500
        return False

    def _run(self):
        # 1: four at once, two paused, one resumed, one canceled
        p = "several"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        for i in range(4):
            self._start_dl(f"{BASE}/file/dlmod{i}.zip?rate={RATE}")
        yield 1200
        i0, i1, i2 = self._index("dlmod0.zip"), self._index("dlmod1.zip"), self._index("dlmod2.zip")
        self._note(p, "indices", [i0, i1, i2])
        self._signal("pauseDownload", i0)
        self._signal("pauseDownload", i1)
        yield 800
        self._signal("resumeDownload", i0)
        self._signal("pauseDownload", i0)   # quickly again
        yield 300
        self._signal("resumeDownload", i0)
        self._signal("cancelDownload", self._index("dlmod2.zip"))
        yield 1500
        self._signal("resumeDownload", self._index("dlmod1.zip"))
        ok = yield from self._wait_ready(["dlmod0.zip", "dlmod1.zip", "dlmod3.zip"])
        self._note(p, "all finished", ok)
        for n in ("dlmod0.zip", "dlmod1.zip", "dlmod3.zip"):
            self._intact(p, n, n)
        left = [f for f in os.listdir(self._dldir()) if f.startswith("dlmod2.zip")]
        self._note(p, "canceled leftovers", left)
        if left:
            self._err(f"{p}: the canceled download left {left}")

        # 2: the same link twice at once
        p = "same name"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        url = f"{BASE}/file/dlmod4.zip?rate={RATE}"
        QTimer.singleShot(0, lambda: self._start_dl(url))
        self._start_dl(url)
        yield 2000
        self._answer = ["Yes", "Sí"]
        yield 6000
        names = sorted(f for f in os.listdir(self._dldir()) if f.endswith("dlmod4.zip"))
        self._note(p, "files", names)
        for n in names:
            self._intact(p, n, "dlmod4.zip")
        self._answer = []

        # 3: the connection drops: no finished-looking truncated file
        p = "cut"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        self._start_dl(f"{BASE}/cut/dlmod5.zip?after=400000")
        yield 15000
        path = os.path.join(self._dldir(), "dlmod5.zip")
        self._note(p, "files", [f for f in os.listdir(self._dldir()) if f.startswith("dlmod5")])
        if os.path.isfile(path) and os.path.getsize(path) != os.path.getsize(os.path.join(SRV, "dlmod5.zip")):
            self._err(f"{p}: a truncated dlmod5.zip is shown as complete")

        # 4: an empty list
        p = "empty"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        try:
            self._note(p, "returned", self._org.downloadManager().startDownloadURLs([]))
        except Exception as e:  # noqa: BLE001
            self._note(p, "raised", str(e))
        yield 500

        # 5: a Unicode name from Content-Disposition, then installed
        p = "unicode"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        uname = "Ñandú Ünïcødé 日本語.zip"
        self._start_dl(f"{BASE}/file/{uname}?cd={uname}")
        yield 3000
        files = [f for f in os.listdir(self._dldir()) if not f.endswith(".meta")]
        self._note(p, "downloads", files)
        target = next((f for f in files if "dl" not in f.lower() or "ñ" in f.lower()), None)
        if target:
            mod = self._org.installMod(os.path.join(self._dldir(), target), "Unicode Mod")
            self._note(p, "installed", mod is not None)
            folder = os.path.join(self._org.modsPath(), "Unicode Mod")
            self._note(p, "files", sorted(os.path.relpath(os.path.join(d, f), folder)
                                          for d, _, fs in os.walk(folder) for f in fs))
        yield 1000

        # 6: paths past MAX_PATH inside the archive
        p = "deep"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        try:
            mod = self._org.installMod(os.path.join(SRV, "deepmod.zip"), "Deep Mod")
            self._note(p, "installed", mod is not None)
        except Exception as e:  # noqa: BLE001
            self._note(p, "raised", str(e)[:300])
        self._note(p, "folder left", os.path.isdir(os.path.join(self._org.modsPath(), "Deep Mod")))
        yield 1000

        # 7: the same mod twice, replaced
        p = "duplicate"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        self._org.installMod(os.path.join(SRV, "dupmod.zip"), "Dup Mod")
        yield 500
        self._answer = ["replaceBtn", "Replace", "Reemplazar"]
        self._org.installMod(os.path.join(SRV, "dupmod-v2.zip"), "Dup Mod")
        self._answer = []
        yield 1000
        folder = os.path.join(self._org.modsPath(), "Dup Mod")
        has = sorted(os.path.relpath(os.path.join(d, f), folder).replace("\\", "/")
                     for d, _, fs in os.walk(folder) for f in fs if f != "meta.ini")
        self._note(p, "files", has)
        if "textures/dup/two.dds" not in has:
            self._err(f"{p}: the replaced mod lacks the new file")
        if "textures/dup/one.dds" in has:
            self._err(f"{p}: a replace kept the old mod's file")

        # 8: names that aren't folder names
        p = "names"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        before = set(os.listdir(self._org.modsPath()))
        for name in ("Dup Mod/sub", "..", "Odd."):
            try:
                mod = self._org.installMod(os.path.join(SRV, "dupmod.zip"), name)
                self._note(p, name, None if mod is None else mod.name())
            except Exception as e:  # noqa: BLE001
                self._note(p, name, "raised " + str(e)[:200])
            yield 500
        self._note(p, "new folders", sorted(set(os.listdir(self._org.modsPath())) - before))
        if os.path.isdir(os.path.join(self._org.modsPath(), "Dup Mod", "sub")):
            self._err(f"{p}: a mod was installed inside another mod's folder")
        inst = os.path.dirname(self._org.modsPath())
        if os.path.isfile(os.path.join(inst, "dupmod.esp")):
            self._err(f"{p}: a mod was installed into the instance folder")

        # 9: remove a download while it is installed
        p = "remove while installing"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        idx = self._index("dlmod0.zip")
        self._answer = ["replaceBtn", "Replace", "Reemplazar"]
        # from the list only: deleting the file it installs from brings up the
        # Windows "file in use" prompt, which this plugin can't answer
        QTimer.singleShot(150, lambda: self._signal("removeDownload", self._index("dlmod0.zip"), False))
        self._note(p, "install called", time.strftime("%H:%M:%S"))
        self._signal("installDownload", idx)
        self._note(p, "install returned", time.strftime("%H:%M:%S"))
        yield 3000
        self._answer = []
        self._note(p, "archive still there", os.path.isfile(os.path.join(self._dldir(), "dlmod0.zip")))
        self._note(p, "mod", os.path.isdir(os.path.join(self._org.modsPath(), "dlmod0")))

        # 10: a read-only downloads folder
        p = "read-only folder"
        self._note(p, "started", time.strftime("%H:%M:%S"))
        user = os.environ.get("USERNAME", "")
        dl = self._dldir()
        subprocess.run(["icacls", dl, "/deny", f"{user}:(OI)(CI)(W,D,DC)"], capture_output=True)
        try:
            self._note(p, "returned", self._start_dl(f"{BASE}/file/dlmod5.zip?rate={RATE}"))
            yield 4000
        finally:
            subprocess.run(["icacls", dl, "/remove:d", user], capture_output=True)
        yield 500

    def _finish(self):
        self._r["downloads_now"] = [os.path.basename(p) for p in self._paths()]
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "result.json"), "w", encoding="utf-8") as f:
            json.dump(self._r, f, indent=1, ensure_ascii=False)
        with open(os.path.join(OUT, "done"), "w") as f:
            f.write("errors" if self._r["errors"] else "ok")

    def name(self):
        return "MO2R Download Stress"

    def author(self):
        return "MO2 Revamped"

    def description(self):
        return "Test-only plugin."

    def version(self):
        return mobase.VersionInfo(1, 0, 0)

    def settings(self):
        return []


def createPlugin():
    return DlStress()
