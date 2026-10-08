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
from PyQt6.QtCore import Qt, qCritical
from PyQt6.QtGui import QAction, QDesktopServices, QIcon, QPixmap
from PyQt6.QtWidgets import (
    QApplication,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFrame,
    QGridLayout,
    QGroupBox,
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
        return "About, credits, component versions, and clearer help for MO2 Revamped."

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo(1, 0, 0, 0)

    def requirements(self):
        return []

    def settings(self) -> list[mobase.PluginSetting]:
        return []

    def enabledByDefault(self) -> bool:
        return True

    def displayName(self) -> str:
        return "About MO2 Revamped"

    def tooltip(self) -> str:
        return "View MO2 Revamped features, component versions, credits, and licenses."

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
                "Nexus Mods: abre el sitio web para explorar mods del juego activo.",
                "Abre Nexus Mods en el navegador. El contenido depende del juego activo y de tu cuenta.",
            ),
            (
                "actionNotifications",
                "Avisos de MO2: consulta errores detectados y recomendaciones de configuración.",
                "Abre los avisos de MO2. El icono puede resaltarse cuando hay problemas que conviene revisar.",
            ),
            (
                "actionEndorseMO",
                "Apoya el proyecto original de Mod Organizer en Nexus Mods.",
                "Esta acción abre la página de Mod Organizer para respaldar el proyecto original.",
            ),
            (
                "actionHelp",
                "Ayuda de MO2: guías y accesos de soporte. Usa «About MO2 Revamped» para ver esta edición, sus componentes y créditos.",
                "Abre las opciones de ayuda. «About MO2 Revamped» describe esta edición, sus funciones, componentes y licencias.",
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
            self._about_action = QAction("About MO2 Revamped…", main_window)
            self._about_action.setObjectName("mo2RevampedAboutAction")
            self._about_action.setToolTip(
                "Información de MO2 Revamped, funciones, componentes y créditos."
            )
            self._about_action.triggered.connect(
                lambda _checked=False: self._show_about(main_window)
            )
            help_menu.addAction(self._about_action)
        if not diagnostics_found:
            self._diagnostics_action = QAction(
                "Create MO2 Revamped bug report…", main_window
            )
            self._diagnostics_action.setObjectName("mo2RevampedDiagnosticsAction")
            self._diagnostics_action.setToolTip(
                "Guarda un informe con los registros de MO2, USVFS y el puente de Elden Ring."
            )
            self._diagnostics_action.triggered.connect(
                lambda _checked=False: self._create_diagnostics_bundle(main_window)
            )
            help_menu.addAction(self._diagnostics_action)

    def _context_text(self) -> str:
        game_name = "No detectado"
        profile_name = "No seleccionado"
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
        return f"Juego activo: {game_name}<br>Perfil activo: {profile_name}"

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
    def _about_feature(title: str, description: str, parent: QWidget) -> QGroupBox:
        card = QGroupBox(title, parent)
        card_layout = QVBoxLayout(card)
        card_layout.setContentsMargins(10, 10, 10, 8)
        card_layout.setSpacing(4)

        text = QLabel(description, card)
        text.setWordWrap(True)
        card_layout.addWidget(text)
        return card

    def _show_about(self, parent) -> None:
        dialog = QDialog(parent)
        dialog.setWindowTitle("About MO2 Revamped")
        dialog.setMinimumSize(720, 510)
        dialog.resize(790, 560)
        dialog.setWindowFlag(Qt.WindowType.WindowContextHelpButtonHint, False)

        layout = QVBoxLayout(dialog)
        header = QHBoxLayout()
        logo = QLabel(dialog)
        logo.setObjectName("mo2RevampedAboutLogo")
        logo.setFixedSize(72, 72)
        logo.setAlignment(Qt.AlignmentFlag.AlignCenter)
        try:
            import os

            app_dir = QApplication.applicationDirPath()
            pixmap = QPixmap(os.path.join(app_dir, "resources", "mo_icon.png"))
            if pixmap.isNull():
                pixmap = QPixmap(os.path.join(app_dir, "splash.png"))
            if not pixmap.isNull():
                logo.setPixmap(pixmap.scaled(
                    logo.size(),
                    Qt.AspectRatioMode.KeepAspectRatio,
                    Qt.TransformationMode.SmoothTransformation,
                ))
        except Exception:
            pass
        header.addWidget(logo)

        title = QLabel(
            "<h2>Mod Organizer 2: Revamped</h2>"
            "<p>Una edición comunitaria con mejoras de interfaz y herramientas "
            "especializadas para Elden Ring.</p>"
            "<p><b>Edición:</b> Revamped · <b>Autor:</b> ArialSenki</p>"
            f"<p>{self._context_text()}</p>",
            dialog,
        )
        title.setWordWrap(True)
        header.addWidget(title, 1)
        layout.addLayout(header)

        tabs = QTabWidget(dialog)
        tabs.setObjectName("mo2RevampedAboutTabs")
        overview_page = QWidget(dialog)
        overview_layout = QVBoxLayout(overview_page)
        overview_layout.setContentsMargins(14, 12, 14, 12)
        overview_layout.setSpacing(10)

        overview_intro = QLabel(
            "<h3>Qué aporta Revamped</h3>"
            "<p>Conserva el flujo de Mod Organizer 2 y añade herramientas para "
            "instalar mods, separar configuraciones y reconocer mejor cada entorno.</p>",
            overview_page,
        )
        overview_intro.setWordWrap(True)
        overview_layout.addWidget(overview_intro)

        feature_grid = QGridLayout()
        feature_grid.setHorizontalSpacing(10)
        feature_grid.setVerticalSpacing(8)
        feature_grid.addWidget(
            self._about_feature(
                "Instalación de mods",
                "Detecta rutas de Elden Ring y reconoce carpetas anidadas para "
                "instalar mods y complementos nativos.",
                overview_page,
            ),
            0,
            0,
        )
        feature_grid.addWidget(
            self._about_feature(
                "Perfiles y partidas",
                "Organiza perfiles y, para Elden Ring, permite enrutar los "
                "guardados por perfil.",
                overview_page,
            ),
            0,
            1,
        )
        feature_grid.addWidget(
            self._about_feature(
                "Instancias independientes",
                "Elige instalaciones globales o portables. También puedes crear "
                "una copia aislada de MO2 para Elden Ring.",
                overview_page,
            ),
            1,
            0,
        )
        feature_grid.addWidget(
            self._about_feature(
                "Interfaz y diagnóstico",
                "Incluye los temas MO2 Classic Refined claro y oscuro, además de "
                "herramientas para preparar informes de diagnóstico.",
                overview_page,
            ),
            1,
            1,
        )
        overview_layout.addLayout(feature_grid, 1)

        compatibility_note = QLabel(
            "Las funciones específicas de Elden Ring se activan únicamente en "
            "una instancia de ese juego.",
            overview_page,
        )
        compatibility_note.setWordWrap(True)
        overview_layout.addWidget(compatibility_note)
        tabs.addTab(overview_page, "Revamped")
        components_page = QWidget(dialog)
        components_layout = QVBoxLayout(components_page)
        components_layout.setContentsMargins(14, 12, 14, 12)
        components_layout.setSpacing(10)

        components_intro = QLabel(
            "<h3>Componentes incluidos</h3>"
            "<p>Resumen de las versiones y herramientas que forman parte de esta copia.</p>",
            components_page,
        )
        components_intro.setWordWrap(True)
        components_layout.addWidget(components_intro)

        components_grid = QGridLayout()
        components_grid.setHorizontalSpacing(10)
        components_grid.setVerticalSpacing(8)
        components_grid.addWidget(
            self._about_feature(
                "Base de MO2",
                "Mod Organizer 2 · 2.5.2\nQt y PyQt · 6.7.1\nPython · 3.12.15\nOpenSSL · 3.5.9",
                components_page,
            ),
            0,
            0,
        )
        components_grid.addWidget(
            self._about_feature(
                "Sistema de archivos virtual",
                "USVFS · 0.5.7.2\nPermite desplegar mods virtualmente sin modificar "
                "los archivos originales del juego.",
                components_page,
            ),
            0,
            1,
        )
        components_grid.addWidget(
            self._about_feature(
                "Herramientas Elden Ring",
                "Complemento · 0.5.0-alpha.75\nPuente nativo · 0.5.0-alpha.33\n"
                "Instalador de archivos · alpha.72",
                components_page,
            ),
            1,
            0,
        )
        components_grid.addWidget(
            self._about_feature(
                "Perfiles e interfaz",
                "Aislamiento de partidas · alpha.68\nOpciones de inicio · 0.5.0.64\n"
                "MO2 Classic Refined · Light y Dark",
                components_page,
            ),
            1,
            1,
        )
        components_layout.addLayout(components_grid, 1)

        component_versions_note = QLabel(
            "Son las versiones incluidas en esta compilación; no indican "
            "necesariamente las versiones más recientes disponibles.",
            components_page,
        )
        component_versions_note.setWordWrap(True)
        components_layout.addWidget(component_versions_note)
        tabs.addTab(components_page, "Componentes")
        credits_page = QWidget(dialog)
        credits_layout = QVBoxLayout(credits_page)
        credits_layout.setContentsMargins(14, 12, 14, 12)
        credits_layout.setSpacing(10)

        credits_intro = QLabel(
            "<h3>Créditos y licencias</h3>"
            "<p>Información sobre esta edición comunitaria, el proyecto base y sus avisos legales.</p>",
            credits_page,
        )
        credits_intro.setWordWrap(True)
        credits_layout.addWidget(credits_intro)

        credits_grid = QGridLayout()
        credits_grid.setHorizontalSpacing(10)
        credits_grid.setVerticalSpacing(8)
        credits_grid.addWidget(
            self._about_feature(
                "Autoría de Revamped",
                "MO2 Revamped · ArialSenki\nEdición comunitaria independiente.",
                credits_page,
            ),
            0,
            0,
        )
        credits_grid.addWidget(
            self._about_feature(
                "Proyecto base",
                "Basado en Mod Organizer 2 y sus contribuciones. Revamped no es "
                "una publicación oficial del equipo de MO2.",
                credits_page,
            ),
            0,
            1,
        )
        credits_layout.addLayout(credits_grid)

        license_group = QGroupBox("Licencias y código fuente", credits_page)
        license_layout = QVBoxLayout(license_group)
        license_layout.setContentsMargins(10, 10, 10, 8)
        license_layout.setSpacing(4)
        license_note = QLabel(
            "Se conservan los avisos de autoría y las licencias de MO2 y sus dependencias. "
            "Consulta la carpeta licenses junto al programa.",
            license_group,
        )
        license_note.setWordWrap(True)
        license_layout.addWidget(license_note)
        source_links = self._browser(
            "<p><a href=\"https://github.com/ModOrganizer2/modorganizer\">"
            "Proyecto original y código fuente de Mod Organizer 2</a><br/>"
            "<a href=\"https://github.com/ModOrganizer2/usvfs\">"
            "Código fuente de USVFS</a></p>",
            license_group,
        )
        source_links.setFrameShape(QFrame.Shape.NoFrame)
        source_links.setStyleSheet("background: transparent;")
        source_links.setFixedHeight(58)
        license_layout.addWidget(source_links)
        credits_layout.addWidget(license_group)
        credits_layout.addStretch(1)
        tabs.addTab(credits_page, "Créditos y licencias")

        diagnostics_page = QWidget(dialog)
        diagnostics_layout = QVBoxLayout(diagnostics_page)
        diagnostics_layout.setContentsMargins(14, 12, 14, 12)
        diagnostics_layout.setSpacing(10)

        diagnostics_intro = QLabel(
            "<h3>Informes para corregir errores</h3>"
            "<p>El informe reúne los registros necesarios para ayudar a localizar problemas de MO2 Revamped.</p>",
            diagnostics_page,
        )
        diagnostics_intro.setWordWrap(True)
        diagnostics_layout.addWidget(diagnostics_intro)

        diagnostics_grid = QGridLayout()
        diagnostics_grid.setHorizontalSpacing(10)
        diagnostics_grid.setVerticalSpacing(8)
        diagnostics_grid.addWidget(
            self._about_feature(
                "Contenido del informe",
                "Incluye el registro principal de MO2, registros recientes de USVFS y "
                "los registros actual y anterior del puente de Elden Ring cuando "
                "están disponibles, junto con versiones del programa, sistema, juego e instancia.",
                diagnostics_page,
            ),
            0,
            0,
        )
        diagnostics_grid.addWidget(
            self._about_feature(
                "Privacidad",
                "No incluye mods, descargas, partidas, claves de Nexus ni volcados de memoria. "
                "Los registros pueden contener rutas de Windows y nombres de perfiles; revísalos "
                "antes de compartirlos.",
                diagnostics_page,
            ),
            0,
            1,
        )
        diagnostics_grid.addWidget(
            self._about_feature(
                "Si MO2 no inicia",
                "Adjunta el último registro del instalador que aparece en su mensaje de error. "
                "Si MO2 se cierra inesperadamente, conserva también el archivo de crashDumps.",
                diagnostics_page,
            ),
            1,
            0,
            1,
            2,
        )
        diagnostics_layout.addLayout(diagnostics_grid, 1)

        create_report_button = QPushButton(
            "Crear informe de errores de MO2 Revamped…", diagnostics_page
        )
        create_report_button.clicked.connect(
            lambda _checked=False: self._create_diagnostics_bundle(dialog)
        )
        report_button_row = QHBoxLayout()
        report_button_row.addStretch(1)
        report_button_row.addWidget(create_report_button)
        diagnostics_layout.addLayout(report_button_row)
        tabs.addTab(diagnostics_page, "Diagnóstico")
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
                "Package: MO2 Revamped 1.0.0 (based on MO2 2.5.2)",
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
                "Native bridge: 0.5.0-alpha.33",
                "Archive layout installer: alpha.72",
                "Save isolation: alpha.68",
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

    def _create_diagnostics_bundle(self, parent) -> None:
        response = QMessageBox.warning(
            parent,
            "Review before sharing",
            "The report contains MO2, USVFS and Elden Ring bridge logs. They can "
            "include Windows paths and profile names. Review the ZIP before "
            "sending it. It does not include mods, downloads, saves, Nexus keys "
            "or memory dumps.",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel,
            QMessageBox.StandardButton.Cancel,
        )
        if response != QMessageBox.StandardButton.Yes:
            return

        filename = "MO2-Revamped-Diagnostics-" + datetime.now().strftime(
            "%Y%m%d-%H%M%S"
        ) + ".zip"
        initial_path = Path.home() / "Desktop" / filename
        destination, _ = QFileDialog.getSaveFileName(
            parent,
            "Save MO2 Revamped diagnostic report",
            str(initial_path),
            "ZIP archive (*.zip)",
        )
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
            QMessageBox.information(
                parent,
                "Diagnostic report created",
                f"Saved the report to:\n{destination}\n\n"
                "Review its contents and remove any private paths before sharing.",
            )
        except Exception as error:
            qCritical(
                "MO2 Revamped: could not create diagnostic report.\n"
                + traceback.format_exc()
            )
            QMessageBox.critical(
                parent,
                "Could not create diagnostic report",
                f"{error}\n\nSee the MO2 log for details.",
            )


def createPlugin() -> Mo2RevampedInfo:
    return Mo2RevampedInfo()
