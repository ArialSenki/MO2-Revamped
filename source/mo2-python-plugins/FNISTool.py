# MO2 Revamped: the FNIS integration, rewritten for Revamped. It keeps the
# plugin name, the settings and the way it runs FNIS from Mod Organizer 2's
# FNIS Integration Tool by AnyOldName3 (GNU GPL version 3), so existing
# settings carry over.
#
# Tools > FNIS > Run FNIS runs GenerateFNISforUsers.exe inside the VFS with
# InstantExecute=1, and, when chosen, RedirectFiles="<output mod>" so FNIS
# writes into a mod of its own. The first time, or when a saved choice no
# longer holds, one window asks for everything it needs.

from __future__ import annotations

import os

from PyQt6.QtCore import QCoreApplication
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QCheckBox, QComboBox, QFileDialog, QInputDialog, QMessageBox, QPushButton, QWidget

import mobase

import revamped_fluent as fluent

# MO2 installs translations/revamped_<language>.qm; the source text is English
_TR_CONTEXT = "RevampedFNIS"

TOOL_NAME = "FNIS Integration Tool"
EXECUTABLE = "GenerateFNISforUsers.exe"
FNIS_GAMES = ["Skyrim", "Skyrim Special Edition", "Skyrim VR"]

# characters Windows does not allow in a folder name
_INVALID_NAME = set('<>:"/\\|?*')


def tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


def _norm(path: str) -> str:
    """A path as Windows compares it: absolute, one kind of slash, any case.
    Junctions are not followed: a mod folder that is a junction to another
    drive is still inside the mods folder."""
    return os.path.normcase(os.path.abspath(path)) if path else ""


def _relative(path: str, folder: str) -> str | None:
    """path below folder, as it is spelled, or None when it isn't below it."""
    compared, base = _norm(path), _norm(folder)
    if not compared or not base:
        return None
    try:
        if os.path.commonpath([compared, base]) != base or compared == base:
            return None
    except ValueError:  # different drives
        return None
    return os.path.relpath(os.path.abspath(path), os.path.abspath(folder))


class FnisSettings:
    """The tool's settings, read and checked together. The keys are those of
    MO2's FNIS Integration Tool; every value is kept as saved, the choices only
    apply when FNIS runs."""

    def __init__(self, organizer: mobase.IOrganizer):
        self._organizer = organizer
        get = lambda key: organizer.pluginSetting(TOOL_NAME, key)
        self.executable = str(get("fnis-path") or "")
        self.output_to_mod = bool(get("output-to-mod"))
        self.output_path = str(get("output-path") or "")
        self.logs_to_mod = bool(get("output-logs-to-mod"))
        self.logs_path = str(get("output-logs-path") or "")
        self.initialised = bool(get("initialised"))

    def save(self) -> None:
        set_ = lambda key, value: self._organizer.setPluginSetting(TOOL_NAME, key, value)
        set_("fnis-path", self.executable)
        set_("output-to-mod", self.output_to_mod)
        set_("output-path", self.output_path)
        set_("output-logs-to-mod", self.logs_to_mod)
        set_("output-logs-path", self.logs_path)
        set_("initialised", True)

    # --- where things are -------------------------------------------------
    def mods_folder(self) -> str:
        return self._organizer.modsPath()

    def data_folder(self) -> str:
        return self._organizer.managedGame().dataDirectory().absolutePath()

    def fnis_mod(self) -> str:
        """The mod FNIS is installed in, or "" when it is in the game's Data."""
        relative = _relative(self.executable, self.mods_folder())
        if relative is None:
            return ""
        parts = relative.split(os.sep)
        # a file right in the mods folder is in no mod
        return parts[0] if len(parts) > 1 else ""

    def logs_go_to_mod(self) -> bool:
        return self.output_to_mod and self.logs_to_mod

    # --- checks ------------------------------------------------------------
    def executable_problem(self) -> str:
        if not self.executable:
            return tr("Choose GenerateFNISforUsers.exe.")
        if not os.path.isfile(self.executable):
            return tr("GenerateFNISforUsers.exe is no longer at this location.")
        if not (self.fnis_mod() or _relative(self.executable, self.data_folder())):
            return tr("FNIS only works inside MO2's virtual Data folder: choose a copy in a mod "
                      "or in the game's Data folder.")
        return ""

    def mod_problem(self, path: str, logs: bool) -> str:
        if not path:
            return tr("Choose the mod for FNIS's logs.") if logs else tr("Choose the mod for FNIS's files.")
        relative = _relative(path, self.mods_folder())
        if relative is None or os.sep in relative or not os.path.isdir(path):
            return tr("This mod is no longer in MO2's mods folder.")
        name = os.path.basename(_norm(path))
        if name == os.path.normcase(self.fnis_mod()):
            return tr("This is the mod FNIS is installed in. Choose another one.")
        if logs and _norm(path) == _norm(self.output_path):
            return tr("FNIS's logs need a different mod from its files.")
        return ""

    def problem(self) -> str:
        """The first thing that keeps FNIS from running, or ""."""
        problem = self.executable_problem()
        if not problem and self.output_to_mod:
            problem = self.mod_problem(self.output_path, False)
            if not problem and self.logs_to_mod:
                problem = self.mod_problem(self.logs_path, True)
        return problem


