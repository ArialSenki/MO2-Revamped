"""MO2 Revamped's Fluent building blocks for Python plugins.

The themes style these widgets by their object names and properties, the same
ones MO2's own windows use: a content dialog (title, text, the dialog's own
content and a band of buttons), cards of setting rows, switches and glyphs.
MOApplication polishes every dialog when it opens, so nothing here sets colors.
"""

from __future__ import annotations

from typing import Iterable

from PyQt6.QtCore import QEvent, QObject, QRectF, QSize, Qt
from PyQt6.QtGui import QColor, QFont, QFontMetrics, QIcon, QPainter, QPalette, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QListWidget,
    QPushButton,
    QSizePolicy,
    QStyle,
    QStyledItemDelegate,
    QVBoxLayout,
    QWidget,
)

GLYPH_FAMILIES = ["Segoe Fluent Icons", "Segoe MDL2 Assets"]

# Segoe Fluent Icons code points used by the plugins
GLYPH_FOLDER = 0xE8B7
GLYPH_PLAY = 0xE768
GLYPH_PAGE = 0xE7C3
GLYPH_SETTINGS = 0xE713
GLYPH_ADD = 0xE710
GLYPH_FILTER = 0xE71C
GLYPH_WARNING = 0xE7BA
GLYPH_INFO = 0xE946
GLYPH_LIST = 0xEA37


def glyph_font(pixels: int) -> QFont:
    font = QFont()
    font.setFamilies(GLYPH_FAMILIES)
    font.setPixelSize(pixels)
    return font


def glyph_label(glyph: int, parent: QWidget | None = None) -> QLabel:
    """A 16 px glyph at the start of a setting row."""
    label = QLabel(chr(glyph), parent)
    label.setObjectName("instanceRowGlyph")
    label.setFont(glyph_font(16))
    label.setFixedWidth(24)
    label.setAlignment(Qt.AlignmentFlag.AlignCenter)
    label.setAccessibleName("")
    return label


def glyph_icon(glyph: int, muted: bool = False) -> QIcon:
    """A glyph in the window's text color, for buttons."""
    application = QApplication.instance()
    palette = application.palette() if application is not None else None
    color = (
        palette.color(
            QPalette.ColorGroup.Disabled if muted else QPalette.ColorGroup.Active,
            QPalette.ColorRole.WindowText,
        )
        if palette is not None
        else QColor("#808080")
    )
    pixmap = QPixmap(32, 32)
    pixmap.setDevicePixelRatio(2.0)
    pixmap.fill(Qt.GlobalColor.transparent)
    painter = QPainter(pixmap)
    painter.setRenderHint(QPainter.RenderHint.Antialiasing)
    painter.setFont(glyph_font(16))
    painter.setPen(color)
    painter.drawText(QRectF(0, 0, 16, 16), Qt.AlignmentFlag.AlignCenter, chr(glyph))
    painter.end()
    return QIcon(pixmap)


def make_switch(box: QCheckBox, title: str, description: str = "") -> QCheckBox:
    """A check box drawn as a Windows 11 toggle switch; its row names it."""
    box.setText("")
    box.setAccessibleName(title)
    box.setAccessibleDescription(description)
    box.setProperty("fluentSwitch", True)
    box.setCursor(Qt.CursorShape.PointingHandCursor)
    box.style().unpolish(box)
    box.style().polish(box)
    return box


def card(title: str = "", parent: QWidget | None = None) -> QFrame:
    """A card that holds setting rows, with an optional heading."""
    frame = QFrame(parent)
    frame.setProperty("fluentCard", True)
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(0, 10 if title else 0, 0, 0)
    layout.setSpacing(0)
    if title:
        heading = QLabel(title, frame)
        heading.setObjectName("fluentCardTitle")
        heading.setContentsMargins(18, 0, 18, 8)
        layout.addWidget(heading)
    return frame


class _EnterPressesItself(QObject):
    """Enter on a focused button presses that button, as in Windows, not the
    dialog's default one (enterPressesItself() in fluentdialogs.cpp)."""

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if (event.type() == QEvent.Type.KeyPress
                and event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter)
                and isinstance(watched, QPushButton) and watched.isEnabled()):
            watched.click()
            return True
        return False


