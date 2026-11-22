# MO2 Python plugin for the stability stress run (copied into a test bed only,
# never shipped). Once the interface is up: fast mod toggles, mods enabled
# during a refresh, the plugin view's selection across a fast-path disable,
# then a consistency check. Results go to MO2R_STRESS_OUT/result.json.
import json
import os
import random
import time

import mobase
from PyQt6.QtCore import QItemSelectionModel, QTimer, Qt
from PyQt6.QtWidgets import QAbstractItemView, QApplication

OUT = os.environ.get("MO2R_STRESS_OUT", "")


class Stress(mobase.IPlugin):
    def __init__(self):
        super().__init__()
        self._org = None
        self._log = []
        self._result = {"errors": []}
        self._rng = random.Random(7)

    def init(self, organizer):
        self._org = organizer
        if OUT:
            organizer.onUserInterfaceInitialized(lambda w: QTimer.singleShot(8000, self._start))
        return True

    # -- helpers ---------------------------------------------------------
    def _note(self, text):
        self._log.append(f"{time.strftime('%H:%M:%S')} {text}")

    def _mods(self):
        return [m for m in self._org.modList().allModsByProfilePriority()
                if m.startswith("Stress ")]

    def _active(self, mod):
        return bool(self._org.modList().state(mod) & mobase.ModState.ACTIVE)

    def _plugins_of(self, mod):
        folder = os.path.join(self._org.modsPath(), mod)
        return [f for f in os.listdir(folder) if f.lower().endswith(".esp")]

    def _plugin_active(self, name):
        return self._org.pluginList().state(name) == mobase.PluginState.ACTIVE

    def _view(self):
        for w in QApplication.topLevelWidgets():
            view = w.findChild(QAbstractItemView, "espList")
            if view is not None:
                return view
        return None

    def _after(self, ms, fn):
        QTimer.singleShot(ms, fn)

    # -- phases ----------------------------------------------------------
    def _start(self):
        self._note(f"start, {len(self._mods())} mods, {len(self._org.pluginList().pluginNames())} plugins")
        self._toggles = 0
        self._times = []
        self._toggle_step()

    def _toggle_step(self):
        if self._toggles >= 120:
            self._result["toggles"] = {
                "count": len(self._times),
                "avg_ms": round(sum(self._times) / max(1, len(self._times)), 1),
                "max_ms": round(max(self._times or [0]), 1),
            }
            self._note(f"toggles done {self._result['toggles']}")
            # every mod back on before the next phase
            for mod in self._mods():
                if not self._active(mod):
                    self._org.modList().setActive(mod, True)
            self._after(3000, self._selection_phase)
            return
        mod = self._rng.choice(self._mods()[:400])
        t = time.perf_counter()
        self._org.modList().setActive(mod, not self._active(mod))
        self._times.append((time.perf_counter() - t) * 1000)
        self._toggles += 1
        self._after(self._rng.choice([0, 0, 10, 50]), self._toggle_step)

    def _selection_phase(self, round_=0):
        view = self._view()
        if view is None:
            self._result["errors"].append("plugin view not found")
            self._after(0, self._refresh_phase)
            return
        model = view.model()
        mods = self._mods()
        # select a plugin late in the list, then disable a mod early in it
        target_mod = mods[1200 + round_]
        early_mod = mods[300 + round_ * 3]
        target = f"stress_{int(target_mod.split()[1]):04d}.esp"
        row = None
        for r in range(model.rowCount()):
            if model.index(r, 0).data(Qt.ItemDataRole.DisplayRole) == target:
                row = r
                break
        if row is None:
            self._result["errors"].append(f"selection: {target} not in the view")
        else:
            index = model.index(row, 0)
            view.selectionModel().setCurrentIndex(
                index,
                QItemSelectionModel.SelectionFlag.ClearAndSelect
                | QItemSelectionModel.SelectionFlag.Rows,
            )
            self._org.modList().setActive(early_mod, False)
            current = view.selectionModel().currentIndex()
            selected = [i.data(Qt.ItemDataRole.DisplayRole) for i in view.selectionModel().selectedRows()]
            name = current.data(Qt.ItemDataRole.DisplayRole) if current.isValid() else None
            ok = name == target and selected == [target]
            self._result.setdefault("selection", []).append(
                {"target": target, "current": name, "selected": selected, "ok": ok})
            if not ok:
                self._result["errors"].append(f"selection moved: {target} -> {name} {selected}")
            self._org.modList().setActive(early_mod, True)
        if round_ < 4:
            self._after(1500, lambda: self._selection_phase(round_ + 1))
        elif os.environ.get("MO2R_STRESS_PHASES") == "selection":
            self._after(2000, self._final)
        else:
            self._after(2000, self._refresh_phase)

    def _refresh_phase(self, round_=0):
        mods = self._mods()
        mod = mods[500 + round_ * 7]
        if self._active(mod):
            self._org.modList().setActive(mod, False)
        self._pending = mod
        self._org.refresh(True)
        # during the refresh
        self._after(30, lambda: self._enable_during_refresh(round_))

    def _enable_during_refresh(self, round_):
        # the refresh must still be running, or this tests the plain enable:
        # onNextRefresh(..., True) runs the callback at once when no refresh
        # is running, and keeps it for the refresh's end when one is
        ran_at_once = []
        self._org.onNextRefresh(lambda: ran_at_once.append(True), True)
        self._during = not ran_at_once
        if not self._during:
            self._result["errors"].append(
                f"{self._pending}: the refresh had ended before the mod was enabled; "
                "the round doesn't test enabling during a refresh")
        self._org.modList().setActive(self._pending, True)
        self._waited = 0
        self._after(2000, lambda: self._check_enabled(round_))

    def _check_enabled(self, round_):
        mod = self._pending
        plugins = self._plugins_of(mod)
        off = [p for p in plugins if not self._plugin_active(p)]
        if off and self._waited < 30:
            self._waited += 2
            self._after(2000, lambda: self._check_enabled(round_))
            return
        self._result.setdefault("enable_during_refresh", []).append(
            {"mod": mod, "plugins": plugins, "inactive": off, "waited_s": self._waited + 2,
             "during_refresh": self._during})
        if off:
            self._result["errors"].append(f"{mod}: plugins still off after a refresh: {off}")
        if round_ < 5:
            self._after(500, lambda: self._refresh_phase(round_ + 1))
        else:
            self._after(3000, self._final)

    def _final(self):
        missing = []
        known = {n.lower() for n in self._org.pluginList().pluginNames()}
        for mod in self._mods():
            if not self._active(mod):
                continue
            for p in self._plugins_of(mod):
                if p.lower() not in known:
                    missing.append(p)
        self._result["missing_plugins"] = missing[:20]
        if missing:
            self._result["errors"].append(f"{len(missing)} plugins of active mods missing")
        self._result["plugins_now"] = len(self._org.pluginList().pluginNames())
        self._result["log"] = self._log
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, "result.json"), "w", encoding="utf-8") as f:
            json.dump(self._result, f, indent=1)
        with open(os.path.join(OUT, "done"), "w") as f:
            f.write("errors" if self._result["errors"] else "ok")

    def name(self):
        return "MO2R Stress"

    def author(self):
        return "MO2 Revamped"

    def description(self):
        return "Test-only plugin."

    def version(self):
        return mobase.VersionInfo(1, 0, 0)

    def settings(self):
        return []


def createPlugin():
    return Stress()
