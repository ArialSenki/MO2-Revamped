using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Windows.Forms;

namespace Mo2RevampedSetup
{
    internal sealed class CompactSetupForm : Form
    {
        private sealed class InstallerPalette
        {
            public Color Page { get; private set; }
            public Color Surface { get; private set; }
            public Color Input { get; private set; }
            public Color Border { get; private set; }
            public Color BorderHover { get; private set; }
            public Color Heading { get; private set; }
            public Color Text { get; private set; }
            public Color Muted { get; private set; }
            public Color Accent { get; private set; }
            public Color AccentHover { get; private set; }
            public Color AccentPressed { get; private set; }
            public Color Selected { get; private set; }
            public Color Hover { get; private set; }
            public Color IconTile { get; private set; }
            public Color IconSelected { get; private set; }
            public Color IconInk { get; private set; }
            public Color Success { get; private set; }
            public Color Error { get; private set; }
            public Color Warning { get; private set; }
            public bool IsDark { get; private set; }

            private InstallerPalette() { }

            public static InstallerPalette Detect()
            {
                bool dark = false;
                try
                {
                    object value = Registry.GetValue(
                        @"HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize",
                        "AppsUseLightTheme", 1);
                    dark = value != null && Convert.ToInt32(value) == 0;
                }
                catch { }

                InstallerPalette palette = new InstallerPalette();
                if (dark)
                {
                    palette.IsDark = true;
                    palette.Page = Color.FromArgb(32, 38, 43);
                    palette.Surface = Color.FromArgb(37, 43, 48);
                    palette.Input = Color.FromArgb(31, 37, 42);
                    palette.Border = Color.FromArgb(65, 77, 86);
                    palette.BorderHover = Color.FromArgb(82, 109, 125);
                    palette.Heading = Color.FromArgb(225, 237, 245);
                    palette.Text = Color.FromArgb(211, 221, 228);
                    palette.Muted = Color.FromArgb(157, 174, 185);
                    palette.Accent = Color.FromArgb(45, 102, 135);
                    palette.AccentHover = Color.FromArgb(57, 121, 155);
                    palette.AccentPressed = Color.FromArgb(37, 79, 104);
                    palette.Selected = Color.FromArgb(53, 73, 86);
                    palette.Hover = Color.FromArgb(43, 51, 57);
                    palette.IconTile = Color.FromArgb(48, 58, 65);
                    palette.IconSelected = Color.FromArgb(53, 73, 86);
                    palette.IconInk = Color.FromArgb(137, 188, 218);
                    palette.Success = Color.FromArgb(128, 194, 148);
                    palette.Error = Color.FromArgb(225, 137, 129);
                    palette.Warning = Color.FromArgb(225, 183, 108);
                }
                else
                {
                    palette.IsDark = false;
                    palette.Page = Color.FromArgb(244, 247, 250);
                    palette.Surface = Color.White;
                    palette.Input = Color.FromArgb(248, 250, 252);
                    palette.Border = Color.FromArgb(207, 220, 230);
                    palette.BorderHover = Color.FromArgb(183, 205, 220);
                    palette.Heading = Color.FromArgb(36, 79, 108);
                    palette.Text = Color.FromArgb(41, 56, 71);
                    palette.Muted = Color.FromArgb(82, 110, 132);
                    palette.Accent = Color.FromArgb(54, 133, 180);
                    palette.AccentHover = Color.FromArgb(43, 124, 174);
                    palette.AccentPressed = Color.FromArgb(35, 105, 150);
                    palette.Selected = Color.FromArgb(215, 234, 249);
                    palette.Hover = Color.FromArgb(237, 244, 249);
                    palette.IconTile = Color.FromArgb(228, 240, 248);
                    palette.IconSelected = Color.FromArgb(216, 235, 246);
                    palette.IconInk = Color.FromArgb(39, 113, 157);
                    palette.Success = Color.FromArgb(44, 126, 82);
                    palette.Error = Color.FromArgb(171, 68, 68);
                    palette.Warning = Color.FromArgb(173, 106, 28);
                }
                return palette;
            }
        }

        private static readonly InstallerPalette Theme = InstallerPalette.Detect();
        private static readonly Color Navy = Theme.Heading;
        private static readonly Color Accent = Theme.Accent;
        private static readonly Color Page = Theme.Page;
        private static readonly Color Border = Theme.Border;
        private static readonly Color TextColor = Theme.Text;
        private static readonly Color Muted = Theme.Muted;
        private static readonly Color SelectedCard = Theme.Selected;
        private static readonly Color HoverCard = Theme.Hover;
        private static readonly Color IconTile = Theme.IconTile;
        private static readonly Color White = Theme.Surface;

        private readonly BackgroundWorker Worker = new BackgroundWorker();
        [DllImport("dwmapi.dll", PreserveSig = true)]
        private static extern int DwmSetWindowAttribute(IntPtr window, int attribute, ref int value, int valueSize);

        private readonly bool UninstallerOnly;
        private readonly string FixedTarget;
        private ComboBox LanguageBox;
        private LanguagePickerControl LanguagePicker;
        private ContextMenuStrip LanguageMenu;
        private ComboBox ActionBox;
        private Label HeaderTitle;
        private Label HeaderSubtitle;
        private Label ActionDescription;
        private ActionIconControl ModeSummaryIcon;
        private TableLayoutPanel ActionGrid;
        private readonly List<Panel> ActionCards = new List<Panel>();
        private readonly List<RadioButton> ActionCardRadios = new List<RadioButton>();
        private readonly List<ActionIconControl> ActionCardIcons = new List<ActionIconControl>();
        private FlowLayoutPanel Content;
        private GroupBox ActionGroup;
        private GroupBox ModeSummaryGroup;
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
        private string FreshTargetPath;
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
        private CheckBox RemoveRevampedAppDataCheck;
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

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            if (!Theme.IsDark) return;

            try
            {
                int dark = 1;
                if (DwmSetWindowAttribute(Handle, 20, ref dark, sizeof(int)) != 0)
                    DwmSetWindowAttribute(Handle, 19, ref dark, sizeof(int));
            }
            catch { }
        }

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