def enter_presses_itself(button: QPushButton) -> QPushButton:
    button.installEventFilter(_EnterPressesItself(button))
    return button


def content_button(text: str, glyph: int = 0) -> QPushButton:
    """A button inside a dialog's content (Browse…, New…): never the default
    one, and Enter presses it when it has the focus."""
    button = QPushButton(text)
    button.setAutoDefault(False)
    if glyph:
        button.setIcon(glyph_icon(glyph))
    return enter_presses_itself(button)


class SettingRow(QFrame):
    """One row of a card: a glyph, a title, a description, a problem in the
    themes' error tone, and the row's controls on the right."""

    def __init__(self, parent: QFrame, glyph: int, title: str, description: str = "",
                 widgets: Iterable[QWidget] = ()):
        super().__init__(parent)
        self.setObjectName("fluentSettingRow")
        self.setMinimumHeight(62)
        layout = QHBoxLayout(self)
        layout.setContentsMargins(18, 8, 12, 8)
        layout.setSpacing(14)
        layout.addWidget(glyph_label(glyph, self))
        texts = QVBoxLayout()
        texts.setSpacing(2)
        self.title = QLabel(title, self)
        self.title.setObjectName("fluentSettingTitle")
        self.title.setWordWrap(True)
        self.description = QLabel(description, self)
        self.description.setObjectName("fluentSettingDescription")
        self.description.setWordWrap(True)
        self.description.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
        self.description.setVisible(bool(description))
        # the error tone of MO2's name checks (instanceNameValidation)
        self.problem = QLabel("", self)
        self.problem.setObjectName("instanceNameValidation")
        self.problem.setWordWrap(True)
        self.problem.setVisible(False)
        # the row's spare height goes around the text, not between its lines
        texts.addStretch(1)
        texts.addWidget(self.title)
        texts.addWidget(self.description)
        texts.addWidget(self.problem)
        texts.addStretch(1)
        layout.addLayout(texts, 1)
        self._controls = list(widgets)
        for widget in widgets:
            widget.setParent(self)
            if isinstance(widget, QCheckBox):
                make_switch(widget, title, description)
                layout.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignRight)
            else:
                if isinstance(widget, QComboBox):
                    widget.setMinimumWidth(220)
                    widget.setAccessibleName(title)
                layout.addWidget(widget, 0, Qt.AlignmentFlag.AlignVCenter)
            widget.show()
        parent.layout().addWidget(self)

    def setDescription(self, text: str) -> None:
        self.description.setText(text)
        self.description.setVisible(bool(text))

    def setProblem(self, text: str) -> None:
        """What is wrong with the row's choice, or "": screen readers hear it
        on the row's controls too."""
        self.problem.setText(text)
        self.problem.setVisible(bool(text))
        description = "\n".join(t for t in (self.description.text(), text) if t)
        for widget in self._controls:
            widget.setAccessibleDescription(description)


