"""The BAIN wizard installer's window, in MO2 Revamped's design.

The interpreter runs the archive's wizard.txt; this window shows each page the
script stops at: a question with its options, the versions the package asks
for, and at the end what will be installed. installer.py reads the result
through the same methods MO2 2.5.2's window had (name, subpackages, plugins,
renames, tweaks, selectedOptions, isManualRequested).
"""

from __future__ import annotations

import html
import re
from collections.abc import Sequence
from pathlib import Path
from typing import Any, cast

from antlr4 import ParserRuleContext
from PyQt6.QtCore import QRectF, QSize, Qt, QTimer, pyqtSignal
from PyQt6.QtGui import (
    QFontDatabase,
    QImageReader,
    QKeySequence,
    QPainter,
    QPainterPath,
    QPixmap,
    QShortcut,
)
from PyQt6.QtWidgets import (
    QAbstractItemView,
    QApplication,
    QComboBox,
    QDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListView,
    QListWidget,
    QListWidgetItem,
    QPlainTextEdit,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QStackedWidget,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

from wizard.contexts import (
    WizardInterpreterContext,
    WizardRequireVersionsContext,
    WizardSelectContext,
    WizardSelectManyContext,
    WizardSelectOneContext,
    WizardTerminationContext,
    WizardTopLevelContext,
)
from wizard.errors import WizardError
from wizard.interpreter import WizardInterpreter
from wizard.manager import SelectOption
from wizard.runner import WizardRunnerKeywordVisitor, WizardRunnerState
from wizard.tweaks import WizardINISetting
from wizard.value import Plugin

import mobase
import revamped_fluent as fluent

from .utils import make_ini_tweaks

WizardRunnerContext = WizardInterpreterContext[WizardRunnerState, Any]

GLYPH_OK = 0xE73E
GLYPH_NO = 0xE711
GLYPH_UNKNOWN = 0xE9CE
GLYPH_GAME = 0xE7FC
GLYPH_EXTENDER = 0xE943
GLYPH_GRAPHICS = 0xE790
GLYPH_TOOL = 0xE90F


def tr(text: str) -> str:
    return QApplication.translate("RevampedBAINWizard", text)


def check_version(
    context: WizardRequireVersionsContext[WizardRunnerState],
    organizer: mobase.IOrganizer,
) -> tuple[bool, bool, bool, bool]:
    """Whether the game, the script extender, the graphics extender and Wrye
    Bash meet the package's versions. MO2 can't check the graphics extender
    (False when one is asked for) and has no Wrye Bash (always True)."""
    game = organizer.managedGame()

    game_ok = True
    if context.game_version:
        game_ok = mobase.VersionInfo(context.game_version) <= game.version()

    se_ok = True
    if context.script_extender_version:
        se = organizer.gameFeatures().gameFeature(mobase.ScriptExtender)
        se_ok = bool(
            se
            and se.isInstalled()
            and mobase.VersionInfo(context.script_extender_version)
            <= mobase.VersionInfo(se.getExtenderVersion())
        )

    ge_ok = not context.graphics_extender_version
    return (game_ok, se_ok, ge_ok, True)


def arg(text: str, *values: str) -> str:
    """Qt's QString::arg for translated text: %1, %2… replaced in one pass,
    so a value that holds "%2" stays as it is."""
    return re.sub(r"%(\d)", lambda m: values[int(m.group(1)) - 1]
                  if 0 < int(m.group(1)) <= len(values) else m.group(0), text)


def _plain_label(parent: QWidget, object_name: str = "") -> QLabel:
    """A label for the script's own text: never read as HTML."""
    label = QLabel(parent)
    if object_name:
        label.setObjectName(object_name)
    label.setTextFormat(Qt.TextFormat.PlainText)
    label.setWordWrap(True)
    return label


# the biggest an option's picture is kept in memory: larger pictures are
# read at this size, once
IMAGE_LIMIT = 1600


def load_image(path: Path, cache: dict[Path, QPixmap]) -> QPixmap:
    if path not in cache:
        reader = QImageReader(path.as_posix())
        reader.setAutoTransform(True)
        size = reader.size()
        if size.isValid() and max(size.width(), size.height()) > IMAGE_LIMIT:
            reader.setScaledSize(size.scaled(IMAGE_LIMIT, IMAGE_LIMIT, Qt.AspectRatioMode.KeepAspectRatio))
        cache[path] = QPixmap.fromImage(reader.read())
    return cache[path]


def _read_only_text(parent: QWidget, name: str) -> QTextBrowser:
    text = QTextBrowser(parent)
    text.setAccessibleName(name)
    text.setOpenLinks(False)
    text.setFrameShape(QFrame.Shape.NoFrame)
    return text


class ImageView(QWidget):
    """An option's picture, scaled to fit with its proportions and rounded
    corners. It paints the picture instead of holding it in a QLabel, so a
    large picture never makes the window grow."""

    def __init__(self, parent: QWidget):
        super().__init__(parent)
        self._pixmap = QPixmap()
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumHeight(120)

    def setPixmap(self, pixmap: QPixmap) -> None:
        self._pixmap = pixmap
        self.setVisible(not pixmap.isNull())
        self.update()

    def sizeHint(self) -> QSize:
        return QSize(320, 240)

    def paintEvent(self, event) -> None:
        if self._pixmap.isNull():
            return
        size = self._pixmap.size().scaled(self.size(), Qt.AspectRatioMode.KeepAspectRatio)
        target = QRectF(
            (self.width() - size.width()) / 2,
            (self.height() - size.height()) / 2,
            size.width(),
            size.height(),
        )
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        painter.setRenderHint(QPainter.RenderHint.SmoothPixmapTransform)
        clip = QPainterPath()
        clip.addRoundedRect(target, 6, 6)
        painter.setClipPath(clip)
        painter.drawPixmap(target, self._pixmap, QRectF(self._pixmap.rect()))
        painter.end()


class Page(QWidget):
    """A page of the wizard: its title and text above it, and the name of
    the button that goes on from it ("" when nothing follows)."""

    title = ""
    text = ""

    def nextText(self) -> str:
        return tr("Next")

    def canGoOn(self) -> bool:
        return True

    def focusChain(self) -> list[QWidget]:
        """The page's widgets in Tab order."""
        return []

    def summary(self) -> str:
        """What a screen reader hears when the page opens."""
        return "\n".join(t for t in (self.title, self.text) if t)


class SelectPage(Page):
    """A question of the script: its options in a list (check boxes when
    several can be chosen), and the chosen option's picture and description."""

    # a double click on an option of a one-choice question goes on
    itemDoubleClicked = pyqtSignal()

    context: WizardSelectContext[WizardRunnerState, Any]

    def __init__(
        self,
        context: WizardSelectContext[WizardRunnerState, Any],
        images: dict[Path, Path],
        previous: Sequence[str] | None,
        parent: QWidget,
        image_cache: dict[Path, QPixmap] | None = None,
    ):
        super().__init__(parent)
        self._images = images
        self._image_cache = image_cache if image_cache is not None else {}
        self._many = isinstance(context, WizardSelectManyContext)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)

        self.options = QListWidget(self)
        self.options.setObjectName("bainOptions")
        self.options.setSelectionMode(QAbstractItemView.SelectionMode.SingleSelection)
        self.options.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self.options.setWordWrap(True)
        # long names wrap at the list's width instead of being cut
        self.options.setResizeMode(QListView.ResizeMode.Adjust)
        self.options.setUniformItemSizes(False)
        self.options.setMinimumWidth(260)
        layout.addWidget(self.options, 2)

        details = fluent.card(parent=self)
        details_layout = cast(QVBoxLayout, details.layout())
        details_layout.setContentsMargins(16, 14, 16, 14)
        details_layout.setSpacing(10)
        self.image = ImageView(details)
        details_layout.addWidget(self.image, 3)
        self.optionName = _plain_label(details, "instanceDetailName")
        details_layout.addWidget(self.optionName)
        self.description = _read_only_text(details, tr("Option description"))
        details_layout.addWidget(self.description, 2)
        layout.addWidget(details, 3)

        for _ in context.options:
            self.options.addItem(QListWidgetItem())
        self.update_context(context)

        # what was chosen the last time this mod was installed, else the
        # script's defaults
        if previous:
            chosen = [option for option in context.options if option.name in previous]
        elif isinstance(context, WizardSelectManyContext):
            chosen = list(context.defaults)
        elif isinstance(context, WizardSelectOneContext):
            chosen = [context.default]
        else:
            chosen = []

        current = -1
        for row, option in enumerate(context.options):
            item = self.options.item(row)
            assert item is not None
            if self._many:
                item.setFlags(item.flags() | Qt.ItemFlag.ItemIsUserCheckable)
                item.setCheckState(
                    Qt.CheckState.Checked if option in chosen else Qt.CheckState.Unchecked
                )
            elif option in chosen and current < 0:
                current = row
        self.options.setCurrentRow(max(current, 0))

        self.options.currentItemChanged.connect(self._show_option)
        # assistive tools select an option without making it current: the
        # current one follows the selection, which is what Next reads
        self.options.itemSelectionChanged.connect(self._follow_selection)
        self._show_option(self.options.currentItem())
        if not self._many:
            self.options.itemDoubleClicked.connect(lambda _item: self.itemDoubleClicked.emit())

    def update_context(self, context: WizardSelectContext[WizardRunnerState, Any]) -> None:
        """The same question asked again after Back: the script may have
        changed what its options say."""
        self.context = context
        assert len(context.options) == self.options.count()
        self.title = tr("Choose any of these") if self._many else tr("Choose one")
        self.text = context.description
        self.options.setAccessibleName(context.description or self.title)
        for row, option in enumerate(context.options):
            item = self.options.item(row)
            assert item is not None
            item.setText(option.name)
            item.setToolTip(option.name)
            item.setData(Qt.ItemDataRole.UserRole, option)
        if self.options.currentItem() is None and self.options.count():
            self.options.setCurrentRow(0)
        self._show_option(self.options.currentItem())

    def _follow_selection(self) -> None:
        selected = self.options.selectedItems()
        if selected and selected[0] is not self.options.currentItem():
            self.options.setCurrentItem(selected[0])

    def _show_option(self, item: QListWidgetItem | None, _previous: Any = None) -> None:
        option: SelectOption | None = item.data(Qt.ItemDataRole.UserRole) if item else None
        self.optionName.setText(option.name if option else "")
        self.description.setPlainText(option.description if option else "")
        pixmap = QPixmap()
        if option and option.image:
            path = self._images.get(Path(option.image))
            if path is not None:
                pixmap = load_image(path, self._image_cache)
        self.image.setPixmap(pixmap)
        self.image.setAccessibleName(arg(tr("Picture of %1"), option.name) if option else "")

    def selectedOptions(self) -> list[SelectOption]:
        if not self._many:
            item = self.options.currentItem()
            return [item.data(Qt.ItemDataRole.UserRole)] if item else []
        chosen: list[SelectOption] = []
        for row in range(self.options.count()):
            item = self.options.item(row)
            assert item is not None
            if item.checkState() == Qt.CheckState.Checked:
                chosen.append(item.data(Qt.ItemDataRole.UserRole))
        return chosen

    def selected(self) -> WizardSelectContext[WizardRunnerState, Any]:
        if isinstance(self.context, WizardSelectOneContext):
            return self.context.select(self.selectedOptions()[0])
        if isinstance(self.context, WizardSelectManyContext):
            return self.context.select(self.selectedOptions())
        return self.context

    def canGoOn(self) -> bool:
        return self._many or self.options.currentItem() is not None

    def focusChain(self) -> list[QWidget]:
        return [self.options, self.description]


