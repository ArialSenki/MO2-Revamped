using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Mo2RevampedSetup
{
    internal sealed class CompactSetupForm : Form
    {
        private static readonly Color Navy = Color.FromArgb(41, 56, 71);
        private static readonly Color Page = Color.FromArgb(242, 245, 248);
        private static readonly Color Border = Color.FromArgb(199, 211, 221);
        private static readonly Color TextColor = Color.FromArgb(45, 57, 68);
        private static readonly Color Muted = Color.FromArgb(102, 116, 129);
        private static readonly Color White = Color.White;

        private readonly BackgroundWorker Worker = new BackgroundWorker();
        private readonly bool UninstallerOnly;
        private readonly string FixedTarget;
        private ComboBox ActionBox;
        private Label ActionDescription;
        private FlowLayoutPanel Content;
        private GroupBox ActionGroup;
        private GroupBox LocationGroup;
        private GroupBox OptionsGroup;
        private GroupBox ProgressGroup;
        private TextBox FreshTargetBox;
        private TextBox UpdateTargetBox;
        private TextBox SourceInstallBox;
        private TextBox SourceDataBox;
        private TextBox MigrateTargetBox;
        private TextBox RestoreTargetBox;
        private TextBox UninstallTargetBox;
        private RadioButton PortableRadio;
        private RadioButton StandardRadio;
        private bool FreshPortableSelected = true;
        private string FreshAutoPath;
        private CheckBox ProfilesCheck;
        private CheckBox ModsCheck;
        private CheckBox DownloadsCheck;
        private CheckBox OverwriteCheck;
        private CheckBox SettingsCheck;
        private CheckBox CategoriesCheck;
        private CheckBox StartMenuCheck;
        private CheckBox DesktopCheck;
        private CheckBox LaunchCheck;
        private CheckBox ForceCloseCheck;
        private CheckBox RemoveAllContentsCheck;
        private Label ProgressDetail;
        private ProgressBar ProgressBar;
        private TextBox ProgressLog;
        private Button ActionButton;
        private Button CloseButton;
        private bool IsWorking;
        private bool HasCompleted;
        private string LastLoggedStage;
        private bool HasRenderedLocation;
        private SetupMode RenderedLocationMode;
        private readonly Dictionary<SetupMode, string[]> SavedPaths = new Dictionary<SetupMode, string[]>();

        private sealed class ModeChoice
        {
            public SetupMode Mode;
            public string Title;
            public string Description;

            public ModeChoice(SetupMode mode, string title, string description)
            {
                Mode = mode;
                Title = title;
                Description = description;
            }

            public override string ToString() { return Title; }
        }

        public CompactSetupForm(SetupMode? fixedMode, string target, bool uninstallerOnly)
        {
            UninstallerOnly = uninstallerOnly;
            FixedTarget = target ?? "";
            Text = uninstallerOnly ? "Desinstalar MO2 Revamped" : "Mod Organizer 2: Revamped";
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = !uninstallerOnly;
            AutoScaleMode = AutoScaleMode.Font;
            BackColor = Page;
            ForeColor = TextColor;
            Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
            ClientSize = new Size(760, uninstallerOnly ? 650 : 690);
            MinimumSize = new Size(776, 570);
            try
            {
                Icon icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                if (icon != null) Icon = icon;
            }
            catch { }

            BuildWindow(fixedMode);
            Worker.WorkerReportsProgress = true;
            Worker.DoWork += WorkerDoWork;
            Worker.ProgressChanged += WorkerProgressChanged;
            Worker.RunWorkerCompleted += WorkerCompleted;
        }

        private void BuildWindow(SetupMode? fixedMode)
        {
            TableLayoutPanel root = new TableLayoutPanel();
            root.Dock = DockStyle.Fill;
            root.Padding = new Padding(17, 12, 17, 8);
            root.ColumnCount = 1;
            root.RowCount = 3;
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 47));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 48));
            Controls.Add(root);

            Panel heading = new Panel { Dock = DockStyle.Fill };
            Label title = new Label
            {
                Text = UninstallerOnly ? "Desinstalar MO2 Revamped" : "Instalador de MO2 Revamped",
                Font = new Font("Segoe UI Semibold", 13F, FontStyle.Bold),
                ForeColor = Navy,
                AutoSize = true,
                Location = new Point(1, 0)
            };
            heading.Controls.Add(title);
            root.Controls.Add(heading, 0, 0);

            Content = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = true,
                BackColor = Page,
                Padding = new Padding(12, 1, 12, 5),
                Margin = Padding.Empty
            };
            root.Controls.Add(Content, 0, 1);

            ActionGroup = NewGroup("Acción", 100);
            ActionBox = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Location = new Point(14, 24),
                Width = 690,
                Font = new Font("Segoe UI", 9F)
            };
#if ELDENRING_ONLY_INSTALLER
            ActionBox.Items.Add(new ModeChoice(SetupMode.EldenRingPortable, "Instalación portable solo para Elden Ring", "Instala una edición reducida y aislada, en una carpeta propia, con soporte únicamente para Elden Ring."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Update, "Actualizar esta edición aislada de Elden Ring", "Solo actualiza una instalación portable marcada como aislada para Elden Ring."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Restore, "Restaurar la versión anterior de Elden Ring", "Recupera la copia de seguridad de esta instalación aislada de Elden Ring."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Uninstall, "Desinstalar esta edición aislada de Elden Ring", "Quita únicamente una instalación marcada como aislada para Elden Ring."));
