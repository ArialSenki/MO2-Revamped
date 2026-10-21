"""MO2 Revamped help, credits, and runtime information."""

from __future__ import annotations

import os
import platform
import sys
import tempfile
import traceback
import zipfile
from datetime import datetime, timezone
from pathlib import Path

import mobase
from PyQt6 import QtCore
from PyQt6.QtCore import QCoreApplication, Qt, qCritical
from PyQt6.QtGui import QAction, QDesktopServices, QIcon, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMenu,
    QMessageBox,
    QPushButton,
    QTabWidget,
    QTextBrowser,
    QVBoxLayout,
    QWidget,
)

# MO2 installs translations/revamped_<language>.qm; the source text is English.
_TR_CONTEXT = "Mo2RevampedInfo"


def _tr(text: str) -> str:
    return QCoreApplication.translate(_TR_CONTEXT, text)


class Mo2RevampedInfo(mobase.IPluginTool):
    """Add an About entry and clearer help for MO2's existing toolbar buttons."""

    def __init__(self):
        super().__init__()
        self._organizer = None
        self._parent_widget = None
        self._about_action = None
        self._diagnostics_action = None

    def init(self, organizer: mobase.IOrganizer) -> bool:
        self._organizer = organizer
        organizer.onUserInterfaceInitialized(self._initialize_main_window)
        return True

    def name(self) -> str:
        return "MO2 Revamped Information"

    def localizedName(self) -> str:
        return self.name()

    def author(self) -> str:
        return "ArialSenki"

    def description(self) -> str:
        return _tr("About, credits, component versions, and clearer help for MO2 Revamped.")

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, 0)

    def requirements(self):
        return []

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return _tr("About MO2 Revamped")

    def tooltip(self) -> str:
        return _tr("View MO2 Revamped features, component versions, credits, and licenses.")

    def icon(self) -> QIcon:
        return QIcon()

    def setParentWidget(self, widget) -> None:
        self._parent_widget = widget

    def display(self) -> None:
        self._show_about(self._parent_widget)

    def _initialize_main_window(self, main_window) -> None:
        self._parent_widget = main_window
        for object_name, tooltip, whats_this in (
            (
                "actionNexus",
                _tr("Nexus Mods: open the website to browse mods for the active game."),
                _tr("Opens Nexus Mods in your browser. The content depends on the active game and your account."),
            ),
            (
                "actionNotifications",
                _tr("MO2 notifications: review detected errors and setup recommendations."),
                _tr("Opens MO2 notifications. The icon can be highlighted when there are problems worth reviewing."),
            ),
            (
                "actionEndorseMO",
                _tr("Support MO2 Revamped on Nexus Mods."),
                _tr("Opens the options to endorse or not endorse MO2 Revamped on its Nexus Mods page."),
            ),
            (
                "actionHelp",
                _tr("MO2 help: guides and support links. Use \"About MO2 Revamped\" to see this edition, its components and credits."),
                _tr("Opens the help options. \"About MO2 Revamped\" describes this edition, its features, components and licenses."),
            ),
        ):
            action = main_window.findChild(QAction, object_name)
            if action is not None:
                action.setToolTip(tooltip)
                action.setStatusTip(tooltip)
                action.setWhatsThis(whats_this)

        help_menu = main_window.findChild(QMenu, "menuHelp")
        if help_menu is None:
            return
        about_found = False
        diagnostics_found = False
        for action in help_menu.actions():
            if action.objectName() == "mo2RevampedAboutAction":
                self._about_action = action
                about_found = True
            elif action.objectName() == "mo2RevampedDiagnosticsAction":
                self._diagnostics_action = action
                diagnostics_found = True
        if not about_found:
            help_menu.addSeparator()
            self._about_action = QAction(_tr("About MO2 Revamped…"), main_window)
            self._about_action.setObjectName("mo2RevampedAboutAction")
            self._about_action.setToolTip(
                _tr("MO2 Revamped information, features, components and credits.")
            )
            self._about_action.triggered.connect(
                lambda _checked=False: self._show_about(main_window)
            )
            help_menu.addAction(self._about_action)
        if not diagnostics_found:
            self._diagnostics_action = QAction(
                _tr("Create MO2 Revamped bug report…"), main_window
            )
            self._diagnostics_action.setObjectName("mo2RevampedDiagnosticsAction")
            self._diagnostics_action.setToolTip(
                _tr("Saves a report with the MO2, USVFS and Elden Ring bridge logs.")
            )
            self._diagnostics_action.triggered.connect(
                lambda _checked=False: self._create_diagnostics_bundle(main_window)
            )
            help_menu.addAction(self._diagnostics_action)

    def _context_text(self) -> str:
        game_name = _tr("Not detected")
        profile_name = _tr("Not selected")
        try:
            game = self._organizer.managedGame()
            if game is not None:
                game_name = game.gameName()
        except Exception:
            pass
        try:
            profile = self._organizer.profile()
            if profile is not None:
                profile_name = profile.name()
        except Exception:
            pass
        return _tr("Active game: {game}<br>Active profile: {profile}").format(
            game=game_name, profile=profile_name
        )

    @staticmethod
    def _browser(html: str, parent) -> QTextBrowser:
        browser = QTextBrowser(parent)
        browser.setOpenExternalLinks(False)
        browser.setOpenLinks(False)
        browser.setReadOnly(True)
        browser.setHtml(html)
        browser.anchorClicked.connect(QDesktopServices.openUrl)
        return browser

    @staticmethod
    def _about_page_intro(title: str, description: str, parent: QWidget) -> QFrame:
        intro = QFrame(parent)
        intro.setObjectName("mo2RevampedAboutPageIntro")
        intro_layout = QVBoxLayout(intro)
        intro_layout.setContentsMargins(14, 10, 14, 10)
        intro_layout.setSpacing(3)

        heading = QLabel(title, intro)
        heading.setObjectName("mo2RevampedAboutPageTitle")
        intro_layout.addWidget(heading)

        text = QLabel(description, intro)
        text.setObjectName("mo2RevampedAboutPageDescription")
        text.setWordWrap(True)
        intro_layout.addWidget(text)
        return intro

    @staticmethod
    def _about_feature(title: str, description: str, parent: QWidget) -> QFrame:
        card = QFrame(parent)
        card.setObjectName("mo2RevampedAboutFeatureCard")
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(13, 11, 13, 11)
        card_layout.setSpacing(5)

        heading = QLabel(title, card)
        heading.setObjectName("mo2RevampedAboutFeatureTitle")
        heading.setWordWrap(True)
        card_layout.addWidget(heading)

        text = QLabel(description, card)
        text.setObjectName("mo2RevampedAboutFeatureText")
        text.setWordWrap(True)
        # the text starts under the title instead of floating in the card
        text.setAlignment(Qt.AlignmentFlag.AlignLeft | Qt.AlignmentFlag.AlignTop)
        card_layout.addWidget(text, 1)
        return card

    def _show_about(self, parent) -> None:
        dialog = QDialog(parent)
        dialog.setObjectName("MO2RevampedAboutDialog")
        dialog.setWindowTitle(_tr("About MO2 Revamped"))
        dialog.setMinimumSize(820, 620)
        dialog.resize(920, 700)
        dialog.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)

        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(16, 16, 16, 14)
        layout.setSpacing(12)

        header_card = QFrame(dialog)
        header_card.setObjectName("mo2RevampedAboutHeaderCard")
        header_layout = QHBoxLayout(header_card)
        header_layout.setContentsMargins(14, 12, 16, 12)
        header_layout.setSpacing(14)

        logo = QLabel(header_card)
        logo.setObjectName("mo2RevampedAboutLogo")
        logo.setFixedSize(62, 62)
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        try:
            # the large logo built into ModOrganizer.exe first, so it always
            # matches the program; the loose files are older builds' fallback
            pixmap = QPixmap(":/MO/gui/app_icon")
            app_dir = QApplication.applicationDirPath()
            if pixmap.isNull():
                pixmap = QPixmap(os.path.join(app_dir, "resources", "mo_icon.png"))
            if pixmap.isNull():
                pixmap = QPixmap(os.path.join(app_dir, "splash.png"))
            if not pixmap.isNull():
                logo.setPixmap(pixmap.scaled(
                    42,
                    42,
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                ))
        except Exception:
            pass
        header_layout.addWidget(logo)

        header_text = QWidget(header_card)
        header_text.setObjectName("mo2RevampedAboutHeaderText")
        header_text_layout = QVBoxLayout(header_text)
        header_text_layout.setContentsMargins(0, 0, 0, 0)
        header_text_layout.setSpacing(3)

        title = QLabel("Mod Organizer 2: Revamped", header_text)
        title.setObjectName("mo2RevampedAboutHeaderTitle")
        header_text_layout.addWidget(title)

        subtitle = QLabel(
            _tr("A community edition with interface improvements and tools "
                "specialized for Elden Ring."),
            header_text,
        )
        subtitle.setObjectName("mo2RevampedAboutHeaderSubtitle")
        subtitle.setWordWrap(True)
        header_text_layout.addWidget(subtitle)

        metadata = QHBoxLayout()
        metadata.setContentsMargins(0, 2, 0, 0)
        metadata.setSpacing(14)
        edition = QLabel(_tr("Revamped edition  ·  ArialSenki"), header_text)
        edition.setObjectName("mo2RevampedAboutHeaderMeta")
        metadata.addWidget(edition)

        context = QLabel(
            self._context_text().replace("<br>", "  ·  "), header_text
        )
        context.setObjectName("mo2RevampedAboutHeaderContext")
        context.setTextFormat(Qt.TextFormat.PlainText)
        context.setWordWrap(True)
        metadata.addWidget(context, 1)
        header_text_layout.addLayout(metadata)
        header_layout.addWidget(header_text, 1)
        layout.addWidget(header_card)

        tabs = QTabWidget(dialog)
        tabs.setObjectName("mo2RevampedAboutTabs")
        overview_page = QWidget(dialog)
        overview_page.setObjectName("mo2RevampedAboutOverviewPage")
        overview_layout = QVBoxLayout(overview_page)
        overview_layout.setContentsMargins(14, 14, 14, 12)
        overview_layout.setSpacing(11)

        overview_layout.addWidget(
            self._about_page_intro(
                _tr("What Revamped adds"),
                _tr("Keeps the Mod Organizer 2 workflow and adds tools to install "
                    "mods, separate setups and recognize each environment more easily."),
                overview_page,
            )
        )

        feature_grid = QGridLayout()
        feature_grid.setContentsMargins(0, 0, 0, 0)
        feature_grid.setHorizontalSpacing(10)
        feature_grid.setVerticalSpacing(10)
        feature_grid.addWidget(
            self._about_feature(
                _tr("Mod installation"),
                _tr("Detects Elden Ring paths and recognizes nested folders to "
                    "install mods and native add-ons."),
                overview_page,
            ),
            0,
            0,
        )
        feature_grid.addWidget(
            self._about_feature(
                _tr("Profiles and saves"),
                _tr("Organizes profiles and, for Elden Ring, can route saves per "
                    "profile."),
                overview_page,
            ),
            0,
            1,
        )
        feature_grid.addWidget(
            self._about_feature(
                _tr("Independent instances"),
                _tr("Choose global or portable setups. You can also create an "
                    "isolated MO2 copy for Elden Ring."),
                overview_page,
            ),
            1,
            0,
        )
        feature_grid.addWidget(
            self._about_feature(
                _tr("Interface and diagnostics"),
                _tr("Includes Windows 11 style themes (Light, Dark and Dark Black) "
                    "with Fluent icons, plus tools to prepare diagnostic reports."),
                overview_page,
            ),
            1,
            1,
        )
        # cards keep their natural height; spare room goes below them
        overview_layout.addLayout(feature_grid)
        overview_layout.addStretch(1)

        compatibility_note = QLabel(
            _tr("Elden Ring-specific features are only enabled in an instance "
                "for that game."),
            overview_page,
        )
        compatibility_note.setObjectName("mo2RevampedAboutCompatibilityNote")
        compatibility_note.setWordWrap(True)
        overview_layout.addWidget(compatibility_note)
        tabs.addTab(overview_page, _tr("Revamped"))
        components_page = QWidget(dialog)
        components_page.setObjectName("mo2RevampedAboutComponentsPage")
        components_layout = QVBoxLayout(components_page)
        components_layout.setContentsMargins(14, 14, 14, 12)
        components_layout.setSpacing(11)

        components_layout.addWidget(
            self._about_page_intro(
                _tr("Included components"),
                _tr("Versions and tools that are part of this copy of MO2 Revamped."),
                components_page,
            )
        )

        components_grid = QGridLayout()
        components_grid.setContentsMargins(0, 0, 0, 0)
        components_grid.setHorizontalSpacing(10)
        components_grid.setVerticalSpacing(10)
        components_grid.addWidget(
            self._about_feature(
                _tr("MO2 base"),
                _tr("Mod Organizer 2 · 2.5.2\nQt and PyQt · 6.7.1\nPython · 3.12.15\nOpenSSL · 3.5.9"),
                components_page,
            ),
            0,
            0,
        )
        components_grid.addWidget(
            self._about_feature(
                _tr("Virtual file system"),
                "USVFS · 0.5.7.2\n"
                + _tr("Deploys mods virtually without changing the game's "
                      "original files."),
                components_page,
            ),
            0,
            1,
        )
        components_grid.addWidget(
            self._about_feature(
                _tr("Elden Ring tools"),
                _tr("Game plugin · {plugin}\nNative bridge · {bridge}\n"
                    "Archive installer · {installer}").format(
                    plugin="0.5.0-alpha.75", bridge="0.5.0-alpha.35", installer="alpha.72"
                ),
                components_page,
            ),
            1,
            0,
        )
        components_grid.addWidget(
            self._about_feature(
                _tr("Profiles and interface"),
                _tr("Save isolation · {saves}\nStartup options · {startup}\n"
                    "Fluent themes · Light, Dark and Dark Black").format(
                    saves="alpha.76", startup="0.5.0.64"
                ),
                components_page,
            ),
            1,
            1,
        )
        # cards keep their natural height; spare room goes below them
        components_layout.addLayout(components_grid)
        components_layout.addStretch(1)

        component_versions_note = QLabel(
            _tr("These are the versions included in this build; they are not "
                "necessarily the latest versions available."),
            components_page,
        )
        component_versions_note.setObjectName("mo2RevampedAboutVersionsNote")
        component_versions_note.setWordWrap(True)
        components_layout.addWidget(component_versions_note)
        tabs.addTab(components_page, _tr("Components"))
        credits_page = QWidget(dialog)
        credits_page.setObjectName("mo2RevampedAboutCreditsPage")
        credits_layout = QVBoxLayout(credits_page)
        credits_layout.setContentsMargins(14, 14, 14, 12)
        credits_layout.setSpacing(11)

        credits_layout.addWidget(
            self._about_page_intro(
                _tr("Credits and licenses"),
                _tr("Information about this community edition, the base project and its legal notices."),
                credits_page,
            )
        )

        credits_grid = QGridLayout()
        credits_grid.setContentsMargins(0, 0, 0, 0)
        credits_grid.setHorizontalSpacing(10)
        credits_grid.setVerticalSpacing(10)
        credits_grid.addWidget(
            self._about_feature(
                _tr("Revamped authorship"),
                "MO2 Revamped · ArialSenki\n" + _tr("Independent community edition."),
                credits_page,
            ),
            0,
            0,
        )
        credits_grid.addWidget(
            self._about_feature(
                _tr("Base project"),
                _tr("Based on Mod Organizer 2 and its contributions. Revamped is not "
                    "an official release from the MO2 team."),
                credits_page,
            ),
            0,
            1,
        )
        credits_layout.addLayout(credits_grid)

        license_card = QFrame(credits_page)
        license_card.setObjectName("mo2RevampedAboutLicenseCard")
        license_layout = QVBoxLayout(license_card)
        license_layout.setContentsMargins(13, 11, 13, 10)
        license_layout.setSpacing(5)
        license_title = QLabel(_tr("Licenses and source code"), license_card)
        license_title.setObjectName("mo2RevampedAboutLicenseTitle")
        license_layout.addWidget(license_title)
        license_note = QLabel(
            _tr("The authorship notices and licenses of MO2 and its dependencies are "
                "kept. See the licenses folder next to the program."),
            license_card,
        )
        license_note.setObjectName("mo2RevampedAboutLicenseText")
        license_note.setWordWrap(True)
        license_layout.addWidget(license_note)
        source_links = self._browser(
            "<p><a href=\"https://github.com/ModOrganizer2/modorganizer\">"
            + _tr("Original Mod Organizer 2 project and source code")
            + "</a><br/><a href=\"https://github.com/ModOrganizer2/usvfs\">"
            + _tr("USVFS source code")
            + "</a></p>",
            license_card,
        )
        source_links.setObjectName("mo2RevampedAboutSourceLinks")
        source_links.setFrameShape(QFrame.Shape.NoFrame)
        source_links.setMinimumHeight(58)
        license_layout.addWidget(source_links)
        credits_layout.addWidget(license_card, 1)
        tabs.addTab(credits_page, _tr("Credits and licenses"))

        diagnostics_page = QWidget(dialog)
        diagnostics_page.setObjectName("mo2RevampedAboutDiagnosticsPage")
        diagnostics_layout = QVBoxLayout(diagnostics_page)
        diagnostics_layout.setContentsMargins(14, 14, 14, 12)
        diagnostics_layout.setSpacing(11)

        diagnostics_layout.addWidget(
            self._about_page_intro(
                _tr("Bug reports"),
                _tr("Collects the logs needed to help locate MO2 Revamped problems."),
                diagnostics_page,
            )
        )

        diagnostics_grid = QGridLayout()
        diagnostics_grid.setContentsMargins(0, 0, 0, 0)
        diagnostics_grid.setHorizontalSpacing(10)
        diagnostics_grid.setVerticalSpacing(10)
        diagnostics_grid.addWidget(
            self._about_feature(
                _tr("Report contents"),
                _tr("Includes the main MO2 log, recent USVFS logs and the current and "
                    "previous Elden Ring bridge logs when available, along with program, "
                    "system, game and instance versions."),
                diagnostics_page,
            ),
            0,
            0,
        )
        diagnostics_grid.addWidget(
            self._about_feature(
                _tr("Privacy"),
                _tr("Mods, downloads, saves, Nexus keys and memory dumps are not included. "
                    "Logs can contain Windows paths and profile names; review them "
                    "before sharing."),
                diagnostics_page,
            ),
            0,
            1,
        )
        diagnostics_grid.addWidget(
            self._about_feature(
                _tr("If MO2 does not start"),
                _tr("Attach the latest installer log shown in its error message. "
                    "If MO2 closes unexpectedly, also keep the file from crashDumps."),
                diagnostics_page,
            ),
            1,
            0,
            1,
            2,
        )
        diagnostics_layout.addLayout(diagnostics_grid, 1)

        create_report_button = QPushButton(
            _tr("Create MO2 Revamped bug report…"), diagnostics_page
        )
        create_report_button.setObjectName("mo2RevampedAboutDiagnosticsButton")
        create_report_button.clicked.connect(
            lambda _checked=False: self._create_diagnostics_bundle(dialog)
        )
        report_button_row = QHBoxLayout()
        report_button_row.setContentsMargins(0, 0, 0, 0)
        report_button_row.addStretch(1)
        report_button_row.addWidget(create_report_button)
        diagnostics_layout.addLayout(report_button_row)
        tabs.addTab(diagnostics_page, _tr("Diagnostics"))
        layout.addWidget(tabs, 1)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close, dialog)
        buttons.rejected.connect(dialog.reject)
        buttons.accepted.connect(dialog.accept)
        buttons.button(QDialogButtonBox.StandardButton.Close).clicked.connect(
            dialog.accept
        )
        layout.addWidget(buttons)
        dialog.exec()

    @staticmethod
    def _safe_text(value) -> str:
        try:
            return str(value)
        except Exception:
            return "unavailable"

    def _diagnostics_context(self) -> str:
        app_dir = Path(QApplication.applicationDirPath())
        game_name = "Not detected"
        profile_name = "Not selected"
        profile_path = "Not available"
        game_path = "Not available"
        game_data_path = "Not available"
        try:
            game = self._organizer.managedGame()
            if game is not None:
                game_name = self._safe_text(game.gameName())
                try:
                    game_path = self._safe_text(game.gameDirectory().absolutePath())
                except Exception:
                    pass
                try:
                    game_data_path = self._safe_text(game.dataDirectory().absolutePath())
                except Exception:
                    pass
        except Exception:
            pass
        try:
            profile = self._organizer.profile()
            if profile is not None:
                profile_name = self._safe_text(profile.name())
                profile_path = self._safe_text(profile.absolutePath())
        except Exception:
            pass

        mode = "Standard"
        if (app_dir / "eldenring-only.portable").is_file():
            mode = "Isolated portable - Elden Ring only"
        elif (app_dir / "portable.txt").is_file():
            mode = "Portable"

        try:
            pyqt_version = QtCore.PYQT_VERSION_STR
        except Exception:
            pyqt_version = "Unavailable"
        try:
            qt_version = QtCore.qVersion()
        except Exception:
            qt_version = "Unavailable"
        try:
            import ssl
            openssl_version = ssl.OPENSSL_VERSION
        except Exception:
            openssl_version = "Unavailable"

        return "\n".join(
            (
                "MO2 Revamped diagnostic report",
                f"Created UTC: {datetime.now(timezone.utc).isoformat()}",
                "Package: MO2 Revamped 1.0.2 (based on MO2 2.5.2)",
                "Base: Mod Organizer 2.5.2",
                "USVFS: 0.5.7.2",
                f"Windows: {platform.platform()}",
                f"Architecture: {platform.machine()}",
                f"Python: {sys.version.replace(os.linesep, ' ')}",
                f"OpenSSL: {openssl_version}",
                f"Qt: {qt_version}",
                f"PyQt: {pyqt_version}",
                f"Application directory: {app_dir}",
                f"Mods directory: {self._organizer_path('modsPath')}",
                f"Downloads directory: {self._organizer_path('downloadsPath')}",
                f"Overwrite directory: {self._organizer_path('overwritePath')}",
                f"Instance mode: {mode}",
                f"Active game: {game_name}",
                f"Game directory: {game_path}",
                f"Game data directory: {game_data_path}",
                f"Active profile: {profile_name}",
                f"Profile directory: {profile_path}",
                "Elden Ring game plugin: 0.5.0-alpha.75",
                "Native bridge: 0.5.0-alpha.35",
                "Archive layout installer: alpha.72",
                "Save isolation: alpha.76",
                "Startup options: 0.5.0.64",
                "",
                "Review this report and the included logs before sharing; paths and profile names may identify your Windows account.",
            )
        )

    def _organizer_path(self, method_name: str) -> str:
        try:
            value = getattr(self._organizer, method_name)()
            if hasattr(value, "absolutePath"):
                value = value.absolutePath()
            return self._safe_text(value)
        except Exception:
            return "Not available"

    @staticmethod
    def _diagnostic_log_files() -> list[tuple[str, Path]]:
        app_dir = Path(QApplication.applicationDirPath())
        log_dir = app_dir / "logs"
        candidates: list[tuple[str, Path]] = []
        main_log = log_dir / "mo_interface.log"
        if main_log.is_file():
            candidates.append(("logs/mo_interface.log", main_log))

        if log_dir.is_dir():
            usvfs_logs = sorted(
                log_dir.glob("usvfs-*.log"),
                key=lambda path: path.stat().st_mtime,
                reverse=True,
            )
            candidates.extend(
                (f"logs/{path.name}", path) for path in usvfs_logs[:3]
            )
            memory_log = log_dir / "memory_diagnostics.log"
            if memory_log.is_file():
                candidates.append(("logs/memory_diagnostics.log", memory_log))

        temp_dir = Path(tempfile.gettempdir())
        for archive_name, filename in (
            ("bridge/current.log", "EldenRingMO2Bridge.log"),
            ("bridge/previous.log", "EldenRingMO2Bridge.previous.log"),
        ):
            path = temp_dir / filename
            if path.is_file():
                candidates.append((archive_name, path))
        return candidates

    @staticmethod
    def _show_report_notice(
        parent, object_name: str, title: str, heading: str, details: str
    ) -> None:
        message = QMessageBox(parent)
        message.setObjectName(object_name)
        message.setOption(QMessageBox.Option.DontUseNativeDialog, True)
        message.setWindowTitle(title)
        message.setIcon(QMessageBox.Icon.NoIcon)
        message.setText(heading)
        message.setInformativeText(details)
        message.setStandardButtons(QMessageBox.StandardButton.Ok)
        message.setDefaultButton(QMessageBox.StandardButton.Ok)
        message.setEscapeButton(QMessageBox.StandardButton.Ok)
        accept_button = message.button(QMessageBox.StandardButton.Ok)
        if accept_button is not None:
            accept_button.setText(_tr("OK"))
            accept_button.setObjectName("mo2RevampedReportAcceptButton")
        for label_name in ("qt_msgbox_label", "qt_msgbox_informativelabel"):
            label = message.findChild(QLabel, label_name)
            if label is not None:
                label.setWordWrap(True)
        message.exec()

    def _create_diagnostics_bundle(self, parent) -> None:
        confirmation = QMessageBox(parent)
        confirmation.setObjectName("MO2RevampedReportConfirmDialog")
        confirmation.setOption(QMessageBox.Option.DontUseNativeDialog, True)
        confirmation.setWindowTitle(_tr("Create bug report"))
        confirmation.setIcon(QMessageBox.Icon.NoIcon)
        confirmation.setText(_tr("Review the report before sharing it"))
        confirmation.setInformativeText(
            _tr("Includes MO2, USVFS and Elden Ring bridge logs. They can contain "
                "Windows paths and profile names. The ZIP does not include mods, "
                "downloads, saves, Nexus keys or memory dumps.")
        )
        create_button = confirmation.addButton(
            _tr("Create report"), QMessageBox.ButtonRole.AcceptRole
        )
        create_button.setObjectName("mo2RevampedReportCreateButton")
        cancel_button = confirmation.addButton(
            _tr("Cancel"), QMessageBox.ButtonRole.RejectRole
        )
        cancel_button.setObjectName("mo2RevampedReportCancelButton")
        confirmation.setDefaultButton(cancel_button)
        confirmation.setEscapeButton(cancel_button)
        for label_name in ("qt_msgbox_label", "qt_msgbox_informativelabel"):
            label = confirmation.findChild(QLabel, label_name)
            if label is not None:
                label.setWordWrap(True)
        confirmation.exec()
        if confirmation.clickedButton() is not create_button:
            return

        filename = "MO2-Revamped-Diagnostics-" + datetime.now().strftime(
            "%Y%m%d-%H%M%S"
        ) + ".zip"
        initial_path = Path.home() / "Desktop" / filename
        save_dialog = QFileDialog(parent)
        save_dialog.setObjectName("MO2RevampedReportSaveDialog")
        save_dialog.setOption(QFileDialog.Option.DontUseNativeDialog, True)
        save_dialog.setWindowTitle(_tr("Save MO2 Revamped report"))
        save_dialog.setAcceptMode(QFileDialog.AcceptMode.AcceptSave)
        save_dialog.setFileMode(QFileDialog.FileMode.AnyFile)
        save_dialog.setNameFilters([_tr("ZIP archive (*.zip)")])
        save_dialog.setDefaultSuffix("zip")
        save_dialog.setDirectory(str(initial_path.parent))
        save_dialog.selectFile(initial_path.name)
        save_label = _tr("Save")
        save_dialog.setLabelText(QFileDialog.DialogLabel.LookIn, _tr("Save in:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.FileName, _tr("File name:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.FileType, _tr("File type:"))
        save_dialog.setLabelText(QFileDialog.DialogLabel.Accept, save_label)
        save_dialog.setLabelText(QFileDialog.DialogLabel.Reject, _tr("Cancel"))
        for button in save_dialog.findChildren(QPushButton):
            if button.text().replace("&", "") == save_label:
                button.setObjectName("mo2RevampedReportSaveButton")
        save_dialog.setMinimumSize(760, 500)
        save_dialog.resize(860, 560)
        if save_dialog.exec() != QDialog.DialogCode.Accepted:
            return
        selected_files = save_dialog.selectedFiles()
        destination = selected_files[0] if selected_files else ""
        if not destination:
            return
        if not destination.casefold().endswith(".zip"):
            destination += ".zip"

        try:
            included = []
            collection_errors = []
            with zipfile.ZipFile(
                destination, "w", compression=zipfile.ZIP_DEFLATED
            ) as archive:
                archive.writestr("diagnostics.txt", self._diagnostics_context())
                for archive_name, source_path in self._diagnostic_log_files():
                    try:
                        metadata = source_path.stat()
                        data = source_path.read_bytes()
                        original_size = len(data)
                        truncated = original_size > 2 * 1024 * 1024
                        if truncated:
                            data = (
                                b"[MO2 Revamped report: only the last 2 MiB are included]\n"
                                + data[-(2 * 1024 * 1024):]
                            )
                        archive.writestr(archive_name, data)
                        modified = datetime.fromtimestamp(
                            metadata.st_mtime, timezone.utc
                        ).isoformat()
                        included.append(
                            f"{archive_name}\tmodified UTC={modified}\tsize={original_size} bytes"
                            + ("\tcontent limited to last 2 MiB" if truncated else "")
                        )
                    except OSError as error:
                        collection_errors.append(
                            f"Could not read {archive_name}: {error}"
                        )
                archive.writestr(
                    "included-files.txt",
                    "\n".join(included) if included else "No application log files were available.",
                )
                if collection_errors:
                    archive.writestr(
                        "collection-errors.txt",
                        "\n".join(collection_errors),
                    )
            self._show_report_notice(
                parent,
                "MO2RevampedReportSuccessDialog",
                _tr("Report created"),
                _tr("The report was saved"),
                _tr("Location:\n{path}\n\n"
                    "Review the ZIP and remove private paths before sharing it.").format(
                    path=destination
                ),
            )
        except Exception as error:
            qCritical(
                "MO2 Revamped: could not create diagnostic report.\n"
                + traceback.format_exc()
            )
            self._show_report_notice(
                parent,
                "MO2RevampedReportErrorDialog",
                _tr("Could not create the report"),
                _tr("MO2 Revamped could not save the bug report"),
                _tr("{error}\n\nCheck the MO2 log for details.").format(error=error),
            )


def createPlugin() -> Mo2RevampedInfo:
    return Mo2RevampedInfo()