class RequiresPage(Page):
    """The versions the package asks for, against the ones MO2 found."""

    title = ""

    def __init__(
        self,
        context: WizardRequireVersionsContext[WizardRunnerState],
        organizer: mobase.IOrganizer,
        parent: QWidget,
    ):
        super().__init__(parent)
        self.context = context
        self.title = tr("Check this package's requirements")
        self.text = tr(
            "Some versions below are older than needed, missing, or can't be checked "
            "by MO2. The mod may not work; you can install it anyway."
        )
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        requirements = fluent.card(tr("Requirements"), self)
        layout.addWidget(requirements)
        layout.addStretch(1)
        self._rows: list[str] = []

        game = organizer.managedGame()
        game_ok, se_ok, _, _ = check_version(context, organizer)

        # a version MO2 can't read (None) is shown as one it can't check
        game_have: str | None = game.version().canonicalString()
        if not game_have or set(game_have) <= {"0", "."}:
            game_have = None
        se = organizer.gameFeatures().gameFeature(mobase.ScriptExtender)
        se_have: str | None
        if se is None:
            se_have = None
        elif not se.isInstalled():
            se_have = ""
        else:
            se_have = se.getExtenderVersion() or None

        def row(glyph: int, name: str, needed: str, have: str | None, ok: bool):
            if not needed:
                return
            state: bool | None = True if ok else (None if have is None else False)
            mark = QLabel(chr(GLYPH_OK if state else GLYPH_UNKNOWN if state is None else GLYPH_NO))
            mark.setFont(fluent.glyph_font(16))
            if have is None:
                description = arg(tr("Needs %1. MO2 can't check this one."), needed)
            elif have:
                description = arg(tr("Needs %1. Installed: %2."), needed, have)
            else:
                description = arg(tr("Needs %1. Not installed."), needed)
            setting = fluent.SettingRow(requirements, glyph, name, description, [mark])
            verdict = tr("Met") if state else tr("Can't check") if state is None else tr("Not met")
            mark.setAccessibleName(verdict)
            if state is False:
                setting.setProblem(tr("Older than needed") if have else tr("Missing"))
            self._rows.append(f"{name}: {description} {verdict}.")

        row(GLYPH_GAME, game.gameName(), context.game_version, game_have, game_ok)
        row(GLYPH_EXTENDER, tr("Script extender"), context.script_extender_version, se_have, se_ok)
        row(GLYPH_GRAPHICS, tr("Graphics extender"), context.graphics_extender_version, None, False)
        row(GLYPH_TOOL, tr("Wrye Bash"), context.wrye_bash_version, None, False)

    def nextText(self) -> str:
        return tr("Continue anyway")

    def summary(self) -> str:
        return "\n".join([super().summary(), *self._rows])