#else
            ActionBox.Items.Add(new ModeChoice(SetupMode.Fresh, "Instalación nueva", "Instala MO2 Revamped en una carpeta vacía. Elige una instancia portable o estándar."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.EldenRingPortable, "Instalación portable solo para Elden Ring", "Instala una edición reducida y aislada, en una carpeta propia, con soporte únicamente para Elden Ring."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Update, "Actualizar MO2 existente", "Reemplaza los archivos del programa compatibles y guarda los originales para poder restaurarlos."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Migrate, "Convertir o copiar datos", "Crea una instalación portable nueva y copia solo las categorías seleccionadas. El origen queda intacto."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Restore, "Restaurar MO2 anterior", "Revierte una actualización y recupera los archivos previos de MO2. Requiere una copia de seguridad."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Uninstall, "Desinstalar MO2 Revamped", "Permite quitar MO2 Revamped o la versión original de MO2. Elige si conservar los datos o borrar también toda la carpeta seleccionada."));
#endif
            ActionDescription = NewLabel("", false);
            ActionDescription.Location = new Point(15, 54);
            ActionDescription.Size = new Size(700, 34);
            ActionDescription.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
            ActionGroup.Controls.Add(ActionBox);
            ActionGroup.Controls.Add(ActionDescription);
            Content.Controls.Add(ActionGroup);

            LocationGroup = NewGroup("Ubicación", 120);
            Content.Controls.Add(LocationGroup);

            OptionsGroup = NewGroup("Opciones", 90);
            Content.Controls.Add(OptionsGroup);

            ProgressGroup = NewGroup("Proceso", 159);
            ProgressDetail = NewLabel("Listo para empezar.", true);
            ProgressDetail.Location = new Point(15, 24);
            ProgressDetail.Size = new Size(700, 22);
            ProgressBar = new ProgressBar
            {
                Location = new Point(15, 51),
                Size = new Size(700, 18),
                Style = ProgressBarStyle.Continuous,
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right
            };
            ProgressLog = new TextBox
            {
                Location = new Point(15, 77),
                Size = new Size(700, 64),
                Multiline = true,
                ReadOnly = true,
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = White,
                ForeColor = Muted,
                ScrollBars = ScrollBars.Vertical,
                Text = "El proceso y los avisos aparecerán aquí.",
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right
            };
            ProgressGroup.Controls.Add(ProgressDetail);
            ProgressGroup.Controls.Add(ProgressBar);
            ProgressGroup.Controls.Add(ProgressLog);
            Content.Controls.Add(ProgressGroup);

            Panel footer = new Panel { Dock = DockStyle.Fill };
            footer.Paint += delegate(object sender, PaintEventArgs e)
            {
                using (Pen pen = new Pen(Border)) e.Graphics.DrawLine(pen, 12, 0, footer.Width - 12, 0);
            };
            CloseButton = NewButton("Cerrar", false);
            CloseButton.Size = new Size(104, 31);
            CloseButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            CloseButton.Click += delegate { if (!IsWorking) Close(); };
            ActionButton = NewButton("Instalar", true);
            ActionButton.Size = new Size(176, 31);
            ActionButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            ActionButton.Click += ActionButtonClick;
            footer.Controls.Add(CloseButton);
            footer.Controls.Add(ActionButton);
            footer.Resize += delegate
            {
                CloseButton.Left = footer.ClientSize.Width - CloseButton.Width;
                ActionButton.Left = CloseButton.Left - ActionButton.Width - 9;
            };
            root.Controls.Add(footer, 0, 2);

            ActionBox.SelectedIndexChanged += delegate { UpdateView(); };
            if (fixedMode.HasValue)
            {
                ActionBox.SelectedIndex = FindModeIndex(fixedMode.Value);
                ActionBox.Enabled = false;
            }
            else ActionBox.SelectedIndex = 0;
            if (UninstallerOnly)
            {
                ActionBox.Enabled = false;
                ActionButton.Text = "Desinstalar";
            }
            UpdateView();
            ResizeGroups();
            Content.Resize += delegate { ResizeGroups(); };
        }

        private GroupBox NewGroup(string title, int height)
        {
            return new GroupBox
            {
                Text = title,
                Height = height,
                Width = 715,
                BackColor = Page,
                ForeColor = Color.FromArgb(75, 98, 118),
                Font = new Font("Segoe UI", 9F, FontStyle.Regular),
                Padding = new Padding(10, 9, 10, 8),
                Margin = new Padding(0, 0, 0, 8)
            };
        }

        private Label NewLabel(string text, bool bold)
        {
            return new Label
            {
                Text = text,
                ForeColor = bold ? Navy : TextColor,
                Font = new Font(bold ? "Segoe UI Semibold" : "Segoe UI", 9F, bold ? FontStyle.Bold : FontStyle.Regular),
                AutoEllipsis = true
            };
        }

        private Button NewButton(string text, bool primary)
        {
            Button button = new Button
            {
                Text = text,
                FlatStyle = FlatStyle.Flat,
                BackColor = primary ? Color.FromArgb(232, 241, 248) : White,
                ForeColor = Navy,
                Font = new Font("Segoe UI", 9F),
                Cursor = Cursors.Hand
            };
            button.FlatAppearance.BorderColor = Border;
            button.FlatAppearance.BorderSize = 1;
            return button;
        }

        private int FindModeIndex(SetupMode mode)
        {
            for (int i = 0; i < ActionBox.Items.Count; i++)
                if (((ModeChoice)ActionBox.Items[i]).Mode == mode) return i;
            return 0;
        }

        private SetupMode SelectedMode
        {
            get { return ActionBox.SelectedItem == null ? SetupMode.Fresh : ((ModeChoice)ActionBox.SelectedItem).Mode; }
        }

        private void ResizeGroups()
        {
            int width = Math.Max(680, Content.ClientSize.Width - Content.Padding.Horizontal);
            foreach (Control control in Content.Controls) control.Width = width;
            int inner = width - 38;
            ActionBox.Width = inner;
            ActionDescription.Width = inner;
            ProgressDetail.Width = inner;
            ProgressBar.Width = inner;
            ProgressLog.Width = inner;
            foreach (Control control in LocationGroup.Controls)
            {
                if (control is Panel) control.Width = inner;
            }
            foreach (Control control in OptionsGroup.Controls)
            {
                if (control is Label) control.Width = inner;
                if (control is Panel) control.Width = inner;
            }
        }

        private void UpdateView()
        {
            if (ActionDescription == null) return;
            ModeChoice choice = (ModeChoice)ActionBox.SelectedItem;
            SetupMode mode = choice.Mode;
            ActionDescription.Text = choice.Description;
            ActionButton.Text = GetActionCaption(mode);
            BuildLocation(mode);
            BuildOptions(mode);
            if (!HasCompleted) ActionButton.Enabled = !IsWorking;
            ResizeGroups();
            int contentHeight = 0;
            foreach (Control control in Content.Controls) contentHeight += control.Height + control.Margin.Vertical;
            int desiredClientHeight = contentHeight + 121 + Content.Padding.Vertical;
            int screenHeight = Screen.FromControl(this).WorkingArea.Height;
            int maxClientHeight = screenHeight - 34 - (Height - ClientSize.Height);
            ClientSize = new Size(ClientSize.Width, Math.Min(desiredClientHeight, maxClientHeight));
        }

        private string GetActionCaption(SetupMode mode)
        {
            switch (mode)
            {
                case SetupMode.Update: return "Actualizar MO2";
                case SetupMode.EldenRingPortable: return "Instalar MO2 para Elden Ring";
                case SetupMode.Migrate: return "Instalar y copiar datos";
                case SetupMode.Restore: return "Restaurar MO2 anterior";
                case SetupMode.Uninstall: return "Desinstalar MO2";
                default: return "Instalar MO2";
            }
        }

        private void BuildLocation(SetupMode mode)
        {
            SaveRenderedPaths();
            LocationGroup.SuspendLayout();
            LocationGroup.Controls.Clear();
            RenderedLocationMode = mode;
            HasRenderedLocation = true;
            int y = 25;
            if (mode == SetupMode.Fresh || mode == SetupMode.EldenRingPortable)
            {
                string defaultTarget = mode == SetupMode.EldenRingPortable ? @"C:\MO2 Revamped\Isolated\Elden Ring" : @"C:\MO2 Revamped\Portable";
                FreshTargetBox = AddPathRow("Carpeta de destino", GetSavedPath(mode, 0, defaultTarget), ref y, true);
            }
            else if (mode == SetupMode.Update)
                UpdateTargetBox = AddPathRow("Instalación MO2 que se actualizará", GetSavedPath(mode, 0, @"C:\Modding\MO2"), ref y, true);
            else if (mode == SetupMode.Migrate)
            {
                SourceInstallBox = AddPathRow("Instalación MO2 de origen", GetSavedPath(mode, 0, ""), ref y, true);
                SourceDataBox = AddPathRow("Carpeta de datos del origen", GetSavedPath(mode, 1, ""), ref y, true);
                MigrateTargetBox = AddPathRow("Nueva carpeta de destino", GetSavedPath(mode, 2, @"C:\MO2 Revamped\Portable"), ref y, true);
                SourceInstallBox.TextChanged += delegate { AutoFindDataDirectory(); };
            }
            else if (mode == SetupMode.Restore)
                RestoreTargetBox = AddPathRow("Instalación con la copia de seguridad", GetSavedPath(mode, 0, FixedTarget.Length == 0 ? @"C:\Modding\MO2" : FixedTarget), ref y, !UninstallerOnly);
            else
            {
                UninstallTargetBox = AddPathRow("Carpeta de instalación de MO2", GetSavedPath(mode, 0, FixedTarget.Length == 0 ? @"C:\Modding\MO2" : FixedTarget), ref y, !UninstallerOnly);
                UninstallTargetBox.TextChanged += delegate { UpdateUninstallOptionsAvailability(); };
            }
            LocationGroup.Height = Math.Max(92, y + 6);
            LocationGroup.ResumeLayout(true);
        }

        private static string FreshDefaultTarget(bool portable)
        {
            return portable ? @"C:\MO2 Revamped\Portable" : @"C:\MO2 Revamped\Full";
        }

        private static bool SameInstallPath(string left, string right)
        {
            string normalizeLeft = (left ?? "").Trim().TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string normalizeRight = (right ?? "").Trim().TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            return String.Equals(normalizeLeft, normalizeRight, StringComparison.OrdinalIgnoreCase);
        }

        private void UpdateFreshInstallTarget(bool portable)
        {
            if (FreshTargetBox == null || String.IsNullOrWhiteSpace(FreshAutoPath)) return;
            if (!SameInstallPath(FreshTargetBox.Text, FreshAutoPath))
            {
                FreshAutoPath = "";
                return;
            }
            FreshAutoPath = FreshDefaultTarget(portable);
            FreshTargetBox.Text = FreshAutoPath;
        }

        private void SaveRenderedPaths()
        {
            if (!HasRenderedLocation) return;
            TextBox[] boxes;
            switch (RenderedLocationMode)
            {
                case SetupMode.Fresh:
                case SetupMode.EldenRingPortable: boxes = new TextBox[] { FreshTargetBox }; break;
                case SetupMode.Update: boxes = new TextBox[] { UpdateTargetBox }; break;
                case SetupMode.Migrate: boxes = new TextBox[] { SourceInstallBox, SourceDataBox, MigrateTargetBox }; break;
                case SetupMode.Restore: boxes = new TextBox[] { RestoreTargetBox }; break;
                default: boxes = new TextBox[] { UninstallTargetBox }; break;
            }
            string[] values = new string[boxes.Length];
            for (int i = 0; i < boxes.Length; i++) values[i] = boxes[i] == null ? "" : boxes[i].Text;
            SavedPaths[RenderedLocationMode] = values;
        }

        private string GetSavedPath(SetupMode mode, int index, string fallback)
        {
            if (UninstallerOnly && FixedTarget.Length > 0) return FixedTarget;
            string[] values;
            return SavedPaths.TryGetValue(mode, out values) && index < values.Length && !String.IsNullOrWhiteSpace(values[index])
                ? values[index] : fallback;
        }

        private TextBox AddPathRow(string caption, string initial, ref int y, bool browse)
        {
            int width = Math.Max(650, LocationGroup.ClientSize.Width - 37);
            Panel row = new Panel { Location = new Point(14, y), Size = new Size(width, 57), Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right };
            Label label = NewLabel(caption, true);
            label.Location = new Point(0, 0);
            label.Size = new Size(width, 18);
            TextBox box = new TextBox
            {
                Text = initial,
                Location = new Point(0, 22),
                Size = new Size(width - (browse ? 105 : 0), 25),
                BorderStyle = BorderStyle.FixedSingle,
                Font = new Font("Segoe UI", 9F),
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                ReadOnly = UninstallerOnly
            };
            row.Controls.Add(label);
            row.Controls.Add(box);
            if (browse)
            {
                Button button = NewButton("Examinar…", false);
                button.Location = new Point(width - 96, 20);
                button.Size = new Size(96, 28);
                button.Anchor = AnchorStyles.Top | AnchorStyles.Right;
                button.Click += delegate { BrowseFolder(box); };
                row.Controls.Add(button);
            }
            row.Resize += delegate
            {
                label.Width = row.ClientSize.Width;
                box.Width = row.ClientSize.Width - (browse ? 105 : 0);
                Control button = row.Controls.Count > 2 ? row.Controls[2] : null;
                if (button != null) button.Left = row.ClientSize.Width - button.Width;
            };
            LocationGroup.Controls.Add(row);
            y += 58;
            return box;
        }

        private void BuildOptions(SetupMode mode)
        {
            OptionsGroup.SuspendLayout();
            OptionsGroup.Controls.Clear();
            int width = Math.Max(650, OptionsGroup.ClientSize.Width - 37);
            int height = 78;
            if (mode == SetupMode.Fresh)
            {
                PortableRadio = new RadioButton { Text = "Instancia portable", Checked = FreshPortableSelected, Location = new Point(14, 22), AutoSize = true, ForeColor = Navy };
                StandardRadio = new RadioButton { Text = "Instancia estándar", Checked = !FreshPortableSelected, Location = new Point(170, 22), AutoSize = true, ForeColor = Navy };
                StartMenuCheck = new CheckBox { Text = "Acceso en menú Inicio", Checked = true, Location = new Point(14, 51), AutoSize = true };
                DesktopCheck = new CheckBox { Text = "Acceso en escritorio", Location = new Point(220, 51), AutoSize = true };
                LaunchCheck = new CheckBox { Text = "Abrir MO2 al terminar", Checked = true, Location = new Point(430, 51), AutoSize = true };
                OptionsGroup.Controls.Add(PortableRadio);
                OptionsGroup.Controls.Add(StandardRadio);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                string currentTarget = FreshTargetBox == null ? "" : FreshTargetBox.Text;
                FreshAutoPath = SameInstallPath(currentTarget, FreshDefaultTarget(true)) || SameInstallPath(currentTarget, FreshDefaultTarget(false))
                    ? currentTarget : "";
                PortableRadio.CheckedChanged += delegate
                {
                    if (!PortableRadio.Checked) return;
                    FreshPortableSelected = true;
                    UpdateFreshInstallTarget(true);
                };
                StandardRadio.CheckedChanged += delegate
                {
                    if (!StandardRadio.Checked) return;
                    FreshPortableSelected = false;
                    UpdateFreshInstallTarget(false);
                };

                Label hint = NewLabel("Portable guarda los datos junto a MO2; estándar usa la carpeta de datos de Windows.", false);
                hint.Location = new Point(14, 77);
                hint.Size = new Size(width, 19);
                hint.AutoSize = false;
                OptionsGroup.Controls.Add(hint);
                height = 108;
            }
            else if (mode == SetupMode.EldenRingPortable)
            {
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 14, 25);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 220, 25);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 430, 25);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                Label hint = NewLabel("Instalación portable aislada en su propia carpeta. Sus perfiles, mods y ajustes quedarán allí. Solo usará Elden Ring y no cargará instancias globales de MO2.", false);
                hint.Location = new Point(14, 52);
                hint.Size = new Size(width, 34);
                hint.AutoSize = false;
                hint.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                OptionsGroup.Controls.Add(hint);
                height = 88;
            }
            else if (mode == SetupMode.Update)
            {
                Label note = NewLabel("Los archivos reemplazados se guardan en una copia previa. Perfiles, mods, descargas y plugins adicionales se mantienen.", false);
                note.Location = new Point(14, 23);
                note.Size = new Size(width, 32);
                note.AutoSize = false;
                note.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                OptionsGroup.Controls.Add(note);
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 14, 57);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 220, 57);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 430, 57);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                height = 91;
            }
            else if (mode == SetupMode.Migrate)
            {
                ProfilesCheck = NewCheck("Perfiles y partidas", true, 14, 24);
                ModsCheck = NewCheck("Mods instalados", true, 14, 49);
                DownloadsCheck = NewCheck("Descargas", true, 14, 74);
                OverwriteCheck = NewCheck("Overwrite", true, 270, 24);
                SettingsCheck = NewCheck("Preferencias MO2", true, 270, 49);
                CategoriesCheck = NewCheck("Categorías y mapa Nexus", true, 270, 74);
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 14, 99);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 270, 99);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 14, 124);
                OptionsGroup.Controls.Add(ProfilesCheck);
                OptionsGroup.Controls.Add(ModsCheck);
                OptionsGroup.Controls.Add(DownloadsCheck);
                OptionsGroup.Controls.Add(OverwriteCheck);
                OptionsGroup.Controls.Add(SettingsCheck);
                OptionsGroup.Controls.Add(CategoriesCheck);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                height = 148;
            }
            else
            {
                Label note;
                if (mode == SetupMode.Restore)
                    note = NewLabel("Recupera la versión anterior guardada al actualizar. Mantiene perfiles, mods, descargas y cualquier archivo que haya cambiado después.", false);
                else if (mode == SetupMode.Uninstall)
                {
                    note = NewLabel("La opción marcada borra todo lo que haya en la carpeta elegida. Desmárcala para quitar MO2 y conservar los datos de usuario, como perfiles, mods, descargas y overwrite.", false);
                    note.Location = new Point(14, 23);
                    note.Size = new Size(width, 34);
                    note.AutoSize = false;
                    note.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                    OptionsGroup.Controls.Add(note);
                    ForceCloseCheck = NewCheck("Forzar el cierre de MO2 en esta carpeta si sigue abierto", false, 14, 61);
                    RemoveAllContentsCheck = NewCheck("Borrar todos los datos y la carpeta seleccionada", false, 14, 86);
                    OptionsGroup.Controls.Add(ForceCloseCheck);
                    OptionsGroup.Controls.Add(RemoveAllContentsCheck);
                    UpdateUninstallOptionsAvailability();
                    height = 115;
                }
                else
                    note = NewLabel("Una actualización restaura el MO2 anterior. Una instalación nueva retira solo los archivos de MO2 Revamped que sigan intactos. Los datos de usuario y archivos modificados se conservan.", false);
                if (mode != SetupMode.Uninstall)
                {
                    note.Location = new Point(14, 23);
                    note.Size = new Size(width, 43);
                    note.AutoSize = false;
                    note.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                    OptionsGroup.Controls.Add(note);
                    height = 78;
                }
            }
            OptionsGroup.Height = height;
            OptionsGroup.ResumeLayout(true);
        }

        private void UpdateUninstallOptionsAvailability()
        {
            if (RemoveAllContentsCheck == null || UninstallTargetBox == null) return;
            InstallState existingState = SetupEngine.LoadState(UninstallTargetBox.Text.Trim());
            bool hasPreviousInstall = existingState != null && !String.IsNullOrWhiteSpace(existingState.BackupDirectory);
            RemoveAllContentsCheck.Enabled = true;
            RemoveAllContentsCheck.Text = hasPreviousInstall
                ? "Borrar todos los datos y la carpeta seleccionada, incluida la copia previa"
                : "Borrar todos los datos y la carpeta seleccionada";
        }

        private CheckBox NewCheck(string text, bool check, int x, int y)
        {
            return new CheckBox { Text = text, Checked = check, Location = new Point(x, y), AutoSize = true, Font = new Font("Segoe UI", 8.5F) };
        }

        private void BrowseFolder(TextBox box)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = "Selecciona una carpeta";
                dialog.ShowNewFolderButton = true;
                if (Directory.Exists(box.Text.Trim())) dialog.SelectedPath = box.Text.Trim();
                if (dialog.ShowDialog(this) == DialogResult.OK) box.Text = dialog.SelectedPath;
            }
        }

        private void AutoFindDataDirectory()
        {
            if (SourceInstallBox == null || SourceDataBox == null) return;
            string source = SourceInstallBox.Text.Trim();
            if (File.Exists(Path.Combine(source, "ModOrganizer.ini")))
            {
                SourceDataBox.Text = source;
                return;
            }
            string globalRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "ModOrganizer");
            if (!Directory.Exists(globalRoot)) return;
            List<string> candidates = new List<string>();
            try
            {
                foreach (string child in Directory.GetDirectories(globalRoot))
                    if (File.Exists(Path.Combine(child, "ModOrganizer.ini"))) candidates.Add(child);
            }
            catch { }
            if (candidates.Count == 1) SourceDataBox.Text = candidates[0];
        }

        private TextBox SelectedTargetBox
        {
            get
            {
                switch (SelectedMode)
                {
                    case SetupMode.Update: return UpdateTargetBox;
                    case SetupMode.Migrate: return MigrateTargetBox;
                    case SetupMode.Restore: return RestoreTargetBox;
                    case SetupMode.Uninstall: return UninstallTargetBox;
                    default: return FreshTargetBox;
                }
            }
        }

        private string TargetForMode()
        {
            TextBox box = SelectedTargetBox;
            return box == null ? "" : box.Text.Trim();
        }

        private bool ValidateOperation()
        {
            SetupMode mode = SelectedMode;
            string target = TargetForMode();
            if (String.IsNullOrWhiteSpace(target)) return ShowValidationError("Indica una carpeta válida.", "Falta la ubicación");
            try { target = Path.GetFullPath(target); }
            catch { return ShowValidationError("La ruta indicada no es válida.", "Ubicación no válida"); }

            if (mode == SetupMode.Fresh || mode == SetupMode.EldenRingPortable || mode == SetupMode.Migrate)
            {
                if (mode == SetupMode.Migrate)
                {
                    string sourceInstall = SourceInstallBox.Text.Trim();
                    string sourceData = SourceDataBox.Text.Trim();
                    if (!File.Exists(Path.Combine(sourceInstall, "ModOrganizer.exe")))
                        return ShowValidationError("Selecciona la carpeta de origen que contiene ModOrganizer.exe.", "Origen no encontrado");
                    if (!Directory.Exists(sourceData) || !HasSelectedData(sourceData))
                        return ShowValidationError("Selecciona una carpeta de datos válida y marca al menos una categoría que exista.", "Datos de origen no válidos");
                    if (PathsOverlap(target, sourceData) || PathsOverlap(target, sourceInstall))
                        return ShowValidationError("El origen y el destino deben ser carpetas separadas.", "Carpetas solapadas");
                }
                if (!TargetIsEmptyOrMissing(target)) return false;
            }
            else if (mode == SetupMode.Update)
            {
                string exe = Path.Combine(target, "ModOrganizer.exe");
                if (!File.Exists(exe)) return ShowValidationError("No se encontró ModOrganizer.exe. Para una carpeta nueva, elige «Instalación nueva».", "MO2 no encontrado");
                string versionText = FileVersionInfo.GetVersionInfo(exe).FileVersion;
                Version version;
                if (!Version.TryParse(NormalizeVersion(versionText), out version) || version < new Version(2, 5, 0, 0) || version > new Version(2, 5, 2, 0))
                    return ShowValidationError("La actualización directa admite MO2 2.5.0 a 2.5.2. Usa «Convertir o copiar datos» para otras versiones.", "Versión no compatible");
            }
            else if (mode == SetupMode.Restore)
            {
                InstallState state = SetupEngine.LoadState(target);
                if (!Directory.Exists(target) || state == null || IsFinishedState(state.Status))
                    return ShowValidationError("No hay una instalación de MO2 Revamped con una copia previa pendiente en esta carpeta.", "Instalación no encontrada");
                if (!HasRestoreBackup(state))
                    return ShowValidationError("Esta instalación no tiene una copia previa válida. «Restaurar MO2 anterior» solo está disponible después de una actualización con copia de seguridad.", "Copia previa no encontrada");
            }
            else if (mode == SetupMode.Uninstall)
            {
                if (!Directory.Exists(target) || !File.Exists(Path.Combine(target, "ModOrganizer.exe")))
                    return ShowValidationError("Selecciona la carpeta de una instalación de MO2 que contenga ModOrganizer.exe. Se admiten MO2 Revamped y versiones originales.", "MO2 no encontrado");
            }
            return true;
        }

        private bool ShowValidationError(string message, string title)
        {
            MessageBox.Show(this, message, title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }

        private bool TargetIsEmptyOrMissing(string target)
        {
            if (!Directory.Exists(target)) return true;
            if (File.Exists(Path.Combine(target, "ModOrganizer.exe")))
                return ShowValidationError("Esta carpeta ya contiene MO2. Elige «Actualizar MO2 existente» o crea otra carpeta.", "La carpeta ya contiene MO2");
            string[] entries;
            try { entries = Directory.GetFileSystemEntries(target); }
            catch { return ShowValidationError("No se pudo leer la carpeta de destino.", "Acceso denegado"); }
            if (entries.Length == 0) return true;
            return ShowValidationError("El destino contiene archivos. Elige una carpeta vacía para evitar sobrescribirlos.", "El destino no está vacío");
        }

        private bool HasSelectedData(string root)
        {
            return SetupEngine.HasSelectedMigrationData(new SetupRequest
            {
                SourceDataDirectory = root,
                CopyProfiles = ProfilesCheck.Checked,
                CopyMods = ModsCheck.Checked,
                CopyDownloads = DownloadsCheck.Checked,
                CopyOverwrite = OverwriteCheck.Checked,
                CopySettings = SettingsCheck.Checked,
                CopyCategories = CategoriesCheck.Checked
            });
        }

        private bool PathsOverlap(string a, string b)
        {
            try
            {
                string left = Path.GetFullPath(a).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                string right = Path.GetFullPath(b).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                return String.Equals(left, right, StringComparison.OrdinalIgnoreCase) ||
                    left.StartsWith(right + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase) ||
                    right.StartsWith(left + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
            }
            catch { return true; }
        }

        private string NormalizeVersion(string value)
        {
            if (String.IsNullOrWhiteSpace(value)) return "";
            while (value.Split('.').Length < 4) value += ".0";
            return value;
        }

        private bool IsFinishedState(string status)
        {
            return String.Equals(status, "Restored", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(status, "Uninstalled", StringComparison.OrdinalIgnoreCase);
        }

        private bool HasRestoreBackup(InstallState state)
        {
            if (state == null || String.IsNullOrWhiteSpace(state.BackupDirectory) || !Directory.Exists(state.BackupDirectory)) return false;
            foreach (InstallFileState file in state.Files)
            {
                if (!file.ExistedBefore || file.IsUserData) continue;
                try
                {
                    string path = Path.GetFullPath(Path.Combine(state.BackupDirectory, file.Path));
                    string root = Path.GetFullPath(state.BackupDirectory).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                    if (path.StartsWith(root, StringComparison.OrdinalIgnoreCase) && File.Exists(path)) return true;
                }
                catch { }
            }
            return false;
        }

        private bool ConfirmOperation()
        {
            SetupMode mode = SelectedMode;
            string target = TargetForMode();
            string title;
            string body;
            if (mode == SetupMode.Restore)
            {
                InstallState state = SetupEngine.LoadState(target);
                title = "Restaurar MO2 anterior";
                body = "Se recuperarán los archivos originales guardados para:\n\n" + target +
                    "\n\nCopia previa: " + (state == null ? "no encontrada" : state.BackupDirectory) +
                    "\n\nSe quitarán los accesos y la entrada de MO2 Revamped. Los perfiles, mods, descargas y archivos modificados se conservarán. ¿Continuar?";
            }
            else if (mode == SetupMode.Uninstall)
            {
                InstallState state = SetupEngine.LoadState(target);
                bool trackedInstall = state != null && !IsFinishedState(state.Status);
                title = "Desinstalar MO2 Revamped";
                body = "Se desinstalará esta instalación de MO2 Revamped:\n\n" + target +
                    (RemoveAllContentsCheck != null && RemoveAllContentsCheck.Checked
                        ? "\n\nSe borrará todo lo que haya dentro de la carpeta seleccionada, incluidos perfiles, mods, descargas, ajustes y archivos añadidos. También se quitará la carpeta seleccionada y las contenedoras que queden vacías, hasta llegar a una carpeta con contenido o protegida por Windows. Los datos fuera de esta ruta no se tocarán. Esta acción no se puede deshacer."
                        : "\n\nSe quitarán las carpetas de componentes reconocidas y se conservarán los datos de usuario, como perfiles, mods, descargas, overwrite y ajustes. Los demás archivos fuera de esas carpetas también se mantienen. " + (trackedInstall ? "Si hay una copia previa, se restaurará la versión original." : "Los datos de usuario se mantendrán en su ubicación actual.")) +
                    (ForceCloseCheck != null && ForceCloseCheck.Checked
                        ? "\n\nSi MO2 sigue abierto, se intentará cerrarlo y se forzará el cierre de esta instalación si no responde; podrías perder cambios sin guardar."
                        : "") + "\n\n¿Continuar?";
            }
            else if (mode == SetupMode.Update)
            {
                title = "Actualizar MO2 existente";
                body = "Se actualizarán los archivos del programa en:\n\n" + target +
                    "\n\nLos archivos previos se guardarán para poder restaurarlos. ¿Continuar?";
            }
            else if (mode == SetupMode.Migrate)
            {
                title = "Convertir o copiar datos";
                body = "Se creará una instalación portable en:\n\n" + target +
                    "\n\nEl origen se conservará. Categorías seleccionadas: " + SelectedDataNames() + ". ¿Continuar?";
            }
            else if (mode == SetupMode.EldenRingPortable)
            {
                title = "Instalación portable solo para Elden Ring";
                body = "La edición portable aislada de MO2 se instalará en:\n\n" + target +
                    "\n\nSolo incluirá soporte para Elden Ring y no leerá ni modificará las instancias globales de otras copias de MO2. ¿Continuar?";
            }
            else
            {
                title = "Instalación nueva";
                body = "MO2 Revamped se instalará en:\n\n" + target + "\n\n¿Continuar?";
            }
            return MessageBox.Show(this, body, title, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
        }

        private string SelectedDataNames()
        {
            List<string> names = new List<string>();
            if (ProfilesCheck.Checked) names.Add("perfiles");
            if (ModsCheck.Checked) names.Add("mods");
            if (DownloadsCheck.Checked) names.Add("descargas");
            if (OverwriteCheck.Checked) names.Add("overwrite");
            if (SettingsCheck.Checked) names.Add("preferencias");
            if (CategoriesCheck.Checked) names.Add("categorías");
            return names.Count == 0 ? "ninguna" : String.Join(", ", names.ToArray());
        }

        private void StartOperation()
        {
            if (IsWorking || HasCompleted || !ValidateOperation() || !ConfirmOperation()) return;
            SetupMode mode = SelectedMode;
            SetupRequest request = new SetupRequest
            {
                Mode = mode,
                TargetDirectory = Path.GetFullPath(TargetForMode()),
                SourceInstallDirectory = SourceInstallBox == null ? "" : SourceInstallBox.Text.Trim(),
                SourceDataDirectory = SourceDataBox == null ? "" : SourceDataBox.Text.Trim(),
                InstanceKind = mode == SetupMode.Migrate || mode == SetupMode.EldenRingPortable || (mode == SetupMode.Fresh && PortableRadio.Checked) ? InstanceKind.Portable : InstanceKind.Standard,
                CopyProfiles = ProfilesCheck != null && ProfilesCheck.Checked,
                CopyMods = ModsCheck != null && ModsCheck.Checked,
                CopyDownloads = DownloadsCheck != null && DownloadsCheck.Checked,
                CopyOverwrite = OverwriteCheck != null && OverwriteCheck.Checked,
                CopySettings = SettingsCheck != null && SettingsCheck.Checked,
                CopyCategories = CategoriesCheck != null && CategoriesCheck.Checked,
                StartMenuShortcut = StartMenuCheck != null && StartMenuCheck.Checked,
                DesktopShortcut = DesktopCheck != null && DesktopCheck.Checked,
                LaunchWhenDone = LaunchCheck != null && LaunchCheck.Checked && mode != SetupMode.Restore && mode != SetupMode.Uninstall,
                ForceCloseMo2 = ForceCloseCheck != null && ForceCloseCheck.Checked,
                RemoveAllRemainingContents = RemoveAllContentsCheck != null && RemoveAllContentsCheck.Checked
            };
            IsWorking = true;
            ActionBox.Enabled = false;
            ActionGroup.Enabled = false;
            LocationGroup.Enabled = false;
            OptionsGroup.Enabled = false;
            ActionButton.Enabled = false;
            CloseButton.Enabled = false;
            ProgressDetail.Text = mode == SetupMode.Restore ? "Preparando la restauración…" :
                mode == SetupMode.Uninstall ? "Preparando la desinstalación…" : "Preparando la instalación…";
            ProgressDetail.ForeColor = Navy;
            ProgressBar.Value = 0;
            ProgressLog.Clear();
            ProgressLog.Visible = true;
            LastLoggedStage = "";
            Worker.RunWorkerAsync(request);
        }

        private void WorkerDoWork(object sender, DoWorkEventArgs e)
        {
            SetupRequest request = (SetupRequest)e.Argument;
            SetupDiagnostics.BeginOperation(request);
            try
            {
                e.Result = SetupEngine.Execute(request, Worker);
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Operation failed; the installer returned an exception.", ex);
                throw;
            }
        }

        private string ProgressStage(string message)
        {
            int marker = message.IndexOf(" (", StringComparison.Ordinal);
            if (marker >= 0) return message.Substring(0, marker);
            return message.TrimEnd('.', '…');
        }

        private void WorkerProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            ProgressBar.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            string message = Convert.ToString(e.UserState);
            if (String.IsNullOrWhiteSpace(message)) return;
            ProgressDetail.Text = message;
            string stage = ProgressStage(message);
            if (!String.Equals(stage, LastLoggedStage, StringComparison.Ordinal))
            {
                LastLoggedStage = stage;
                ProgressLog.AppendText(stage + Environment.NewLine);
                string[] rows = ProgressLog.Lines;
                if (rows.Length > 4)
                {
                    string[] tail = new string[4];
                    Array.Copy(rows, rows.Length - 4, tail, 0, 4);
                    ProgressLog.Lines = tail;
                }
            }
        }

        private void WorkerCompleted(object sender, RunWorkerCompletedEventArgs e)
        {
            IsWorking = false;
            HasCompleted = true;
            ActionBox.Enabled = false;
            ActionButton.Visible = false;
            CloseButton.Enabled = true;
            if (e.Error != null)
            {
                ProgressDetail.Text = "La operación no pudo completarse.";
                ProgressDetail.ForeColor = Color.FromArgb(171, 68, 68);
                string logPath = SetupDiagnostics.CurrentPath;
                ProgressLog.Text = e.Error.GetBaseException().Message +
                    (String.IsNullOrEmpty(logPath)
                        ? Environment.NewLine + "No se pudo guardar el registro detallado."
                        : Environment.NewLine + Environment.NewLine + "Registro detallado: " + logPath);
                ProgressLog.Visible = true;
                return;
            }
            OperationResult result = (OperationResult)e.Result;
            SetupDiagnostics.Info("Operation completed: " + result.Message +
                (String.IsNullOrEmpty(result.Details) ? "" : Environment.NewLine + result.Details));
            ProgressBar.Value = 100;
            ProgressDetail.Text = result.Message;
            ProgressDetail.ForeColor = result.Success ? Color.FromArgb(44, 126, 82) : Color.FromArgb(173, 106, 28);
            string logPathOnSuccess = SetupDiagnostics.CurrentPath;
            ProgressLog.Text = result.Details +
                (String.IsNullOrEmpty(logPathOnSuccess)
                    ? Environment.NewLine + Environment.NewLine + "No se pudo guardar el registro detallado."
                    : Environment.NewLine + Environment.NewLine + "Registro detallado: " + logPathOnSuccess);
            ProgressLog.Visible = true;
            if (!String.IsNullOrEmpty(result.LaunchPath))
            {
                try { Process.Start(new ProcessStartInfo("explorer.exe", "\"" + result.LaunchPath + "\"") { UseShellExecute = true }); }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("MO2 could not be opened after setup completed.", ex);
                    ProgressLog.AppendText(Environment.NewLine + "No se pudo abrir MO2: " + ex.Message);
                }
            }
        }

        private void ActionButtonClick(object sender, EventArgs e)
        {
            if (HasCompleted) Close();
            else StartOperation();
        }

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (IsWorking)
            {
                e.Cancel = true;
                MessageBox.Show(this, "Espera a que termine el proceso antes de cerrar el asistente.", "Proceso en curso", MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            base.OnFormClosing(e);
        }
    }
}