            public override string ToString() { return InstallerText.T(Title); }
        }

        private sealed class LanguageChoice
        {
            public readonly string Code;
            public readonly string Name;
            public LanguageChoice(string code, string name) { Code = code; Name = name; }
            public override string ToString() { return Name; }
        }

        private sealed class ActionIconControl : Control
        {
            public SetupMode Mode;
            public bool Selected;

            public ActionIconControl(SetupMode mode, string accessibleName)
            {
                Mode = mode;
                AccessibleName = accessibleName;
                AccessibleRole = AccessibleRole.Graphic;
                Cursor = Cursors.Hand;
                SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                    ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
                BackColor = White;
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaintBackground(e);
                Graphics graphics = e.Graphics;
                graphics.SmoothingMode = SmoothingMode.AntiAlias;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;

                float diameter = Math.Min(ClientSize.Width, ClientSize.Height) - 3F;
                RectangleF circle = new RectangleF(
                    (ClientSize.Width - diameter) / 2F,
                    (ClientSize.Height - diameter) / 2F,
                    diameter, diameter);
                Color circleColor = Selected ? Theme.IconSelected : IconTile;
                using (SolidBrush background = new SolidBrush(circleColor))
                    graphics.FillEllipse(background, circle);

                Color ink = Theme.IconInk;
                GraphicsState state = graphics.Save();
                float scale = Math.Min(ClientSize.Width, ClientSize.Height) / 30F * 1.12F;
                graphics.TranslateTransform(
                    (ClientSize.Width - 30F * scale) / 2F,
                    (ClientSize.Height - 30F * scale) / 2F);
                graphics.ScaleTransform(scale, scale);
                using (Pen pen = new Pen(ink, 1.7F))
                {
                    pen.StartCap = LineCap.Round;
                    pen.EndCap = LineCap.Round;
                    pen.LineJoin = LineJoin.Round;
                    DrawModeIcon(graphics, pen);
                }
                graphics.Restore(state);
            }

            private void DrawModeIcon(Graphics graphics, Pen pen)
            {
                switch (Mode)
                {
                    case SetupMode.Fresh:
                        DrawNewInstall(graphics, pen);
                        break;
                    case SetupMode.EldenRingPortable:
                        DrawIsolatedCopy(graphics, pen);
                        break;
                    case SetupMode.Update:
                        DrawUpdate(graphics, pen);
                        break;
                    case SetupMode.Migrate:
                        DrawTransfer(graphics, pen);
                        break;
                    case SetupMode.Restore:
                        DrawRestore(graphics, pen);
                        break;
                    case SetupMode.Uninstall:
                        DrawUninstall(graphics, pen);
                        break;
                    default:
                        DrawNewInstall(graphics, pen);
                        break;
                }
            }

            private static void DrawNewInstall(Graphics graphics, Pen pen)
            {
                using (GraphicsPath window = new GraphicsPath())
                {
                    window.AddLine(8F, 9F, 22F, 9F);
                    window.AddLine(22F, 9F, 22F, 21F);
                    window.AddLine(22F, 21F, 8F, 21F);
                    window.AddLine(8F, 21F, 8F, 9F);
                    graphics.DrawPath(pen, window);
                }
                graphics.DrawLine(pen, 8.5F, 12F, 21.5F, 12F);
                graphics.DrawLine(pen, 15F, 14.5F, 15F, 19F);
                graphics.DrawLine(pen, 12.75F, 16.75F, 17.25F, 16.75F);
            }

            private static void DrawIsolatedCopy(Graphics graphics, Pen pen)
            {
                using (GraphicsPath folder = new GraphicsPath())
                {
                    folder.AddLine(7F, 11F, 12F, 11F);
                    folder.AddLine(12F, 11F, 14F, 9F);
                    folder.AddLine(14F, 9F, 22F, 9F);
                    folder.AddLine(22F, 9F, 22F, 20F);
                    folder.AddLine(22F, 20F, 7F, 20F);
                    folder.AddLine(7F, 20F, 7F, 11F);
                    graphics.DrawPath(pen, folder);
                }
                graphics.DrawEllipse(pen, 13.5F, 12F, 6F, 6F);
                graphics.DrawLine(pen, 16.5F, 12.5F, 16.5F, 17.5F);
                graphics.DrawLine(pen, 14.5F, 15F, 18.5F, 15F);
            }

            private static void DrawUpdate(Graphics graphics, Pen pen)
            {
                graphics.DrawArc(pen, 8F, 8F, 14F, 14F, 42F, 282F);
                graphics.DrawLine(pen, 20.5F, 8.5F, 20.5F, 12.5F);
                graphics.DrawLine(pen, 20.5F, 8.5F, 16.5F, 8.5F);
            }

            private static void DrawTransfer(Graphics graphics, Pen pen)
            {
                graphics.DrawLine(pen, 8F, 11F, 20F, 11F);
                graphics.DrawLine(pen, 20F, 11F, 17F, 8.5F);
                graphics.DrawLine(pen, 20F, 11F, 17F, 13.5F);
                graphics.DrawLine(pen, 22F, 19F, 10F, 19F);
                graphics.DrawLine(pen, 10F, 19F, 13F, 16.5F);
                graphics.DrawLine(pen, 10F, 19F, 13F, 21.5F);
            }

            private static void DrawRestore(Graphics graphics, Pen pen)
            {
                graphics.DrawArc(pen, 9F, 8F, 13F, 13F, 215F, 275F);
                graphics.DrawLine(pen, 8F, 10F, 8F, 14F);
                graphics.DrawLine(pen, 8F, 10F, 12F, 10F);
            }

            private static void DrawUninstall(Graphics graphics, Pen pen)
            {
                graphics.DrawLine(pen, 9F, 10F, 21F, 10F);
                graphics.DrawLine(pen, 12F, 8F, 18F, 8F);
                graphics.DrawLine(pen, 11F, 12F, 12F, 21F);
                graphics.DrawLine(pen, 19F, 12F, 18F, 21F);
                graphics.DrawLine(pen, 12F, 21F, 18F, 21F);
                graphics.DrawLine(pen, 14F, 13F, 14F, 19F);
                graphics.DrawLine(pen, 16F, 13F, 16F, 19F);
            }
        }

        private sealed class ThemedCheckBox : CheckBox
        {
            private bool Hovered;

            public ThemedCheckBox()
            {
                FlatStyle = FlatStyle.Flat;
                UseVisualStyleBackColor = false;
                FlatAppearance.BorderSize = 0;
                SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                    ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            }

            public override Size GetPreferredSize(Size proposedSize)
            {
                Size textSize = TextRenderer.MeasureText(Text, Font, Size.Empty,
                    TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
                return new Size(textSize.Width + 25, Math.Max(20, textSize.Height + 2));
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                e.Graphics.Clear(BackColor);
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                int boxSize = 15;
                Rectangle bounds = new Rectangle(0, (ClientSize.Height - boxSize) / 2, boxSize, boxSize);
                Color outline = !Enabled ? Border : Hovered ? Theme.Accent : Theme.BorderHover;
                using (GraphicsPath shape = SectionGroupBox.CreateRoundedRectangle(bounds, 4))
                using (SolidBrush fill = new SolidBrush(Checked
                    ? (Enabled ? Accent : Theme.Muted) : BackColor))
                using (Pen edge = new Pen(outline, 1F))
                {
                    e.Graphics.FillPath(fill, shape);
                    e.Graphics.DrawPath(edge, shape);
                }

                if (Checked)
                {
                    using (Pen mark = new Pen(Color.White, 1.8F))
                    {
                        mark.StartCap = LineCap.Round;
                        mark.EndCap = LineCap.Round;
                        mark.LineJoin = LineJoin.Round;
                        e.Graphics.DrawLine(mark, bounds.Left + 3.5F, bounds.Top + 7.5F,
                            bounds.Left + 6.25F, bounds.Top + 10.25F);
                        e.Graphics.DrawLine(mark, bounds.Left + 6.25F, bounds.Top + 10.25F,
                            bounds.Left + 11.5F, bounds.Top + 4.75F);
                    }
                }

                Color textColor = Enabled ? ForeColor : Muted;
                TextRenderer.DrawText(e.Graphics, Text, Font,
                    new Rectangle(24, 0, Math.Max(0, ClientSize.Width - 24), ClientSize.Height),
                    textColor, TextFormatFlags.Left | TextFormatFlags.VerticalCenter |
                    TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
                if (Focused && ShowFocusCues)
                    ControlPaint.DrawFocusRectangle(e.Graphics,
                        new Rectangle(22, 1, Math.Max(0, ClientSize.Width - 23), Math.Max(0, ClientSize.Height - 2)),
                        ForeColor, BackColor);
            }

            protected override void OnMouseEnter(EventArgs e)
            {
                base.OnMouseEnter(e);
                Hovered = true;
                Invalidate();
            }

            protected override void OnMouseLeave(EventArgs e)
            {
                base.OnMouseLeave(e);
                Hovered = false;
                Invalidate();
            }

            protected override void OnCheckStateChanged(EventArgs e)
            {
                base.OnCheckStateChanged(e);
                Invalidate();
            }

            protected override void OnEnabledChanged(EventArgs e)
            {
                base.OnEnabledChanged(e);
                Invalidate();
            }
        }

        private sealed class LanguagePickerControl : Control
        {
            private bool Hovered;
            public string SelectedText;

            public LanguagePickerControl()
            {
                AccessibleRole = AccessibleRole.ComboBox;
                AccessibleName = "Language";
                Cursor = Cursors.Hand;
                TabStop = true;
                SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                    ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaintBackground(e);
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                Rectangle bounds = new Rectangle(0, 0, Math.Max(0, Width - 1), Math.Max(0, Height - 1));
                using (GraphicsPath shape = SectionGroupBox.CreateRoundedRectangle(bounds, 5))
                using (SolidBrush fill = new SolidBrush(Theme.Input))
                using (Pen edge = new Pen(Hovered ? Theme.BorderHover : Border))
                {
                    e.Graphics.FillPath(fill, shape);
                    e.Graphics.DrawPath(edge, shape);
                }

                int divider = Math.Max(0, Width - 29);
                using (Pen edge = new Pen(Border))
                    e.Graphics.DrawLine(edge, divider, 6, divider, Height - 7);
                TextRenderer.DrawText(e.Graphics, SelectedText ?? "", Font,
                    new Rectangle(11, 1, Math.Max(0, divider - 16), Height - 2),
                    TextColor, TextFormatFlags.Left | TextFormatFlags.VerticalCenter |
                    TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);

                float centerX = divider + 14F;
                float centerY = Height / 2F;
                using (Pen chevron = new Pen(Theme.IconInk, 1.7F))
                {
                    chevron.StartCap = LineCap.Round;
                    chevron.EndCap = LineCap.Round;
                    e.Graphics.DrawLine(chevron, centerX - 3F, centerY - 1F,
                        centerX, centerY + 2F);
                    e.Graphics.DrawLine(chevron, centerX, centerY + 2F,
                        centerX + 3F, centerY - 1F);
                }
                if (Focused && ShowFocusCues)
                    ControlPaint.DrawFocusRectangle(e.Graphics, Rectangle.Inflate(bounds, -3, -3), TextColor, Theme.Input);
            }

            protected override void OnMouseEnter(EventArgs e)
            {
                base.OnMouseEnter(e);
                Hovered = true;
                Invalidate();
            }

            protected override void OnMouseLeave(EventArgs e)
            {
                base.OnMouseLeave(e);
                Hovered = false;
                Invalidate();
            }

            protected override void OnKeyDown(KeyEventArgs e)
            {
                base.OnKeyDown(e);
                if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter || e.KeyCode == Keys.Down)
                {
                    OnClick(EventArgs.Empty);
                    e.Handled = true;
                }
            }
        }

        private sealed class InstallerMenuColors : ProfessionalColorTable
        {
            public override Color ToolStripDropDownBackground { get { return Theme.Surface; } }
            public override Color ImageMarginGradientBegin { get { return Theme.Surface; } }
            public override Color ImageMarginGradientMiddle { get { return Theme.Surface; } }
            public override Color ImageMarginGradientEnd { get { return Theme.Surface; } }
            public override Color MenuBorder { get { return Border; } }
            public override Color MenuItemBorder { get { return Theme.BorderHover; } }
            public override Color MenuItemSelected { get { return Theme.Selected; } }
            public override Color MenuItemSelectedGradientBegin { get { return Theme.Selected; } }
            public override Color MenuItemSelectedGradientEnd { get { return Theme.Selected; } }
            public override Color MenuItemPressedGradientBegin { get { return Theme.Selected; } }
            public override Color MenuItemPressedGradientMiddle { get { return Theme.Selected; } }
            public override Color MenuItemPressedGradientEnd { get { return Theme.Selected; } }
        }

        private sealed class BrandMarkControl : Control
        {
            public BrandMarkControl()
            {
                SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                    ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
                BackColor = Page;
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                base.OnPaintBackground(e);
                Graphics graphics = e.Graphics;
                graphics.SmoothingMode = SmoothingMode.AntiAlias;
                graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
                Rectangle bounds = new Rectangle(0, 0, Math.Max(0, Width - 1), Math.Max(0, Height - 1));
                using (GraphicsPath tile = SectionGroupBox.CreateRoundedRectangle(bounds, 10))
                using (SolidBrush background = new SolidBrush(Theme.IconTile))
                using (Pen outline = new Pen(Border))
                {
                    graphics.FillPath(background, tile);
                    graphics.DrawPath(outline, tile);
                }

                float scale = Math.Min(Width, Height) / 46F;
                graphics.TranslateTransform((Width - 36F * scale) / 2F, (Height - 32F * scale) / 2F);
                graphics.ScaleTransform(scale, scale);
                DrawLayer(graphics, 5F, 18F, Color.FromArgb(64, 106, 139));
                DrawLayer(graphics, 5F, 11F, Theme.Accent);
                DrawLayer(graphics, 5F, 4F, Theme.IconInk);
                graphics.ResetTransform();
            }

            private static void DrawLayer(Graphics graphics, float x, float y, Color color)
            {
                PointF[] top = new PointF[] {
                    new PointF(x + 13F, y), new PointF(x + 26F, y + 7F),
                    new PointF(x + 13F, y + 14F), new PointF(x, y + 7F)
                };
                PointF[] left = new PointF[] {
                    new PointF(x, y + 7F), new PointF(x + 13F, y + 14F),
                    new PointF(x + 13F, y + 19F), new PointF(x, y + 12F)
                };
                PointF[] right = new PointF[] {
                    new PointF(x + 13F, y + 14F), new PointF(x + 26F, y + 7F),
                    new PointF(x + 26F, y + 12F), new PointF(x + 13F, y + 19F)
                };
                using (SolidBrush face = new SolidBrush(color))
                using (SolidBrush leftFace = new SolidBrush(ControlPaint.Dark(color, 0.18F)))
                using (SolidBrush rightFace = new SolidBrush(ControlPaint.Dark(color, 0.06F)))
                {
                    graphics.FillPolygon(leftFace, left);
                    graphics.FillPolygon(rightFace, right);
                    graphics.FillPolygon(face, top);
                }
            }
        }

        private sealed class SectionGroupBox : GroupBox
        {
            public SectionGroupBox()
            {
                SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                    ControlStyles.OptimizedDoubleBuffer, true);
                FlatStyle = FlatStyle.Flat;
                // Child controls inherit this color. Keep it aligned with the rounded
                // card body so labels do not paint rectangular page-colored strips.
                BackColor = White;
                ForeColor = Theme.Muted;
                Font = new Font("Segoe UI Semibold", 9F, FontStyle.Bold);
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                Color canvas = Parent == null ? Page : Parent.BackColor;
                e.Graphics.Clear(canvas);

                int inset = 1;
                int top = 1;
                int width = Math.Max(0, ClientSize.Width - 3);
                int height = Math.Max(0, ClientSize.Height - top - 3);
                if (width > 0 && height > 0)
                {
                    using (GraphicsPath outline = CreateRoundedRectangle(
                        new Rectangle(inset, top, width, height), 8))
                    using (SolidBrush fill = new SolidBrush(Theme.Surface))
                    using (Pen pen = new Pen(Border))
                    {
                        e.Graphics.FillPath(fill, outline);
                        e.Graphics.DrawPath(pen, outline);
                    }
                }

                Size titleSize = TextRenderer.MeasureText(Text, Font);
                Rectangle titleRect = new Rectangle(11, 3, titleSize.Width + 12, 21);
                using (SolidBrush background = new SolidBrush(Theme.Surface))
                    e.Graphics.FillRectangle(background, titleRect);
                TextRenderer.DrawText(e.Graphics, Text, Font, titleRect, ForeColor,
                    TextFormatFlags.Left | TextFormatFlags.VerticalCenter |
                    TextFormatFlags.SingleLine | TextFormatFlags.NoPadding);
            }

            public static GraphicsPath CreateRoundedRectangle(Rectangle bounds, int radius)
            {
                int diameter = radius * 2;
                GraphicsPath path = new GraphicsPath();
                path.AddArc(bounds.Left, bounds.Top, diameter, diameter, 180, 90);
                path.AddArc(bounds.Right - diameter, bounds.Top, diameter, diameter, 270, 90);
                path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
                path.AddArc(bounds.Left, bounds.Bottom - diameter, diameter, diameter, 90, 90);
                path.CloseFigure();
                return path;
            }
        }

        public CompactSetupForm(SetupMode? fixedMode, string target, bool uninstallerOnly)
        {
            UninstallerOnly = uninstallerOnly;
            FixedTarget = target ?? "";
            Text = uninstallerOnly ? InstallerText.T("Desinstalar MO2 Revamped") : InstallerText.T("MO2 Revamped");
            StartPosition = FormStartPosition.CenterScreen;
            FormBorderStyle = FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = !uninstallerOnly;
            AutoScaleMode = AutoScaleMode.Font;
            BackColor = Page;
            ForeColor = TextColor;
            Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);
            ClientSize = new Size(1020, 640);
            MinimumSize = new Size(920, 600);
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
            root.Padding = new Padding(16, 10, 16, 10);
            root.ColumnCount = 1;
            root.RowCount = 3;
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 62));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
            Controls.Add(root);

            Panel heading = new Panel { Dock = DockStyle.Fill, BackColor = Page, Padding = new Padding(2, 0, 2, 0) };
            heading.Paint += delegate(object sender, PaintEventArgs e)
            {
                e.Graphics.Clear(Page);
                using (Pen divider = new Pen(Border))
                    e.Graphics.DrawLine(divider, 0, heading.Height - 1, heading.Width, heading.Height - 1);
                using (SolidBrush accent = new SolidBrush(Accent))
                    e.Graphics.FillRectangle(accent, 0, heading.Height - 2, 54, 2);
            };
            BrandMarkControl brand = new BrandMarkControl
            {
                Location = new Point(2, 6),
                Size = new Size(44, 44),
                TabStop = false
            };
            heading.Controls.Add(brand);
            Label title = new Label
            {
                Text = UninstallerOnly ? InstallerText.T("Desinstalar MO2 Revamped") : InstallerText.T("MO2 Revamped"),
                Font = new Font("Segoe UI Semibold", 15F, FontStyle.Bold),
                ForeColor = Navy,
                AutoSize = true,
                Location = new Point(58, 5)
            };
            HeaderTitle = title;
            heading.Controls.Add(title);
            Label subtitle = new Label
            {
                Text = UninstallerOnly ? InstallerText.T("Eliminación segura de esta instalación") : InstallerText.T("Instalación y mantenimiento seguro"),
                Font = new Font("Segoe UI", 8.75F),
                ForeColor = Muted,
                AutoSize = true,
                Location = new Point(59, 32)
            };
            HeaderSubtitle = subtitle;
            heading.Controls.Add(subtitle);
            LanguageBox = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Width = 1,
                Height = 1,
                Visible = false,
                TabStop = false
            };
            string[] languageCodes = InstallerText.LanguageCodes;
            string[] languageNames = InstallerText.LanguageNames;
            for (int i = 0; i < languageCodes.Length; i++)
                LanguageBox.Items.Add(new LanguageChoice(languageCodes[i], languageNames[i]));
            LanguageBox.SelectedIndex = Array.FindIndex(languageCodes,
                delegate(string code) { return String.Equals(code, InstallerText.Language, StringComparison.OrdinalIgnoreCase); });
            LanguageBox.SelectedIndexChanged += LanguageChanged;
            LanguagePicker = new LanguagePickerControl
            {
                Font = new Font("Segoe UI", 9F),
                ForeColor = TextColor,
                Size = new Size(166, 30),
                Anchor = AnchorStyles.Top | AnchorStyles.Right,
                Location = new Point(heading.ClientSize.Width - 168, 12),
                SelectedText = ((LanguageChoice)LanguageBox.SelectedItem).Name
            };
            LanguagePicker.Click += delegate
            {
                if (LanguageMenu != null)
                    LanguageMenu.Show(LanguagePicker, new Point(0, LanguagePicker.Height + 2));
            };
            LanguageMenu = new ContextMenuStrip
            {
                BackColor = Theme.Surface,
                ForeColor = TextColor,
                Font = new Font("Segoe UI", 9F),
                ShowImageMargin = false,
                ShowCheckMargin = true,
                Padding = new Padding(4),
                Renderer = new ToolStripProfessionalRenderer(new InstallerMenuColors())
            };
            for (int i = 0; i < languageCodes.Length; i++)
            {
                ToolStripMenuItem item = new ToolStripMenuItem(languageNames[i])
                {
                    Tag = i,
                    CheckOnClick = false,
                    ForeColor = TextColor,
                    BackColor = Theme.Surface
                };
                item.Click += delegate(object sender, EventArgs eventArgs)
                {
                    ToolStripMenuItem selectedItem = sender as ToolStripMenuItem;
                    if (selectedItem != null) LanguageBox.SelectedIndex = (int)selectedItem.Tag;
                };
                LanguageMenu.Items.Add(item);
            }
            UpdateLanguageMenuSelection();
            heading.Controls.Add(LanguagePicker);
            Action positionLanguagePicker = delegate
            {
                LanguagePicker.Left = heading.ClientSize.Width - LanguagePicker.Width - 2;
            };
            heading.Resize += delegate { positionLanguagePicker(); };
            positionLanguagePicker();
            root.Controls.Add(heading, 0, 0);

            TableLayoutPanel workspace = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                RowCount = 1,
                BackColor = Page,
                Padding = new Padding(0, 8, 0, 0),
                Margin = Padding.Empty,
                CellBorderStyle = TableLayoutPanelCellBorderStyle.None
            };
            workspace.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 252F));
            workspace.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            workspace.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            root.Controls.Add(workspace, 0, 1);

            ActionGroup = NewGroup("¿Qué quieres hacer?", 400);
            ActionGroup.Dock = DockStyle.Fill;
            ActionGroup.Margin = new Padding(0, 0, 10, 0);
            ActionGroup.Padding = new Padding(8, 8, 8, 8);
            Label actionHint = NewLabel("Selecciona una tarea para ver sus opciones y la ubicación afectada.", false);
            actionHint.Location = new Point(14, 27);
            actionHint.Size = new Size(214, 32);
            actionHint.AutoSize = false;
            ActionGroup.Controls.Add(actionHint);

            ActionBox = new ComboBox
            {
                DropDownStyle = ComboBoxStyle.DropDownList,
                Width = 1,
                Height = 1,
                Visible = false,
                TabStop = false
            };
            ConfigureCombo(ActionBox);
            PopulateModeChoices();
            ActionGroup.Controls.Add(ActionBox);
            BuildActionCards(fixedMode);
            workspace.Controls.Add(ActionGroup, 0, 0);

            Content = new FlowLayoutPanel
            {
                Dock = DockStyle.Fill,
                FlowDirection = FlowDirection.TopDown,
                WrapContents = false,
                AutoScroll = true,
                BackColor = Page,
                Padding = new Padding(4, 2, 4, 6),
                Margin = Padding.Empty
            };
            workspace.Controls.Add(Content, 1, 0);

            ModeSummaryGroup = NewGroup("Nueva instalación", 96);
            ActionDescription = NewLabel("Instala MO2 Revamped. Después podrás crear tus instancias desde la aplicación.", false);
            ActionDescription.Location = new Point(72, 31);
            ActionDescription.Size = new Size(660, 39);
            ActionDescription.AutoSize = false;
            ActionDescription.Font = new Font("Segoe UI", 9F);
            ActionDescription.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
            ModeSummaryIcon = new ActionIconControl(SetupMode.Fresh, "MO2 Revamped")
            {
                Location = new Point(15, 29),
                Size = new Size(44, 44),
                TabStop = false
            };
            ModeSummaryGroup.Controls.Add(ModeSummaryIcon);
            ModeSummaryGroup.Controls.Add(ActionDescription);
            Content.Controls.Add(ModeSummaryGroup);

            LocationGroup = NewGroup("Carpeta de trabajo", 104);
            Content.Controls.Add(LocationGroup);

            OptionsGroup = NewGroup("Opciones", 78);
            Content.Controls.Add(OptionsGroup);

            ProgressGroup = NewGroup("Estado", 76);
            ProgressDetail = NewLabel("Listo. Revisa la carpeta seleccionada antes de continuar.", true);
            ProgressDetail.Location = new Point(14, 28);
            ProgressDetail.Size = new Size(680, 20);
            ProgressBar = new ProgressBar
            {
                Location = new Point(14, 53),
                Size = new Size(680, 13),
                Style = ProgressBarStyle.Continuous,
                BackColor = Theme.Input,
                ForeColor = Accent,
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                Visible = false
            };
            ProgressLog = new TextBox
            {
                Location = new Point(14, 77),
                Size = new Size(680, 54),
                Multiline = true,
                ReadOnly = true,
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Theme.Input,
                ForeColor = Muted,
                ScrollBars = ScrollBars.Vertical,
                Text = InstallerText.T("El proceso y los avisos aparecerán aquí."),
                Tag = "El proceso y los avisos aparecerán aquí.",
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                Visible = false
            };
            ProgressGroup.Controls.Add(ProgressDetail);
            ProgressGroup.Controls.Add(ProgressBar);
            ProgressGroup.Controls.Add(ProgressLog);
            Content.Controls.Add(ProgressGroup);

            Panel footer = new Panel { Dock = DockStyle.Fill };
            footer.Paint += delegate(object sender, PaintEventArgs e)
            {
                using (Pen pen = new Pen(Border)) e.Graphics.DrawLine(pen, 0, 0, footer.Width, 0);
                using (SolidBrush accent = new SolidBrush(Accent)) e.Graphics.FillRectangle(accent, 0, 0, 54, 2);
            };
            CloseButton = NewButton("Cerrar", false);
            CloseButton.Size = new Size(112, 34);
            CloseButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            CloseButton.Click += delegate { if (!IsWorking) Close(); };
            ActionButton = NewButton("Instalar", true);
            ActionButton.Size = new Size(178, 34);
            ActionButton.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            ActionButton.Click += ActionButtonClick;
            footer.Controls.Add(CloseButton);
            footer.Controls.Add(ActionButton);
            footer.Resize += delegate
            {
                CloseButton.Left = footer.ClientSize.Width - CloseButton.Width;
                CloseButton.Top = 9;
                ActionButton.Left = CloseButton.Left - ActionButton.Width - 10;
                ActionButton.Top = 9;
            };
            root.Controls.Add(footer, 0, 2);

            ActionBox.SelectedIndexChanged += delegate { UpdateView(); };
            ActionGroup.Resize += delegate
            {
                actionHint.Width = Math.Max(1, ActionGroup.ClientSize.Width - 36);
                if (ActionGrid != null) ActionGrid.Width = Math.Max(1, ActionGroup.ClientSize.Width - 22);
            };
            if (fixedMode.HasValue)
            {
                ActionBox.SelectedIndex = FindModeIndex(fixedMode.Value);
                ActionBox.Enabled = false;
            }
            else ActionBox.SelectedIndex = 0;
            if (UninstallerOnly)
            {
                ActionBox.Enabled = false;
            ActionButton.Text = InstallerText.T("Desinstalar");
            }
            UpdateView();
            ResizeGroups();
            Content.Resize += delegate { ResizeGroups(); };
            FitWindowToContent(false);
        }

        private void PopulateModeChoices()
        {
            ActionBox.Items.Clear();
#if ELDENRING_ONLY_INSTALLER
            ActionBox.Items.Add(new ModeChoice(SetupMode.EldenRingPortable, "Instalar Elden Ring y Nightreign aislado", "Copia separada de MO2, dedicada a Elden Ring y Nightreign."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Update, "Actualizar instalación aislada", "Actualiza solo la copia aislada y conserva sus datos."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Restore, "Restaurar copia anterior", "Recupera la copia previa de una actualización."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Uninstall, "Desinstalar instalación aislada", "Retira esta copia; puedes conservar o borrar sus datos."));
#else
            ActionBox.Items.Add(new ModeChoice(SetupMode.Fresh, "Nueva instalación", "Instala MO2 Revamped. Después podrás crear tus instancias desde la aplicación."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Update, "Actualizar MO2 Revamped", "Actualiza una copia reconocida y crea una copia de seguridad."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Migrate, "Copiar datos", "Elige qué datos copiar; el origen permanece intacto."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Restore, "Restaurar copia anterior", "Recupera la copia previa de una actualización."));
            ActionBox.Items.Add(new ModeChoice(SetupMode.Uninstall, "Desinstalar MO2 Revamped", "Quita Revamped y conserva o elimina sus datos."));
#endif
        }

        private void LanguageChanged(object sender, EventArgs e)
        {
            LanguageChoice selected = LanguageBox.SelectedItem as LanguageChoice;
            if (selected == null) return;
            SetupMode mode = SelectedMode;
            InstallerText.SetLanguage(selected.Code);
            if (LanguagePicker != null)
            {
                LanguagePicker.SelectedText = selected.Name;
                LanguagePicker.Invalidate();
            }
            UpdateLanguageMenuSelection();
            HeaderTitle.Text = InstallerText.T(UninstallerOnly ? "Desinstalar MO2 Revamped" : "MO2 Revamped");
            HeaderSubtitle.Text = InstallerText.T(UninstallerOnly ? "Eliminación segura de esta instalación" : "Instalación y mantenimiento seguro");
            Text = UninstallerOnly ? InstallerText.T("Desinstalar MO2 Revamped") : InstallerText.T("MO2 Revamped");
            TranslateControlTree(this);
            ActionBox.Invalidate();
            ActionBox.SelectedIndex = FindModeIndex(mode);
            ActionButton.Text = InstallerText.T(UninstallerOnly ? "Desinstalar" : GetActionCaptionSource(mode));
            if (!IsWorking && !HasCompleted)
                ProgressDetail.Text = InstallerText.T("Listo. Revisa la carpeta seleccionada antes de continuar.");
            ResizeGroups();
            FitWindowToContent(Visible);
        }

        private void UpdateLanguageMenuSelection()
        {
            if (LanguageMenu == null || LanguageBox == null) return;
            foreach (ToolStripItem entry in LanguageMenu.Items)
            {
                ToolStripMenuItem item = entry as ToolStripMenuItem;
                if (item == null) continue;
                bool selected = Convert.ToInt32(item.Tag) == LanguageBox.SelectedIndex;
                item.Checked = selected;
                item.ForeColor = selected ? Navy : TextColor;
            }
        }

        private void TranslateControlTree(Control parent)
        {
            string source = parent.Tag as string;
            // Once an operation starts, ProgressLog contains live diagnostics,
            // not its placeholder text. Keep that output when the language is
            // changed while the installer is running or after it completes.
            if (parent == ProgressLog && (IsWorking || HasCompleted)) source = null;
            if (source != null) parent.Text = InstallerText.T(source);
            foreach (Control child in parent.Controls) TranslateControlTree(child);
        }

        private void ConfigureCombo(ComboBox combo)
        {
            combo.FlatStyle = FlatStyle.Flat;
            combo.DrawMode = DrawMode.OwnerDrawFixed;
            combo.ItemHeight = 24;
            combo.BackColor = Theme.Input;
            combo.ForeColor = TextColor;
            combo.DrawItem += DrawComboItem;
        }

        private void DrawComboItem(object sender, DrawItemEventArgs e)
        {
            ComboBox combo = sender as ComboBox;
            if (combo == null || e.Bounds.Width <= 0 || e.Bounds.Height <= 0) return;

            bool selected = (e.State & DrawItemState.Selected) == DrawItemState.Selected;
            Color background = selected ? SelectedCard : Theme.Input;
            Color foreground = selected ? Navy : TextColor;
            using (SolidBrush brush = new SolidBrush(background))
                e.Graphics.FillRectangle(brush, e.Bounds);

            string text = e.Index >= 0 && e.Index < combo.Items.Count
                ? Convert.ToString(combo.Items[e.Index]) : combo.Text;
            TextRenderer.DrawText(e.Graphics, text, combo.Font, e.Bounds, foreground,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter |
                TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine);
        }

        private GroupBox NewGroup(string title, int height)
        {
            return new SectionGroupBox
            {
                Text = InstallerText.T(title),
                Tag = title,
                Height = height,
                Width = 850,
                BackColor = Theme.Surface,
                ForeColor = Navy,
                Padding = new Padding(10, 12, 10, 8),
                Margin = new Padding(0, 0, 0, 6)
            };
        }

        private Label NewLabel(string text, bool bold)
        {
            return new Label
            {
                Text = InstallerText.T(text),
                Tag = text,
                ForeColor = bold ? Navy : TextColor,
                Font = new Font(bold ? "Segoe UI Semibold" : "Segoe UI", 9F, bold ? FontStyle.Bold : FontStyle.Regular),
                BackColor = Theme.Surface,
                AutoEllipsis = true
            };
        }

        private Button NewButton(string text, bool primary)
        {
            Button button = new Button
            {
                Text = InstallerText.T(text),
                Tag = text,
                FlatStyle = FlatStyle.Flat,
                BackColor = primary ? Accent : Theme.Surface,
                ForeColor = primary ? Color.White : Navy,
                Font = new Font("Segoe UI Semibold", 9F, FontStyle.Bold),
                Cursor = Cursors.Hand
            };
            button.FlatAppearance.BorderColor = primary ? Accent : Border;
            button.FlatAppearance.BorderSize = primary ? 0 : 1;
            button.FlatAppearance.MouseOverBackColor = primary
                ? Theme.AccentHover : Theme.Hover;
            button.FlatAppearance.MouseDownBackColor = primary
                ? Theme.AccentPressed : Theme.Selected;
            return button;
        }

        private void BuildActionCards(SetupMode? fixedMode)
        {
            ActionCards.Clear();
            ActionCardRadios.Clear();
            ActionCardIcons.Clear();
            int visibleItems = 0;
            for (int index = 0; index < ActionBox.Items.Count; index++)
            {
                ModeChoice choice = (ModeChoice)ActionBox.Items[index];
                if (!fixedMode.HasValue || choice.Mode == fixedMode.Value) visibleItems++;
            }
            int rows = Math.Max(1, visibleItems);
            ActionGroup.Height = 440;
            ActionGrid = new TableLayoutPanel
            {
                Location = new Point(10, 62),
                Size = new Size(220, rows * 56),
                Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                ColumnCount = 1,
                RowCount = rows,
                BackColor = Page,
                CellBorderStyle = TableLayoutPanelCellBorderStyle.None,
                Margin = Padding.Empty,
                Padding = Padding.Empty
            };
            ActionGrid.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            for (int row = 0; row < rows; row++)
                ActionGrid.RowStyles.Add(new RowStyle(SizeType.Absolute, 56F));

            for (int index = 0; index < ActionBox.Items.Count; index++)
            {
                ModeChoice choice = (ModeChoice)ActionBox.Items[index];
                if (fixedMode.HasValue && choice.Mode != fixedMode.Value) continue;
                int itemIndex = index;
                Panel card = new Panel
                {
                    Dock = DockStyle.Fill,
                    Margin = new Padding(2, 2, 2, 2),
                    BackColor = Theme.Surface,
                    Cursor = Cursors.Hand,
                    Tag = itemIndex,
                    TabIndex = ActionCards.Count,
                    TabStop = true,
                    AccessibleRole = AccessibleRole.RadioButton,
                    AccessibleName = InstallerText.T(choice.Title),
                    AccessibleDescription = InstallerText.T(choice.Description)
                };
                RadioButton radio = new RadioButton
                {
                    Visible = false,
                    TabIndex = ActionCards.Count,
                    TabStop = false
                };
                ActionIconControl icon = new ActionIconControl(choice.Mode, InstallerText.T(choice.Title))
                {
                    Location = new Point(9, 10),
                    Size = new Size(36, 36),
                    TabStop = false
                };
                Label title = new Label
                {
                    Text = InstallerText.T(choice.Title),
                    Tag = choice.Title,
                    Location = new Point(54, 7),
                    Size = new Size(137, 42),
                    Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                    AutoSize = false,
                    Font = new Font("Segoe UI Semibold", 8.8F, FontStyle.Bold),
                    ForeColor = Navy,
                    BackColor = Theme.Surface,
                    TextAlign = ContentAlignment.MiddleLeft,
                    AutoEllipsis = true,
                    UseMnemonic = false,
                    Cursor = Cursors.Hand
                };
                card.Controls.Add(radio);
                card.Controls.Add(icon);
                card.Controls.Add(title);
                Action resizeCardContent = delegate
                {
                    int textWidth = Math.Max(1, card.ClientSize.Width - title.Left - 12);
                    title.Width = textWidth;
                };
                card.Resize += delegate { resizeCardContent(); };
                ActionCards.Add(card);
                ActionCardRadios.Add(radio);
                ActionCardIcons.Add(icon);
                int cardIndex = ActionCards.Count - 1;
                ActionGrid.Controls.Add(card, 0, cardIndex);
                resizeCardContent();

                radio.CheckedChanged += delegate
                {
                    if (radio.Checked && ActionBox.SelectedIndex != itemIndex)
                        ActionBox.SelectedIndex = itemIndex;
                };
                card.Click += delegate { ActionBox.SelectedIndex = itemIndex; };
                icon.Click += delegate { ActionBox.SelectedIndex = itemIndex; };
                title.Click += delegate { ActionBox.SelectedIndex = itemIndex; };
                card.KeyDown += delegate(object sender, KeyEventArgs e)
                {
                    if (e.KeyCode == Keys.Space || e.KeyCode == Keys.Enter)
                    {
                        ActionBox.SelectedIndex = itemIndex;
                        e.Handled = true;
                    }
                };
                card.Paint += delegate(object sender, PaintEventArgs e)
                {
                    bool selected = ActionBox.SelectedIndex == itemIndex;
                    Rectangle bounds = new Rectangle(0, 0,
                        Math.Max(0, card.Width - 1), Math.Max(0, card.Height - 1));
                    if (bounds.Width <= 0 || bounds.Height <= 0) return;
                    e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                    using (GraphicsPath path = SectionGroupBox.CreateRoundedRectangle(bounds, 9))
                    using (SolidBrush fill = new SolidBrush(selected ? SelectedCard : card.BackColor))
                    using (Pen outline = new Pen(selected || card.BackColor == HoverCard ? Theme.BorderHover : Border))
                    {
                        e.Graphics.FillPath(fill, path);
                        e.Graphics.DrawPath(outline, path);
                    }
                    if (selected)
                    {
                        using (SolidBrush mark = new SolidBrush(Accent))
                            e.Graphics.FillRectangle(mark, 1, 11, 3, Math.Max(12, card.Height - 22));
                    }
                    else if (card.Focused)
                    {
                        Rectangle focus = Rectangle.Inflate(bounds, -4, -4);
                        ControlPaint.DrawFocusRectangle(e.Graphics, focus, Navy,
                            selected ? SelectedCard : card.BackColor);
                    }
                };
                card.BackColorChanged += delegate
                {
                    icon.BackColor = card.BackColor;
                    title.BackColor = card.BackColor;
                };
                WireCardHover(card, card);
                WireCardHover(radio, card);
                WireCardHover(icon, card);
                WireCardHover(title, card);
                card.MouseDown += delegate { card.Focus(); };
            }
            ActionGroup.Controls.Add(ActionGrid);
        }

        private void WireCardHover(Control control, Panel card)
        {
            control.MouseEnter += delegate
            {
                if (ActionBox.SelectedIndex != (int)card.Tag)
                {
                    card.BackColor = HoverCard;
                    card.Invalidate();
                }
            };
            control.MouseLeave += delegate
            {
                if (ActionBox.SelectedIndex != (int)card.Tag &&
                    !card.ClientRectangle.Contains(card.PointToClient(Cursor.Position)))
                {
                    card.BackColor = White;
                    card.Invalidate();
                }
            };
        }

        private void UpdateActionCardSelection()
        {
            for (int index = 0; index < ActionCards.Count; index++)
            {
                Panel card = ActionCards[index];
                int itemIndex = (int)card.Tag;
                bool selected = itemIndex == ActionBox.SelectedIndex;
                card.BackColor = selected ? SelectedCard : White;
                ActionCardIcons[index].Selected = selected;
                ActionCardIcons[index].Invalidate();
                ActionCardRadios[index].Checked = selected;
                card.Invalidate();
            }
        }

        private int FindModeIndex(SetupMode mode)
        {
            if (mode == SetupMode.EldenRingPortable)
                mode = SetupMode.Fresh;
            for (int i = 0; i < ActionBox.Items.Count; i++)
                if (((ModeChoice)ActionBox.Items[i]).Mode == mode) return i;
            return 0;
        }

        private SetupMode SelectedMode
        {
            get
            {
                if (ActionBox.SelectedItem == null) return SetupMode.Fresh;
                return ((ModeChoice)ActionBox.SelectedItem).Mode;
            }
        }

        private void ResizeGroups()
        {
            int width = Math.Max(1, Content.ClientSize.Width - Content.Padding.Horizontal - 2);
            foreach (Control control in Content.Controls) control.Width = width;
            ActionDescription.Width = Math.Max(1, width - 110);
            int groupInner = Math.Max(1, width - 37);
            ProgressDetail.Width = Math.Max(1, groupInner - 26);
            ProgressBar.Width = Math.Max(1, groupInner - 26);
            ProgressLog.Width = Math.Max(1, groupInner - 26);
            if (ActionGrid != null && ActionGroup != null)
                ActionGrid.Width = Math.Max(1, ActionGroup.ClientSize.Width - 22);
            foreach (Control control in LocationGroup.Controls)
            {
                if (control is Panel) control.Width = Math.Max(1, LocationGroup.ClientSize.Width - 37);
            }
            foreach (Control control in OptionsGroup.Controls)
            {
                if (control is Label) control.Width = groupInner;
                if (control is Panel) control.Width = groupInner;
            }
        }

        private void FitWindowToContent(bool preserveCenter)
        {
            if (Content == null) return;

            Rectangle previousBounds = Bounds;
            int contentHeight = 0;
            foreach (Control control in Content.Controls)
                contentHeight += control.Height + control.Margin.Vertical;

            int navigationHeight = ActionGrid == null ? 0 : ActionGrid.Height + 72;
            int chromeHeight = 134 + Content.Padding.Vertical;
            int desiredClientHeight = Math.Max(600,
                Math.Max(contentHeight + chromeHeight, navigationHeight + chromeHeight));
            Rectangle workingArea = Screen.FromControl(this).WorkingArea;
            int nonClientHeight = Math.Max(0, Height - ClientSize.Height);
            int maxClientHeight = Math.Max(1, workingArea.Height - nonClientHeight - 24);
            int fittedClientHeight = Math.Min(desiredClientHeight, maxClientHeight);

            // Avoid a scroll strip when the current screen can show the full form.
            Content.AutoScroll = desiredClientHeight > fittedClientHeight;
            ClientSize = new Size(ClientSize.Width, fittedClientHeight);

            if (preserveCenter && Visible)
            {
                int centerX = previousBounds.Left + previousBounds.Width / 2;
                int centerY = previousBounds.Top + previousBounds.Height / 2;
                int left = centerX - Width / 2;
                int top = centerY - Height / 2;
                left = Math.Max(workingArea.Left, Math.Min(left, workingArea.Right - Width));
                top = Math.Max(workingArea.Top, Math.Min(top, workingArea.Bottom - Height));
                Location = new Point(left, top);
            }
        }

        private void UpdateView()
        {
            if (ActionDescription == null) return;
            SetupMode mode = SelectedMode;
            ModeChoice choice = ActionBox.SelectedItem as ModeChoice;
            UpdateActionCardSelection();
            ActionButton.Text = GetActionCaption(mode);
            string title = GetActionCaptionSource(mode);
            if (ModeSummaryGroup != null)
            {
                ModeSummaryGroup.Text = InstallerText.T(title);
                ModeSummaryGroup.Tag = title;
            }
            if (ModeSummaryIcon != null)
            {
                ModeSummaryIcon.Mode = mode;
                ModeSummaryIcon.AccessibleName = InstallerText.T(title);
                ModeSummaryIcon.Invalidate();
            }
            if (choice != null)
            {
                ActionDescription.Text = InstallerText.T(choice.Description);
                ActionDescription.Tag = choice.Description;
            }
            BuildLocation(mode);
            BuildOptions(mode);
            if (!HasCompleted) ActionButton.Enabled = !IsWorking;
            ResizeGroups();
            FitWindowToContent(Visible);
        }

        private static void ClearAndDisposeChildren(Control parent)
        {
            while (parent.Controls.Count > 0)
            {
                int lastIndex = parent.Controls.Count - 1;
                Control child = parent.Controls[lastIndex];
                parent.Controls.RemoveAt(lastIndex);
                child.Dispose();
            }
        }

        private string GetActionCaption(SetupMode mode)
        {
            return InstallerText.T(GetActionCaptionSource(mode));
        }

        private string GetActionCaptionSource(SetupMode mode)
        {
            switch (mode)
            {
                case SetupMode.Update: return "Actualizar MO2 Revamped";
                case SetupMode.EldenRingPortable: return "Instalar MO2 para Elden Ring y Nightreign";
                case SetupMode.Migrate: return "Instalar y copiar datos";
                case SetupMode.Restore: return "Restaurar la versión anterior";
                case SetupMode.Uninstall: return "Desinstalar MO2";
                default: return "Instalar MO2";
            }
        }

        private void BuildLocation(SetupMode mode)
        {
            SaveRenderedPaths();
            LocationGroup.SuspendLayout();
            ClearAndDisposeChildren(LocationGroup);
            FreshTargetBox = null;
            UpdateTargetBox = null;
            SourceInstallBox = null;
            SourceDataBox = null;
            MigrateTargetBox = null;
            RestoreTargetBox = null;
            UninstallTargetBox = null;
            RenderedLocationMode = mode;
            HasRenderedLocation = true;
            int y = 28;
            string defaultManagedTarget = DefaultManagedTarget();
            if (mode == SetupMode.Fresh || mode == SetupMode.EldenRingPortable)
            {
                string defaultTarget = FreshDefaultTarget(mode);
                string initialTarget = String.IsNullOrWhiteSpace(FreshTargetPath) ? defaultTarget : FreshTargetPath;
                FreshTargetBox = AddPathRow("Carpeta de destino", initialTarget, ref y, true);
                FreshTargetPath = FreshTargetBox.Text;
                TextBox freshTargetBox = FreshTargetBox;
                freshTargetBox.TextChanged += delegate { FreshTargetPath = freshTargetBox.Text; };
            }
            else if (mode == SetupMode.Update)
                UpdateTargetBox = AddPathRow("Instalación MO2 Revamped que se actualizará", GetSavedPath(mode, 0, defaultManagedTarget), ref y, true);
            else if (mode == SetupMode.Migrate)
            {
                SourceInstallBox = AddPathRow("Instalación MO2 de origen", GetSavedPath(mode, 0, ""), ref y, true);
                SourceDataBox = AddPathRow("Carpeta de datos del origen", GetSavedPath(mode, 1, ""), ref y, true);
                MigrateTargetBox = AddPathRow("Nueva carpeta de destino", GetSavedPath(mode, 2, @"C:\MO2 Revamped\Portable"), ref y, true);
                SourceInstallBox.TextChanged += delegate { AutoFindDataDirectory(); };
            }
            else if (mode == SetupMode.Restore)
                RestoreTargetBox = AddPathRow("Instalación de MO2 Revamped con copia de seguridad", GetSavedPath(mode, 0, FixedTarget.Length == 0 ? defaultManagedTarget : FixedTarget), ref y, !UninstallerOnly);
            else
            {
                UninstallTargetBox = AddPathRow("Carpeta de instalación de MO2 Revamped", GetSavedPath(mode, 0, FixedTarget.Length == 0 ? defaultManagedTarget : FixedTarget), ref y, !UninstallerOnly);
                UninstallTargetBox.TextChanged += delegate { UpdateUninstallOptionsAvailability(); };
            }
            LocationGroup.Height = Math.Max(92, y + 6);
            LocationGroup.ResumeLayout(true);
        }

        private static string FreshDefaultTarget(SetupMode mode)
        {
            return mode == SetupMode.EldenRingPortable
                ? @"C:\MO2 Revamped\Isolated\Elden Ring"
                : @"C:\MO2 Revamped";
        }

        private static string DefaultManagedTarget()
        {
            string[] knownTargets = new string[]
            {
                @"C:\MO2 Revamped",
                @"C:\MO2 Revamped\Full",
                @"C:\MO2 Revamped\MO2 Revamped",
                @"C:\MO2 Revamped\Portable",
                @"C:\MO2 Revamped\Isolated\Elden Ring"
            };
            foreach (string target in knownTargets)
            {
                if (SetupEngine.IsRevampedInstallation(target))
                    return target;
            }
            return FreshDefaultTarget(SetupMode.Fresh);
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
            int width = Math.Max(1, LocationGroup.ClientSize.Width - 37);
            Panel row = new Panel { Location = new Point(14, y), Size = new Size(width, 57), Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right, BackColor = Theme.Surface, Margin = Padding.Empty };
            Label label = NewLabel(caption, true);
            label.Location = new Point(0, 0);
            label.Size = new Size(width, 18);
            TextBox box = new TextBox
            {
                Text = initial,
                Location = new Point(0, 22),
                Size = new Size(width - (browse ? 105 : 0), 25),
                BorderStyle = BorderStyle.FixedSingle,
                BackColor = Theme.Input,
                ForeColor = TextColor,
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
            ClearAndDisposeChildren(OptionsGroup);
            StartMenuCheck = null;
            DesktopCheck = null;
            LaunchCheck = null;
            ProfilesCheck = null;
            ModsCheck = null;
            DownloadsCheck = null;
            OverwriteCheck = null;
            SettingsCheck = null;
            CategoriesCheck = null;
            ForceCloseCheck = null;
            RemoveAllContentsCheck = null;
            RemoveRevampedAppDataCheck = null;
            int width = Math.Max(1, OptionsGroup.ClientSize.Width - 37);
            int height = 78;
            if (mode == SetupMode.Fresh || mode == SetupMode.EldenRingPortable)
            {
                FlowLayoutPanel shortcuts = new FlowLayoutPanel
                {
                    Location = new Point(12, 26),
                    Size = new Size(width, 27),
                    Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right,
                    FlowDirection = FlowDirection.LeftToRight,
                    WrapContents = false,
                    AutoScroll = false,
                    BackColor = Color.Transparent,
                    Margin = Padding.Empty,
                    Padding = Padding.Empty
                };
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 0, 0);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 0, 0);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 0, 0);
                StartMenuCheck.Margin = new Padding(0, 3, 24, 0);
                DesktopCheck.Margin = new Padding(0, 3, 24, 0);
                LaunchCheck.Margin = new Padding(0, 3, 0, 0);
                shortcuts.Controls.Add(StartMenuCheck);
                shortcuts.Controls.Add(DesktopCheck);
                shortcuts.Controls.Add(LaunchCheck);
                OptionsGroup.Controls.Add(shortcuts);
                height = 68;
            }
            else if (mode == SetupMode.Update)
            {
                Label note = NewLabel("Los archivos reemplazados se guardan en una copia previa. Perfiles, mods, descargas y plugins adicionales se mantienen.", false);
                note.Location = new Point(12, 26);
                note.Size = new Size(width, 30);
                note.AutoSize = false;
                note.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                OptionsGroup.Controls.Add(note);
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 12, 60);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 250, 60);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 500, 60);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                height = 92;
            }
            else if (mode == SetupMode.Migrate)
            {
                ProfilesCheck = NewCheck("Perfiles y partidas", true, 14, 30);
                ModsCheck = NewCheck("Mods instalados", true, 14, 55);
                DownloadsCheck = NewCheck("Descargas", true, 14, 80);
                OverwriteCheck = NewCheck("Overwrite", true, 270, 30);
                SettingsCheck = NewCheck("Preferencias MO2", true, 270, 55);
                CategoriesCheck = NewCheck("Categorías y mapa Nexus", true, 270, 80);
                StartMenuCheck = NewCheck("Acceso en menú Inicio", true, 14, 105);
                DesktopCheck = NewCheck("Acceso en escritorio", false, 270, 105);
                LaunchCheck = NewCheck("Abrir MO2 al terminar", true, 14, 130);
                OptionsGroup.Controls.Add(ProfilesCheck);
                OptionsGroup.Controls.Add(ModsCheck);
                OptionsGroup.Controls.Add(DownloadsCheck);
                OptionsGroup.Controls.Add(OverwriteCheck);
                OptionsGroup.Controls.Add(SettingsCheck);
                OptionsGroup.Controls.Add(CategoriesCheck);
                OptionsGroup.Controls.Add(StartMenuCheck);
                OptionsGroup.Controls.Add(DesktopCheck);
                OptionsGroup.Controls.Add(LaunchCheck);
                height = 158;
            }
            else
            {
                Label note;
                if (mode == SetupMode.Restore)
                    note = NewLabel("Recupera la versión anterior guardada al actualizar. Mantiene perfiles, mods, descargas y cualquier archivo que haya cambiado después.", false);
                else if (mode == SetupMode.Uninstall)
                {
                    note = NewLabel("La casilla de la carpeta borra su contenido; la de AppData elimina los datos globales de MO2 Revamped.", false);
                    note.Location = new Point(12, 26);
                    note.Size = new Size(width, 34);
                    note.AutoSize = false;
                    note.Anchor = AnchorStyles.Left | AnchorStyles.Top | AnchorStyles.Right;
                    OptionsGroup.Controls.Add(note);
                    ForceCloseCheck = NewCheck("Forzar el cierre de MO2 en esta carpeta si sigue abierto", false, 12, 65);
                    RemoveAllContentsCheck = NewCheck("Borrar todos los datos y la carpeta seleccionada", false, 12, 88);
                    RemoveRevampedAppDataCheck = NewCheck("Borrar también los datos globales de MO2 Revamped (AppData\\Local)", false, 12, 111);
                    OptionsGroup.Controls.Add(ForceCloseCheck);
                    OptionsGroup.Controls.Add(RemoveAllContentsCheck);
                    OptionsGroup.Controls.Add(RemoveRevampedAppDataCheck);
                    UpdateUninstallOptionsAvailability();
                    height = 140;
                }
                else
                    note = NewLabel("Una actualización restaura el MO2 anterior. Una instalación nueva retira solo los archivos de MO2 Revamped que sigan intactos. Los datos de usuario y archivos modificados se conservan.", false);
                if (mode != SetupMode.Uninstall)
                {
                    note.Location = new Point(14, 28);
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
            string text = hasPreviousInstall
                    ? "Borrar todos los datos y la carpeta seleccionada, incluida la copia previa"
                    : "Borrar todos los datos y la carpeta seleccionada";
            RemoveAllContentsCheck.Tag = text;
            RemoveAllContentsCheck.Text = InstallerText.T(text);

            if (RemoveRevampedAppDataCheck != null)
            {
                bool available = SetupEngine.UsesRevampedGlobalData(UninstallTargetBox.Text.Trim());
                string appDataText = available
                    ? "Borrar también los datos globales de MO2 Revamped (AppData\\Local)"
                    : "Solo disponible para instalaciones no portables de MO2 Revamped";
                if (!available) RemoveRevampedAppDataCheck.Checked = false;
                RemoveRevampedAppDataCheck.Enabled = available;
                RemoveRevampedAppDataCheck.Tag = appDataText;
                RemoveRevampedAppDataCheck.Text = InstallerText.T(appDataText);
            }
        }

        private CheckBox NewCheck(string text, bool check, int x, int y)
        {
            CheckBox result = new ThemedCheckBox
            {
                Text = InstallerText.T(text),
                Tag = text,
                Checked = check,
                Location = new Point(x, y),
                AutoSize = true,
                Font = new Font("Segoe UI", 9F),
                ForeColor = TextColor,
                BackColor = Theme.Surface,
                Cursor = Cursors.Hand
            };
            return result;
        }

        private void BrowseFolder(TextBox box)
        {
            using (FolderBrowserDialog dialog = new FolderBrowserDialog())
            {
                dialog.Description = InstallerText.T("Selecciona una carpeta");
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
            }
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
                if (!SetupEngine.IsRevampedInstallation(target))
                    return ShowValidationError("La actualización solo se admite en una instalación identificada de MO2 Revamped. No se modificó la carpeta seleccionada.", "Instalación no reconocida");
                string exe = Path.Combine(target, "ModOrganizer.exe");
                if (!File.Exists(exe)) return ShowValidationError("No se encontró ModOrganizer.exe. Para una carpeta nueva, elige «Instalación nueva».", "MO2 no encontrado");
                string versionText = FileVersionInfo.GetVersionInfo(exe).FileVersion;
                Version version;
                if (!Version.TryParse(NormalizeVersion(versionText), out version) || version < new Version(2, 5, 0, 0) || version > new Version(2, 5, 2, 0))
                    return ShowValidationError("La actualización directa admite MO2 Revamped basado en MO2 2.5.0 a 2.5.2. Usa «Copiar datos» para pasar tus datos a una instalación nueva.", "Versión no compatible");
            }
            else if (mode == SetupMode.Restore)
            {
                InstallState state = SetupEngine.LoadState(target);
                if (!Directory.Exists(target) || !SetupEngine.IsRevampedInstallation(target) || state == null || IsFinishedState(state.Status))
                    return ShowValidationError("No hay una instalación de MO2 Revamped con una copia previa pendiente en esta carpeta.", "Instalación no encontrada");
                if (!HasRestoreBackup(state))
                    return ShowValidationError("Esta instalación no tiene una copia previa válida. «Restaurar copia anterior» solo está disponible después de una actualización con copia de seguridad.", "Copia previa no encontrada");
            }
            else if (mode == SetupMode.Uninstall)
            {
                if (!Directory.Exists(target) || !SetupEngine.IsRevampedInstallation(target))
                    return ShowValidationError("Selecciona una carpeta que contenga una instalación identificada de MO2 Revamped. Las instalaciones originales de MO2 no se modifican.", "Instalación no reconocida");
            }
            return true;
        }

        private bool ShowValidationError(string message, string title)
        {
            ShowThemedPrompt(InstallerText.T(title), InstallerText.T(message),
                MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return false;
        }

        private DialogResult ShowThemedPrompt(string title, string body, MessageBoxButtons buttons, MessageBoxIcon icon)
        {
            using (Form prompt = new Form())
            {
                prompt.Text = title;
                prompt.FormBorderStyle = FormBorderStyle.FixedDialog;
                prompt.StartPosition = FormStartPosition.CenterParent;
                prompt.ShowInTaskbar = false;
                prompt.MaximizeBox = false;
                prompt.MinimizeBox = false;
                prompt.AutoScaleMode = AutoScaleMode.Font;
                prompt.Font = new Font("Segoe UI", 9F);
                prompt.BackColor = Page;
                prompt.ForeColor = TextColor;

                Size measured = TextRenderer.MeasureText(body, prompt.Font,
                    new Size(520, 1600), TextFormatFlags.WordBreak | TextFormatFlags.TextBoxControl);
                int messageHeight = Math.Min(390, Math.Max(58, measured.Height + 8));
                prompt.ClientSize = new Size(620, messageHeight + 138);

                TableLayoutPanel layout = new TableLayoutPanel
                {
                    Dock = DockStyle.Fill,
                    Padding = new Padding(14, 8, 14, 10),
                    ColumnCount = 1,
                    RowCount = 3,
                    BackColor = Page
                };
                layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 52));
                layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
                layout.RowStyles.Add(new RowStyle(SizeType.Absolute, 54));
                prompt.Controls.Add(layout);

                Panel header = new Panel { Dock = DockStyle.Fill, BackColor = Page };
                Label heading = new Label
                {
                    Text = title,
                    Font = new Font("Segoe UI Semibold", 12F, FontStyle.Bold),
                    ForeColor = Navy,
                    AutoEllipsis = true,
                    Location = new Point(2, 4),
                    Size = new Size(560, 26)
                };
                header.Controls.Add(heading);
                header.Paint += delegate(object sender, PaintEventArgs e)
                {
                    using (Pen pen = new Pen(Border)) e.Graphics.DrawLine(pen, 0, header.Height - 1, header.Width, header.Height - 1);
                    using (SolidBrush brush = new SolidBrush(Accent)) e.Graphics.FillRectangle(brush, 0, header.Height - 2, 38, 2);
                };
                layout.Controls.Add(header, 0, 0);

                Panel card = new Panel
                {
                    Dock = DockStyle.Fill,
                    BackColor = Theme.Surface,
                    Padding = new Padding(14, 12, 14, 12),
                    AutoScroll = true
                };
                card.Paint += delegate(object sender, PaintEventArgs e)
                {
                    e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                    using (GraphicsPath path = SectionGroupBox.CreateRoundedRectangle(
                        new Rectangle(0, 0, card.Width - 1, card.Height - 1), 8))
                    using (Pen pen = new Pen(Border)) e.Graphics.DrawPath(pen, path);
                };

                Control mark = new Control
                {
                    Location = new Point(13, 14),
                    Size = new Size(34, 34),
                    BackColor = Theme.Surface
                };
                string glyph = icon == MessageBoxIcon.Question ? "?" :
                    icon == MessageBoxIcon.Warning ? "!" : icon == MessageBoxIcon.Error ? "×" : "i";
                Color glyphColor = icon == MessageBoxIcon.Warning ? Theme.Warning :
                    icon == MessageBoxIcon.Error ? Theme.Error : Accent;
                mark.Paint += delegate(object sender, PaintEventArgs e)
                {
                    e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                    using (SolidBrush brush = new SolidBrush(Theme.IconTile))
                        e.Graphics.FillEllipse(brush, 1, 1, mark.Width - 2, mark.Height - 2);
                    using (Font glyphFont = new Font("Segoe UI Semibold", 15F, FontStyle.Bold))
                        TextRenderer.DrawText(e.Graphics, glyph, glyphFont, mark.ClientRectangle,
                            glyphColor, TextFormatFlags.HorizontalCenter |
                            TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                };
                card.Controls.Add(mark);

                Label message = new Label
                {
                    Text = body,
                    Font = prompt.Font,
                    ForeColor = TextColor,
                    AutoSize = true,
                    MaximumSize = new Size(500, 0),
                    Location = new Point(60, 14)
                };
                card.Controls.Add(message);
                layout.Controls.Add(card, 0, 1);

                Panel footer = new Panel { Dock = DockStyle.Fill, BackColor = Page };
                footer.Paint += delegate(object sender, PaintEventArgs e)
                {
                    using (Pen pen = new Pen(Border)) e.Graphics.DrawLine(pen, 0, 0, footer.Width, 0);
                };
                bool confirm = buttons == MessageBoxButtons.YesNo;
                Button close = NewButton(confirm ? "No" : "Cerrar", false);
                close.Size = new Size(102, 32);
                close.DialogResult = confirm ? DialogResult.No : DialogResult.OK;
                close.Anchor = AnchorStyles.Top | AnchorStyles.Right;
                footer.Controls.Add(close);
                if (confirm)
                {
                    Button accept = NewButton("Sí", true);
                    accept.Size = new Size(112, 32);
                    accept.DialogResult = DialogResult.Yes;
                    accept.Anchor = AnchorStyles.Top | AnchorStyles.Right;
                    footer.Controls.Add(accept);
                    footer.Resize += delegate
                    {
                        close.Left = footer.ClientSize.Width - close.Width;
                        accept.Left = close.Left - accept.Width - 8;
                    };
                    prompt.AcceptButton = accept;
                    prompt.CancelButton = close;
                }
                else
                {
                    footer.Resize += delegate { close.Left = footer.ClientSize.Width - close.Width; };
                    prompt.AcceptButton = close;
                    prompt.CancelButton = close;
                }
                layout.Controls.Add(footer, 0, 2);
                return prompt.ShowDialog(this);
            }
        }

        private bool TargetIsEmptyOrMissing(string target)
        {
            if (!Directory.Exists(target)) return true;
            if (File.Exists(Path.Combine(target, "ModOrganizer.exe")))
                return ShowValidationError("Esta carpeta ya contiene MO2. Elige «Actualizar MO2 Revamped» si está marcada como Revamped o crea otra carpeta.", "La carpeta ya contiene MO2");
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
                title = InstallerText.T("Restaurar la versión anterior");
                body = InstallerText.T("Se recuperarán los archivos originales guardados para:") + "\n\n" + target +
                    InstallerText.T("\n\nCopia previa: ") + (state == null ? InstallerText.T("no encontrada") : state.BackupDirectory) +
                    InstallerText.T("\n\nSe quitarán los accesos y la entrada de MO2 Revamped. Los perfiles, mods, descargas y archivos modificados se conservarán. ¿Continuar?");
            }
            else if (mode == SetupMode.Uninstall)
            {
                InstallState state = SetupEngine.LoadState(target);
                bool trackedInstall = state != null && !IsFinishedState(state.Status);
                title = InstallerText.T("Desinstalar MO2 Revamped");
                body = InstallerText.T("Se desinstalará esta instalación de MO2 Revamped:") + "\n\n" + target +
                    (RemoveAllContentsCheck != null && RemoveAllContentsCheck.Checked
                        ? InstallerText.T("\n\nSe borrará todo lo que haya dentro de la carpeta seleccionada, incluidos perfiles, mods, descargas, ajustes, guardados compartidos, copias de seguridad de guardados y archivos añadidos. También se quitará la carpeta seleccionada y las contenedoras que queden vacías, hasta llegar a una carpeta con contenido o protegida por Windows. Los datos fuera de esta ruta no se tocarán. Esta acción no se puede deshacer.")
                        : InstallerText.T("\n\nSe quitarán las carpetas de componentes reconocidas y se conservarán los datos de usuario, como perfiles, mods, descargas, overwrite y ajustes. Los demás archivos fuera de esas carpetas también se mantienen. ") + (trackedInstall ? InstallerText.T("Si hay una copia previa, se restaurará la versión original.") : InstallerText.T("Los datos de usuario se mantendrán en su ubicación actual.")));
                if (RemoveRevampedAppDataCheck != null && RemoveRevampedAppDataCheck.Checked)
                    body += String.Format(
                        InstallerText.T("\n\nSe borrará por completo la carpeta global de MO2 Revamped:\n{0}\nIncluye todas las instancias globales y sus perfiles, mods, descargas y ajustes. Se comparte entre instalaciones no portables de Revamped."),
                        SetupEngine.RevampedAppDataDirectory());
                body +=
                    (ForceCloseCheck != null && ForceCloseCheck.Checked
                        ? InstallerText.T("\n\nSi MO2 sigue abierto, se intentará cerrarlo y se forzará el cierre de esta instalación si no responde; podrías perder cambios sin guardar.")
                        : "") + InstallerText.T("\n\n¿Continuar?");
            }
            else if (mode == SetupMode.Update)
            {
                title = InstallerText.T("Actualizar MO2 Revamped");
                body = InstallerText.T("Se actualizarán los archivos del programa en:") + "\n\n" + target +
                    InstallerText.T("\n\nLos archivos previos se guardarán para poder restaurarlos. ¿Continuar?");
            }
            else if (mode == SetupMode.Migrate)
            {
                title = InstallerText.T("Convertir o copiar datos");
                body = InstallerText.T("Se creará una instalación portable en:") + "\n\n" + target +
                    InstallerText.T("\n\nEl origen se conservará. Categorías seleccionadas: ") + SelectedDataNames() + InstallerText.T(". ¿Continuar?");
            }
            else if (mode == SetupMode.EldenRingPortable)
            {
                title = InstallerText.T("Instalación portable solo para Elden Ring y Nightreign");
                body = InstallerText.T("La edición portable aislada de MO2 se instalará en:") + "\n\n" + target +
                    InstallerText.T("\n\nSolo incluirá soporte para Elden Ring y Nightreign y no leerá ni modificará las instancias globales de otras copias de MO2. ¿Continuar?");
            }
            else
            {
                title = InstallerText.T("Instalación nueva");
                body = InstallerText.T("MO2 Revamped se instalará en:") + "\n\n" + target +
                    InstallerText.T("\n\n¿Continuar?");
            }
            return ShowThemedPrompt(title, body, MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes;
        }

        private string SelectedDataNames()
        {
            List<string> names = new List<string>();
            if (ProfilesCheck.Checked) names.Add(InstallerText.T("perfiles"));
            if (ModsCheck.Checked) names.Add(InstallerText.T("mods"));
            if (DownloadsCheck.Checked) names.Add(InstallerText.T("descargas"));
            if (OverwriteCheck.Checked) names.Add("overwrite");
            if (SettingsCheck.Checked) names.Add(InstallerText.T("preferencias"));
            if (CategoriesCheck.Checked) names.Add(InstallerText.T("categorías"));
            return names.Count == 0 ? InstallerText.T("ninguna") : String.Join(", ", names.ToArray());
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
                InstanceKind = mode == SetupMode.Migrate || mode == SetupMode.EldenRingPortable
                    ? InstanceKind.Portable : InstanceKind.Standard,
                CopyProfiles = mode == SetupMode.Migrate && ProfilesCheck != null && ProfilesCheck.Checked,
                CopyMods = mode == SetupMode.Migrate && ModsCheck != null && ModsCheck.Checked,
                CopyDownloads = mode == SetupMode.Migrate && DownloadsCheck != null && DownloadsCheck.Checked,
                CopyOverwrite = mode == SetupMode.Migrate && OverwriteCheck != null && OverwriteCheck.Checked,
                CopySettings = mode == SetupMode.Migrate && SettingsCheck != null && SettingsCheck.Checked,
                CopyCategories = mode == SetupMode.Migrate && CategoriesCheck != null && CategoriesCheck.Checked,
                StartMenuShortcut = mode != SetupMode.Restore && mode != SetupMode.Uninstall && StartMenuCheck != null && StartMenuCheck.Checked,
                DesktopShortcut = mode != SetupMode.Restore && mode != SetupMode.Uninstall && DesktopCheck != null && DesktopCheck.Checked,
                LaunchWhenDone = LaunchCheck != null && LaunchCheck.Checked && mode != SetupMode.Restore && mode != SetupMode.Uninstall,
                ForceCloseMo2 = mode == SetupMode.Uninstall && ForceCloseCheck != null && ForceCloseCheck.Checked,
                RemoveAllRemainingContents = mode == SetupMode.Uninstall && RemoveAllContentsCheck != null && RemoveAllContentsCheck.Checked,
                RemoveRevampedAppData = mode == SetupMode.Uninstall && RemoveRevampedAppDataCheck != null && RemoveRevampedAppDataCheck.Checked
            };
            IsWorking = true;
            ActionBox.Enabled = false;
            ActionGroup.Enabled = false;
            LocationGroup.Enabled = false;
            OptionsGroup.Enabled = false;
            ActionButton.Enabled = false;
            CloseButton.Enabled = false;
            ProgressDetail.Text = InstallerText.T(mode == SetupMode.Restore ? "Preparando la restauración…" :
                mode == SetupMode.Uninstall ? "Preparando la desinstalación…" : "Preparando la instalación…");
            ProgressDetail.ForeColor = Navy;
            ProgressBar.Value = 0;
            ProgressLog.Clear();
            ProgressLog.Visible = true;
            ProgressBar.Visible = true;
            ProgressGroup.Height = 150;
            LastLoggedStage = "";
            FitWindowToContent(true);
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

        private string TranslateProgress(string message)
        {
            int marker = message.IndexOf(" (", StringComparison.Ordinal);
            if (marker > 0)
            {
                string prefix = message.Substring(0, marker);
                string translatedPrefix = InstallerText.T(prefix);
                if (!String.Equals(prefix, translatedPrefix, StringComparison.Ordinal))
                    return translatedPrefix + message.Substring(marker);
            }
            return InstallerText.T(message);
        }

        private void WorkerProgressChanged(object sender, ProgressChangedEventArgs e)
        {
            ProgressBar.Value = Math.Max(0, Math.Min(100, e.ProgressPercentage));
            string message = Convert.ToString(e.UserState);
            if (String.IsNullOrWhiteSpace(message)) return;
            ProgressDetail.Text = TranslateProgress(message);
            string stage = ProgressStage(message);
            if (!String.Equals(stage, LastLoggedStage, StringComparison.Ordinal))
            {
                LastLoggedStage = stage;
                ProgressLog.AppendText(InstallerText.T(stage) + Environment.NewLine);
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
                ProgressDetail.Text = InstallerText.T("La operación no pudo completarse.");
                ProgressDetail.ForeColor = Theme.Error;
                string logPath = SetupDiagnostics.CurrentPath;
                ProgressLog.Text = e.Error.GetBaseException().Message +
                    (String.IsNullOrEmpty(logPath)
                        ? Environment.NewLine + InstallerText.T("No se pudo guardar el registro detallado.")
                        : Environment.NewLine + Environment.NewLine + InstallerText.T("Registro detallado: ") + logPath);
                ProgressLog.Visible = true;
                return;
            }
            OperationResult result = (OperationResult)e.Result;
            SetupDiagnostics.Info("Operation completed: " + result.Message +
                (String.IsNullOrEmpty(result.Details) ? "" : Environment.NewLine + result.Details));
            ProgressBar.Value = 100;
                ProgressDetail.Text = InstallerText.T(result.Message);
            ProgressDetail.ForeColor = result.Success ? Theme.Success : Theme.Warning;
            string logPathOnSuccess = SetupDiagnostics.CurrentPath;
            ProgressLog.Text = result.Details +
                (String.IsNullOrEmpty(logPathOnSuccess)
                    ? Environment.NewLine + Environment.NewLine + InstallerText.T("No se pudo guardar el registro detallado.")
                    : Environment.NewLine + Environment.NewLine + InstallerText.T("Registro detallado: ") + logPathOnSuccess);
            ProgressLog.Visible = true;
            if (!String.IsNullOrEmpty(result.LaunchPath))
            {
                if (!UnelevatedLauncher.Start(result.LaunchPath))
                    ProgressLog.AppendText(Environment.NewLine + InstallerText.T("No se pudo abrir Mod Organizer 2."));
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
                ShowThemedPrompt(InstallerText.T("Proceso en curso"),
                    InstallerText.T("Espera a que termine el proceso antes de cerrar el asistente."),
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            base.OnFormClosing(e);
        }
    }
}