def _list(parent: QWidget, name: str) -> QListWidget:
    items = QListWidget(parent)
    items.setAccessibleName(name)
    items.setSelectionMode(QAbstractItemView.SelectionMode.NoSelection)
    items.setFocusPolicy(Qt.FocusPolicy.TabFocus)
    items.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    items.setFrameShape(QFrame.Shape.NoFrame)
    items.setIconSize(QSize(16, 16))
    return items


class CompletePage(Page):
    """What the script chose: the sub-packages, which plugins start enabled,
    the INI tweaks it writes and its notes."""

    def __init__(self, context: WizardTerminationContext[WizardRunnerState], parent: QWidget):
        super().__init__(parent)
        self.context = context
        self.state = context.state
        kvisitor = cast(WizardRunnerKeywordVisitor, context.factory.kvisitor)

        # the plugins of the chosen sub-packages, under their new names
        self._subpackages = [sp.name for sp in kvisitor.subpackages if sp.name in self.state.subpackages]
        plugins: set[Plugin] = set()
        for sp in kvisitor.subpackages:
            if sp.name in self.state.subpackages:
                plugins.update(kvisitor.plugins_for(sp))
        # whether a plugin starts enabled is kept under its original name:
        # MO2 2.5.2 looked up the new name and left renamed plugins out
        # and a plugin renamed onto another's name replaces it (two passes, so
        # the result doesn't depend on the set's order)
        enabled = {p: p in self.state.plugins for p in plugins if p not in self.state.renames}
        for p in sorted(plugins):
            if p in self.state.renames:
                enabled[Plugin(self.state.renames[p])] = p in self.state.plugins
        self._plugins = {p.name: enabled[p] for p in sorted(enabled)}
        self._tweaks = (
            {file.replace("\\", "/"): self.state.tweaks.tweaks(file) for file in self.state.tweaks.files()}
            if self.state.tweaks
            else {}
        )

        self.title = tr("Ready to install")
        self.text = (
            tr("These are the parts of the package the script chose.")
            if self._subpackages
            else tr("The script chose no sub-packages, so there is nothing to install. "
                    "Go back and choose other options, or install manually.")
        )

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(16)
        left = QVBoxLayout()
        left.setSpacing(12)
        layout.addLayout(left, 1)

        self._focus: list[QWidget] = []
        if self._subpackages:
            packages = fluent.card(tr("Sub-packages"), self)
            package_list = _list(packages, tr("Sub-packages"))
            for name in self._subpackages:
                package_list.addItem(QListWidgetItem(fluent.glyph_icon(fluent.GLYPH_FOLDER), name))
            packages.layout().addWidget(package_list)
            left.addWidget(packages, 1)
            self._focus.append(package_list)

        if self._plugins:
            plugin_card = fluent.card(tr("Plugins"), self)
            plugin_list = _list(plugin_card, tr("Plugins"))
            self._focus.append(plugin_list)
            for name, enabled in self._plugins.items():
                item = QListWidgetItem(
                    fluent.glyph_icon(GLYPH_OK if enabled else GLYPH_NO, muted=not enabled),
                    name if enabled else arg(tr("%1 (left in optional)"), name),
                )
                item.setToolTip(
                    tr("Installed and ready to enable") if enabled
                    else tr("Goes into the mod's optional folder; the game won't see it")
                )
                plugin_list.addItem(item)
            plugin_card.layout().addWidget(plugin_list)
            left.addWidget(plugin_card, 1)

        if not self._subpackages and not self._plugins:
            left.addStretch(1)
        if self._tweaks or self.state.notes:
            right = QVBoxLayout()
            right.setSpacing(12)
            layout.addLayout(right, 1)
            if self.state.notes:
                notes_card = fluent.card(tr("Notes"), self)
                notes = _read_only_text(notes_card, tr("Notes"))
                notes.setContentsMargins(12, 0, 12, 8)
                document = notes.document()
                assert document is not None
                document.setIndentWidth(12)
                # the script's notes as they are: not read as Markdown or HTML
                notes.setHtml("<ul>" + "".join(
                    f"<li>{html.escape(str(note)).replace(chr(10), '<br>')}</li>"
                    for note in self.state.notes) + "</ul>")
                notes_card.layout().addWidget(notes)
                self._focus.append(notes)
                right.addWidget(notes_card, 1)
            if self._tweaks:
                tweaks_card = fluent.card(tr("INI tweaks"), self)
                files = QComboBox(tweaks_card)
                files.setAccessibleName(tr("INI file"))
                preview = QPlainTextEdit(tweaks_card)
                preview.setReadOnly(True)
                preview.setAccessibleName(tr("INI tweaks"))
                preview.setFont(QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont))
                preview.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
                for file in self._tweaks:
                    files.addItem(file)
                files.currentTextChanged.connect(
                    lambda file: preview.setPlainText(make_ini_tweaks(self._tweaks[file]) if file else "")
                )
                preview.setPlainText(make_ini_tweaks(next(iter(self._tweaks.values()))))
                box = QVBoxLayout()
                box.setContentsMargins(16, 0, 16, 14)
                box.setSpacing(8)
                box.addWidget(files)
                box.addWidget(preview, 1)
                self._focus += [files, preview]
                cast(QVBoxLayout, tweaks_card.layout()).addLayout(box, 1)
                right.addWidget(tweaks_card, 1)

    def nextText(self) -> str:
        return tr("Install")

    def canGoOn(self) -> bool:
        return bool(self._subpackages)

    def focusChain(self) -> list[QWidget]:
        return list(self._focus)

    def subpackages(self) -> list[str]:
        return list(self._subpackages)

    def plugins(self) -> dict[str, bool]:
        return dict(self._plugins)

    def tweaks(self) -> dict[str, list[WizardINISetting]]:
        return dict(self._tweaks)