class SetupDialog(fluent.ContentDialog):
    """Everything the tool needs, in one window: where FNIS is and where its
    files and logs go."""

    def __init__(self, parent: QWidget | None, settings: FnisSettings, run_button: bool):
        text = tr("MO2 runs FNIS inside its virtual Data folder, so FNIS sees the animations of "
                  "every active mod.")
        if run_button:
            text += " " + tr("These choices are saved; Tools > FNIS > Change FNIS settings "
                             "brings this window back.")
        super().__init__(parent, "FnisSetupDialog", tr("Set up FNIS"), text)
        self._settings = settings
        # a mod chosen before has FNIS's own files: only a new choice is warned about
        self._saved = {_norm(p) for p in (settings.output_path, settings.logs_path) if p}
        self._created = False
        self.setMinimumWidth(640)

        location = fluent.card(tr("FNIS"))
        browse = fluent.content_button(tr("Browse…"), fluent.GLYPH_FOLDER)
        browse.setAccessibleName(tr("Find GenerateFNISforUsers.exe"))
        browse.clicked.connect(self._browse)
        self._exe_row = fluent.SettingRow(location, fluent.GLYPH_PLAY, EXECUTABLE, "", (browse,))
        self.body.addWidget(location)

        output = fluent.card(tr("Where FNIS writes"))
        self._output_box = QCheckBox()
        self._output_box.setChecked(settings.output_to_mod)
        fluent.SettingRow(
            output, fluent.GLYPH_FOLDER, tr("Write FNIS's files to a mod of their own"),
            tr("Recommended. Otherwise FNIS replaces its earlier files in whichever mod has "
               "them and puts new ones in Overwrite. MO2 turns this mod off while FNIS runs."),
            (self._output_box,),
        )
        self._output_combo, output_new = self._mod_picker(
            settings.output_path, "FNIS Output", tr("New mod for FNIS's files"))
        self._output_mod_row = fluent.SettingRow(
            output, fluent.GLYPH_LIST, tr("Mod for FNIS's files"), "",
            (self._output_combo, output_new),
        )
        self._logs_box = QCheckBox()
        self._logs_box.setChecked(settings.logs_to_mod)
        self._logs_row = fluent.SettingRow(
            output, fluent.GLYPH_PAGE, tr("Write FNIS's logs to a mod of their own"),
            tr("Otherwise new logs go to Overwrite."),
            (self._logs_box,),
        )
        self._logs_combo, logs_new = self._mod_picker(
            settings.logs_path, "FNIS Logs", tr("New mod for FNIS's logs"))
        self._logs_mod_row = fluent.SettingRow(
            output, fluent.GLYPH_LIST, tr("Mod for FNIS's logs"), "",
            (self._logs_combo, logs_new),
        )
        self.body.addWidget(output)

        # not the default: Enter on a mod list or a switch mustn't start FNIS
        self._accept = self.addButton(tr("Save and run FNIS") if run_button else tr("Save"), True,
                                      default=False)
        cancel = self.addButton(tr("Cancel"))
        self._accept.clicked.connect(self._save)
        cancel.clicked.connect(self.reject)
        for signal in (self._output_box.toggled, self._logs_box.toggled,
                       self._output_combo.currentIndexChanged, self._logs_combo.currentIndexChanged):
            signal.connect(self._refresh)
        self._refresh()

    # --- mods --------------------------------------------------------------
    def _mod_names(self) -> list[str]:
        try:
            names = [entry.name for entry in os.scandir(self._settings.mods_folder())
                     if entry.is_dir() and not entry.name.endswith("_separator")]
        except OSError:
            names = []
        return sorted(names, key=str.casefold)

    def _fill(self, combo: QComboBox, selected: str) -> None:
        combo.blockSignals(True)
        combo.clear()
        folder = self._settings.mods_folder()
        for name in self._mod_names():
            combo.addItem(name, os.path.join(folder, name))
        wanted = _norm(selected)
        combo.setCurrentIndex(next(
            (i for i in range(combo.count()) if wanted and _norm(combo.itemData(i)) == wanted), -1))
        combo.blockSignals(False)

    def _mod_picker(self, selected: str, suggestion: str, new_name: str) -> tuple[QComboBox, QPushButton]:
        combo = QComboBox()
        combo.setPlaceholderText(tr("Choose a mod"))
        self._fill(combo, selected)
        new = fluent.content_button(tr("New mod…"), fluent.GLYPH_ADD)
        new.setAccessibleName(new_name)
        new.clicked.connect(lambda: self._new_mod(combo, suggestion))
        return combo, new

    def _new_mod(self, combo: QComboBox, suggestion: str) -> None:
        name, ok = QInputDialog.getText(self, tr("New mod"), tr("Name of the new mod:"), text=suggestion)
        name = name.strip() if ok else ""
        if not name:
            return
        if any(c in _INVALID_NAME for c in name) or name.endswith(".") or name.endswith("_separator"):
            QMessageBox.warning(self, tr("New mod"),
                                tr("A mod's name can't contain < > : \" / \\ | ? *, or end in a dot "
                                   "or _separator."))
            return
        folder = os.path.join(self._settings.mods_folder(), name)
        if os.path.exists(folder):
            QMessageBox.warning(self, tr("New mod"), tr("There is already a mod called \"{name}\".").format(name=name))
            return
        try:
            os.mkdir(folder)
        except OSError as error:
            QMessageBox.warning(self, tr("New mod"), tr("The mod could not be created:\n{error}").format(error=error))
            return
        self._created = True
        for other in (self._output_combo, self._logs_combo):
            self._fill(other, folder if other is combo else (other.currentData() or ""))
        self._refresh()

    # --- choices -------------------------------------------------------------
    def _browse(self) -> None:
        start = self._settings.executable
        start = os.path.dirname(start) if start and os.path.isfile(start) else self._settings.mods_folder()
        path, _ = QFileDialog.getOpenFileName(self, tr("Find GenerateFNISforUsers.exe"), start,
                                              f"FNIS ({EXECUTABLE})")
        if path:
            self._settings.executable = os.path.normpath(path)
            self._refresh()

    def _collect(self) -> None:
        self._settings.output_to_mod = self._output_box.isChecked()
        self._settings.logs_to_mod = self._logs_box.isChecked()
        # a mod no longer chosen in a row that is off keeps its saved path
        for combo, attribute in ((self._output_combo, "output_path"), (self._logs_combo, "logs_path")):
            if combo.currentIndex() >= 0 or combo.isEnabled():
                setattr(self._settings, attribute, combo.currentData() or "")

    def _not_empty(self, path: str) -> bool:
        try:
            return any(entry.name != "meta.ini" for entry in os.scandir(path))
        except OSError:
            return False

    def _refresh(self) -> None:
        self._collect()
        settings = self._settings
        self._exe_row.setDescription(os.path.normpath(settings.executable) if settings.executable else "")
        self._exe_row.setProblem(settings.executable_problem())
        output = settings.output_to_mod
        self._output_mod_row.setEnabled(output)
        self._logs_row.setEnabled(output)
        self._logs_mod_row.setEnabled(settings.logs_go_to_mod())
        for row, path, logs, enabled in (
            (self._output_mod_row, settings.output_path, False, output),
            (self._logs_mod_row, settings.logs_path, True, settings.logs_go_to_mod()),
        ):
            problem = settings.mod_problem(path, logs) if enabled else ""
            note = ""
            if enabled and not problem and _norm(path) not in self._saved and self._not_empty(path):
                note = tr("This mod already has files. FNIS may replace some of them.")
            row.setDescription(note)
            row.setProblem(problem)
        # each row says what is wrong with it; Save waits until nothing is
        self._accept.setEnabled(not settings.problem())

    def _save(self) -> None:
        self._collect()
        if self._settings.problem():
            return
        self._settings.save()
        self.accept()

    def created_mods(self) -> bool:
        return self._created