class ContentDialog(QDialog):
    """MO2's Windows 11 content dialog (FluentContentDialog in C++): a title,
    a short text, the dialog's own content, and the buttons side by side and of
    the same width in a band at the bottom. The first primary button is the
    default and has the accent."""

    def __init__(self, parent: QWidget | None, object_name: str, title: str, text: str = ""):
        super().__init__(parent)
        self.setObjectName(object_name)
        self.setProperty("fluentContentDialog", True)
        self.setWindowTitle(title)
        self.setMinimumWidth(440)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        content = QWidget(self)
        content.setObjectName("fluentDialogContent")
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(24, 20, 24, 24)
        content_layout.setSpacing(8)
        title_label = QLabel(title, content)
        title_label.setObjectName("fluentDialogTitle")
        title_label.setWordWrap(True)
        # as written, like the C++ content dialog: titles carry mods' names
        title_label.setTextFormat(Qt.TextFormat.PlainText)
        content_layout.addWidget(title_label)
        self.text = QLabel(text, content)
        self.text.setObjectName("fluentDialogText")
        self.text.setWordWrap(True)
        self.text.setTextFormat(Qt.TextFormat.PlainText)
        self.text.setVisible(bool(text))
        # a screen reader reads the explanation with the window's name
        self.setAccessibleDescription(text)
        content_layout.addWidget(self.text)
        self.body = QVBoxLayout()
        self.body.setContentsMargins(0, 8, 0, 0)
        self.body.setSpacing(8)
        content_layout.addLayout(self.body, 1)
        layout.addWidget(content, 1)

        band = QFrame(self)
        band.setObjectName("fluentDialogFooter")
        self._buttons = QHBoxLayout(band)
        self._buttons.setContentsMargins(24, 24, 24, 24)
        self._buttons.setSpacing(8)
        layout.addWidget(band)
        self._has_default = False

    def addButton(self, text: str, primary: bool = False, default: bool | None = None) -> QPushButton:
        """default: whether Enter anywhere presses it (the primary button's,
        unless it starts something Enter in a field shouldn't)."""
        button = QPushButton(text, self)
        button.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        button.setMinimumWidth(110)
        button.setAutoDefault(False)
        if default is None:
            default = primary
        if default and not self._has_default:
            self._has_default = True
            button.setDefault(True)
        if primary:
            button.setProperty("fluentPrimary", True)
        enter_presses_itself(button)
        self._buttons.addWidget(button)
        return button


def accent_color() -> QColor:
    application = QApplication.instance()
    dark = application is not None and application.palette().window().color().lightness() < 128
    return QColor("#4CC2FF" if dark else "#005FB8")


class NavigationDelegate(QStyledItemDelegate):
    """The page list of MO2's Settings window: a soft fill under the hovered
    and selected page, an accent pill beside the selected one, a glyph (the
    item's UserRole data) and the page's name."""

    ItemHeight = 36

    def sizeHint(self, option, index) -> QSize:
        return QSize(super().sizeHint(option, index).width(), self.ItemHeight + 4)

    def paint(self, painter, option, index) -> None:
        selected = bool(option.state & QStyle.StateFlag.State_Selected)
        hovered = bool(option.state & QStyle.StateFlag.State_MouseOver)
        text = option.palette.color(QPalette.ColorGroup.Active, QPalette.ColorRole.WindowText)
        painter.save()
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        row = QRectF(option.rect).adjusted(2, 2, -2, -2)
        alpha = 26 if selected else (15 if hovered else 0)
        if alpha:
            fill = QColor(text)
            fill.setAlpha(alpha)
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(fill)
            painter.drawRoundedRect(row, 4, 4)
        if selected:
            painter.setPen(Qt.PenStyle.NoPen)
            painter.setBrush(accent_color())
            painter.drawRoundedRect(QRectF(row.left(), row.center().y() - 8, 3, 16), 1.5, 1.5)
        left = row.left() + 14
        glyph = index.data(Qt.ItemDataRole.UserRole)
        if glyph:
            painter.setFont(glyph_font(16))
            painter.setPen(text)
            painter.drawText(QRectF(left, row.top(), 20, row.height()), Qt.AlignmentFlag.AlignCenter, str(glyph))
            left += 32
        label = QFont(option.font)
        label.setWeight(QFont.Weight.DemiBold if selected else QFont.Weight.Normal)
        painter.setFont(label)
        painter.setPen(text)
        text_rect = QRectF(left, row.top(), row.right() - 8 - left, row.height())
        painter.drawText(text_rect, Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft,
                         QFontMetrics(label).elidedText(str(index.data() or ""), Qt.TextElideMode.ElideRight,
                                                         int(text_rect.width())))
        painter.restore()


def navigation_list(parent: QWidget | None = None, width: int = 232) -> QListWidget:
    nav = QListWidget(parent)
    nav.setObjectName("fluentNav")
    nav.setItemDelegate(NavigationDelegate(nav))
    nav.setFrameShape(QFrame.Shape.NoFrame)
    nav.setMouseTracking(True)
    nav.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
    nav.setFixedWidth(width)
    return nav


def note(text: str, parent: QWidget | None = None) -> QLabel:
    """Secondary text under a list or a card."""
    label = QLabel(text, parent)
    label.setObjectName("fluentSettingDescription")
    label.setWordWrap(True)
    return label