class StopPage(Page):
    """The script cancelled the installation, or couldn't be run."""

    def __init__(self, title: str, text: str, message: str, parent: QWidget):
        super().__init__(parent)
        self.title = title
        self.text = text
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        box = fluent.card(parent=self)
        details = QPlainTextEdit(box)
        details.setObjectName("bainMessage")
        details.setReadOnly(True)
        details.setAccessibleName(title)
        details.setPlainText(message)
        details.setFrameShape(QFrame.Shape.NoFrame)
        box_layout = cast(QVBoxLayout, box.layout())
        box_layout.setContentsMargins(12, 10, 12, 10)
        box_layout.addWidget(details)
        layout.addWidget(box, 1)
        self._details = details

    def nextText(self) -> str:
        return ""

    def canGoOn(self) -> bool:
        return False

    def focusChain(self) -> list[QWidget]:
        return [self._details]


def cancel_page(context: WizardTerminationContext[WizardRunnerState], parent: QWidget,
                can_go_back: bool) -> StopPage:
    return StopPage(
        tr("The installer stopped"),
        tr("The package's script cancelled the installation and gave this reason. "
           "Go back to choose other options, or install manually.") if can_go_back
        else tr("The package's script cancelled the installation and gave this reason. "
                "You can install the mod manually."),
        context.message() or tr("The script gave no reason."),
        parent,
    )