def setup(organizer: mobase.IOrganizer, parent: QWidget | None, run_button: bool) -> FnisSettings | None:
    """Opens the setup window; the saved settings, or None if it was cancelled."""
    settings = FnisSettings(organizer)
    if not settings.initialised:
        # the defaults of a first run: FNIS's files and logs in mods of their own
        settings.output_to_mod = True
        settings.logs_to_mod = True
    dialog = SetupDialog(parent, settings, run_button)
    accepted = dialog.exec() == SetupDialog.DialogCode.Accepted
    if dialog.created_mods():
        organizer.refresh()
    return settings if accepted else None


class FNISTool(mobase.IPluginTool):

    def __init__(self):
        super().__init__()
        self._organizer: mobase.IOrganizer | None = None
        self._parent: QWidget | None = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        return True

    def name(self) -> str:
        return TOOL_NAME

    def localizedName(self) -> str:
        return tr("FNIS Integration Tool")

    def author(self) -> str:
        return "AnyOldName3, MO2 Revamped"

    def description(self) -> str:
        return tr("Runs FNIS (GenerateFNISforUsers.exe) inside MO2 so Skyrim loads your mods' animations.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(2, 0, 0, mobase.ReleaseType.FINAL)

    def requirements(self) -> list:
        return [mobase.PluginRequirementFactory.gameDependency(FNIS_GAMES)]

    def settings(self) -> list:
        return [
            mobase.PluginSetting("fnis-path", tr("Where GenerateFNISforUsers.exe is."), ""),
            mobase.PluginSetting("output-to-mod", tr("Write FNIS's files to a mod of their own."), True),
            mobase.PluginSetting("output-path", tr("The mod FNIS's files go to."), ""),
            mobase.PluginSetting("initialised", tr("The setup has been done. Off shows the setup window again."), False),
            mobase.PluginSetting("output-logs-to-mod", tr("Write FNIS's logs to a mod of their own."), True),
            mobase.PluginSetting("output-logs-path", tr("The mod FNIS's logs go to."), ""),
        ]

    def displayName(self) -> str:
        return tr("FNIS/Run FNIS")

    def tooltip(self) -> str:
        return self.description()

    def icon(self) -> QIcon:
        # MO2 gives tools without an icon a Fluent glyph
        return QIcon()

    def setParentWidget(self, widget: QWidget) -> None:
        self._parent = widget

    def display(self) -> None:
        settings = FnisSettings(self._organizer)
        if not settings.initialised or settings.problem():
            settings = setup(self._organizer, self._parent, True)
            if settings is None:
                return
        self._run(settings)

    def _run(self, settings: FnisSettings) -> None:
        mod_list = self._organizer.modList()
        fnis_mod = settings.fnis_mod()
        if fnis_mod and mod_list.getMod(fnis_mod) is not None \
                and not (mod_list.state(fnis_mod) & mobase.ModState.ACTIVE):
            answer = QMessageBox.question(
                self._parent, tr("FNIS is in a mod that is off"),
                tr("FNIS is installed in \"{mod}\", which is turned off, so MO2 can't run it. "
                   "Turn the mod on and run FNIS?").format(mod=fnis_mod),
                QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            )
            if answer != QMessageBox.StandardButton.Yes:
                return
            mod_list.setActive(fnis_mod, True)

        args = []
        output_mod = log_mod = ""
        if settings.output_to_mod:
            args.append(f'RedirectFiles="{settings.output_path}"')
            output_mod = os.path.basename(os.path.normpath(settings.output_path))
            if settings.logs_to_mod:
                log_mod = os.path.basename(os.path.normpath(settings.logs_path))
        args.append("InstantExecute=1")

        if output_mod:
            # the VFS isn't made for a mod that changes while a program runs on
            # it: FNIS writes to its output mod through RedirectFiles instead
            mod_list.setActive(output_mod, False)
        if log_mod:
            mod_list.setActive(log_mod, True)
        try:
            handle = self._organizer.startApplication(
                settings.executable, args,
                forcedCustomOverwrite=log_mod, ignoreCustomOverwrite=not log_mod,
            )
            if handle in (0, mobase.INVALID_HANDLE_VALUE):
                QMessageBox.warning(self._parent, tr("FNIS did not start"),
                                    tr("MO2 could not start FNIS. MO2's log has the details."))
                return
            # the refresh after the run also tells the other plugins FNIS ran
            # (MO2's FNIS check keeps track of it)
            self._organizer.waitForApplication(handle)
        finally:
            if output_mod:
                mod_list.setActive(output_mod, True)
                # a refresh that saves the list first shows FNIS's files in the
                # mod that is on again
                self._organizer.refresh(True)


def createPlugin() -> mobase.IPlugin:
    return FNISTool()