def error_page(error: WizardError, parent: QWidget, can_go_back: bool) -> StopPage:
    return StopPage(
        tr("The package's script has a mistake"),
        tr("MO2 can't go on with this package's wizard.txt. You can open the script, "
           "go back, or install the mod manually.") if can_go_back
        else tr("MO2 can't run this package's wizard.txt. You can open the script "
                "or install the mod manually."),
        str(error),
        parent,
    )


class WizardInstallerDialog(QDialog):
    """The wizard's window: the mod's name, the page the script is on, and
    Back, Next and Cancel. Back returns to the previous page and its choices."""

    _manual = False

    def __init__(
        self,
        organizer: mobase.IOrganizer,
        interpreter: WizardInterpreter[WizardRunnerState],
        context: WizardTopLevelContext[WizardRunnerState],
        name: mobase.GuessedString,
        images: dict[Path, Path],
        options: dict[str, list[str]],
        parent: QWidget | None,
    ):
        """
        Args:
            organizer: The organizer, for the versions MO2 found.
            interpreter: The interpreter that runs the script.
            context: The script's first context.
            name: The mod's name.
            images: The archive's pictures, by their path in the archive.
            options: The options chosen the last time this mod was installed.
            parent: The parent widget.
        """
        super().__init__(parent)
        self._organizer = organizer
        self._interpreter = interpreter
        self._images = images
        self._options = options
        self._start_context = context
        self._pages: dict[ParserRuleContext, SelectPage] = {}

        self.setObjectName("BainWizardDialog")
        self.setWindowTitle(tr("BAIN Wizard Installer"))
        self.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)
        self.setWindowFlag(Qt.WindowType.WindowMaximizeButtonHint, True)
        self.setMinimumSize(760, 520)
        self.resize(960, 640)
        self.setSizeGripEnabled(True)

        root = QVBoxLayout(self)
        root.setContentsMargins(24, 20, 24, 16)
        root.setSpacing(12)

        header = QHBoxLayout()
        header.setSpacing(24)
        titles = QVBoxLayout()
        titles.setSpacing(4)
        self._title = _plain_label(self)
        self._title.setProperty("wizardPageTitle", True)
        # the page's text is often the script's question, which can run to
        # paragraphs: past a few lines it scrolls instead of pushing the page down
        self._textScroll = QScrollArea(self)
        self._textScroll.setFrameShape(QFrame.Shape.NoFrame)
        self._textScroll.setWidgetResizable(True)
        self._textScroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        self._textScroll.setSizePolicy(QSizePolicy.Policy.Preferred, QSizePolicy.Policy.Fixed)
        self._textScroll.setObjectName("bainPageText")
        self._textScroll.setFocusPolicy(Qt.FocusPolicy.NoFocus)
        self._text = _plain_label(self._textScroll)
        self._text.setProperty("wizardPageText", True)
        self._text.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self._textScroll.setWidget(self._text)
        titles.addWidget(self._title)
        titles.addWidget(self._textScroll)
        header.addLayout(titles, 1)

        name_box = QVBoxLayout()
        name_box.setSpacing(4)
        name_label = QLabel(tr("Mod name"), self)
        self._name = QComboBox(self)
        self._name.setEditable(True)
        self._name.setInsertPolicy(QComboBox.InsertPolicy.NoInsert)
        self._name.setMinimumWidth(300)
        self._name.setAccessibleName(tr("Mod name"))
        name_label.setBuddy(self._name)
        # mobase.GuessedString holds every guess; str() is the likeliest
        for value in name.variants():
            self._name.addItem(value)
        completer = self._name.completer()
        if completer is not None:
            completer.setCaseSensitivity(Qt.CaseSensitivity.CaseSensitive)
        self._name.setEditText(str(name))
        self._name.editTextChanged.connect(lambda _text: self._update_buttons())
        name_box.addWidget(name_label)
        name_box.addWidget(self._name)
        name_box.addStretch(1)
        header.addLayout(name_box)
        root.addLayout(header)

        self._stack = QStackedWidget(self)
        root.addWidget(self._stack, 1)

        footer = QHBoxLayout()
        footer.setSpacing(8)
        self._manualBtn = fluent.content_button(tr("Install manually"))
        self._manualBtn.setToolTip(tr("Close the wizard and choose the files yourself"))
        self._scriptBtn = fluent.content_button(tr("Open script"), fluent.GLYPH_PAGE)
        self._scriptBtn.setToolTip(tr("Open the package's wizard.txt"))
        footer.addWidget(self._manualBtn)
        footer.addWidget(self._scriptBtn)
        footer.addStretch(1)
        self._backBtn = fluent.enter_presses_itself(QPushButton(tr("Back"), self))
        self._backBtn.setAutoDefault(False)
        self._nextBtn = fluent.enter_presses_itself(QPushButton(tr("Next"), self))
        self._nextBtn.setAutoDefault(False)
        self._nextBtn.setDefault(True)
        self._nextBtn.setProperty("fluentPrimary", True)
        cancel = fluent.enter_presses_itself(QPushButton(tr("Cancel"), self))
        cancel.setAutoDefault(False)
        for button in (self._backBtn, self._nextBtn, cancel):
            button.setMinimumWidth(110)
            footer.addWidget(button)
        root.addLayout(footer)

        self._cancelBtn = cancel
        self._image_cache: dict[Path, QPixmap] = {}
        cancel.clicked.connect(self.reject)
        self._manualBtn.clicked.connect(self._manual_clicked)
        self._backBtn.clicked.connect(self.previousClicked)
        self._nextBtn.clicked.connect(self.nextClicked)
        # Backspace goes back as in MO2 2.5.2, except while typing a name
        QShortcut(QKeySequence(Qt.Key.Key_Backspace), self, self._back_key)
        QShortcut(QKeySequence("Alt+Left"), self, self.previousClicked)

    # --- what installer.py reads -------------------------------------------
    @property
    def scriptButtonClicked(self) -> pyqtSignal:
        return self._scriptBtn.clicked  # pyright: ignore[reportReturnType]

    def name(self) -> str:
        return self._name.currentText().strip()

    def _complete(self) -> CompletePage:
        page = self._stack.currentWidget()
        assert isinstance(page, CompletePage)
        return page

    def subpackages(self) -> list[str]:
        """The sub-packages to install; valid once exec() returned Accepted."""
        return self._complete().subpackages()

    def plugins(self) -> dict[str, bool]:
        """Each plugin of those sub-packages, and whether it starts enabled."""
        return self._complete().plugins()

    def renames(self) -> dict[str, str]:
        return {plugin.name: new for plugin, new in self._complete().state.renames.items()}

    def tweaks(self) -> dict[str, list[WizardINISetting]]:
        return self._complete().tweaks()

    def selectedOptions(self) -> dict[str, list[str]]:
        """The options chosen on each question on the way to this page."""
        chosen: dict[str, list[str]] = {}
        for index in range(self._stack.count()):
            page = self._stack.widget(index)
            if isinstance(page, SelectPage):
                chosen[page.context.description] = [o.name for o in page.selectedOptions()]
        return chosen

    def isManualRequested(self) -> bool:
        return self._manual

    # --- moving through the script -----------------------------------------
    def _manual_clicked(self) -> None:
        self._manual = True
        self.reject()

    def _back_key(self) -> None:
        if not isinstance(QApplication.focusWidget(), (QLineEdit, QComboBox)):
            self.previousClicked()

    def previousClicked(self) -> None:
        index = self._stack.currentIndex()
        if index <= 0:
            return
        page = self._stack.widget(index)
        self._stack.removeWidget(page)
        # a question's page is kept for when the script asks it again
        if not isinstance(page, SelectPage):
            page.deleteLater()
        self._show_current()

    def nextClicked(self) -> None:
        page = self._stack.currentWidget()
        if not isinstance(page, Page) or not page.canGoOn():
            return
        if isinstance(page, CompletePage):
            if self.name():
                self.accept()
            return
        try:
            if isinstance(page, SelectPage):
                context = page.selected().exec()
            elif isinstance(page, RequiresPage):
                context = page.context.exec()
            else:
                return
            context = self._exec_until(context)
            cached = self._pages.get(context.context)
            # a question asked again in a loop while its page is still on the
            # way back gets a page of its own, so Back keeps every step
            if cached is not None and self._stack.indexOf(cached) < 0:
                assert isinstance(context, WizardSelectContext)
                cached.update_context(context)
                new_page: Page = cached
            else:
                new_page = self._make_page(context, cache=cached is None)
        except WizardError as error:
            new_page = error_page(error, self, True)
        self._stack.setCurrentIndex(self._stack.addWidget(new_page))
        self._show_current()

    def _exec_until(self, context: WizardRunnerContext) -> WizardRunnerContext:
        context = self._interpreter.exec_until(
            context, (WizardSelectContext, WizardRequireVersionsContext)
        )
        # versions that are all met need no page
        while isinstance(context, WizardRequireVersionsContext) and all(
            check_version(context, self._organizer)
        ):
            context = self._interpreter.exec_until(
                context.exec(), (WizardSelectContext, WizardRequireVersionsContext)
            )
        return context

    def _make_page(self, context: WizardRunnerContext, cache: bool = True) -> Page:
        if isinstance(context, WizardSelectContext):
            page = SelectPage(context, self._images, self._options.get(context.description), self,
                              self._image_cache)
            page.itemDoubleClicked.connect(self.nextClicked)
            page.options.currentItemChanged.connect(lambda *_: self._update_buttons())
            if cache:
                self._pages[context.context] = page  # type: ignore[index]
            return page
        if isinstance(context, WizardRequireVersionsContext):
            return RequiresPage(context, self._organizer, self)
        if isinstance(context, WizardTerminationContext):
            if context.is_cancel():
                return cancel_page(context, self, self._stack.count() > 0)
            return CompletePage(context, self)
        raise NotImplementedError()

    def _fit_text(self) -> None:
        """The page's text takes the lines it needs, up to five; more scroll."""
        if self._text.isHidden():
            return
        width = max(1, self._textScroll.viewport().width())
        needed = self._text.heightForWidth(width)
        limit = self._text.fontMetrics().lineSpacing() * 5
        self._textScroll.setFixedHeight(min(needed, limit) if needed > 0 else limit)

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        # once the layout has given the text its new width
        QTimer.singleShot(0, self._fit_text)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        QTimer.singleShot(0, self._fit_text)

    def _show_current(self) -> None:
        page = self._stack.currentWidget()
        if isinstance(page, Page):
            self._title.setText(page.title)
            self._text.setText(page.text)
            self._textScroll.setVisible(bool(page.text))
            self._textScroll.verticalScrollBar().setValue(0)
            self._fit_text()
        self._update_buttons()

        # Tab goes name, page, then the buttons left to right
        chain: list[QWidget] = [self._name]
        if isinstance(page, Page):
            chain += page.focusChain()
        chain += [self._manualBtn, self._scriptBtn, self._backBtn, self._nextBtn, self._cancelBtn]
        for first, second in zip(chain, chain[1:]):
            QWidget.setTabOrder(first, second)

        # the focus goes to the page's choices, else to the way on; isHidden,
        # not isVisible: the window isn't shown yet for the first page
        if isinstance(page, SelectPage):
            target: QWidget = page.options
        elif not self._nextBtn.isHidden() and self._nextBtn.isEnabled():
            target = self._nextBtn
        elif self._backBtn.isEnabled():
            target = self._backBtn
        else:
            target = self._scriptBtn
        target.setFocus()
        # a screen reader hears the page's title and text with what has the focus
        if isinstance(page, Page):
            summary = page.summary()
            for button in (self._nextBtn, self._backBtn, self._scriptBtn):
                button.setAccessibleDescription(summary if button is target else "")
            if isinstance(page, SelectPage):
                page.options.setAccessibleDescription(summary)

    def _update_buttons(self) -> None:
        page = self._stack.currentWidget()
        self._backBtn.setEnabled(self._stack.currentIndex() > 0)
        if not isinstance(page, Page):
            self._nextBtn.setEnabled(False)
            return
        text = page.nextText()
        self._nextBtn.setVisible(bool(text))
        if text:
            self._nextBtn.setText(text)
        ready = page.canGoOn() and (not isinstance(page, CompletePage) or bool(self.name()))
        self._nextBtn.setEnabled(ready)

    def exec(self) -> int:
        try:
            page = self._make_page(self._exec_until(self._start_context))
        except WizardError as error:
            page = error_page(error, self, False)
        self._stack.addWidget(page)
        self._show_current()
        return super().exec()
