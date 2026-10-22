using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Forms;
using Microsoft.Win32;

namespace Mo2RevampedSetup
{
    internal enum SetupMode { Fresh, EldenRingPortable, Update, Migrate, Restore, Uninstall }
    internal enum InstanceKind { Portable, Standard }

    public sealed class PayloadManifest
    {
        public string Version { get; set; }
        public long TotalBytes { get; set; }
        public List<PayloadFile> Files { get; set; }
    }

    public sealed class PayloadFile
    {
        public string Path { get; set; }
        public long Length { get; set; }
        public string Sha256 { get; set; }
    }

    public sealed class InstallState
    {
        public string TargetDirectory { get; set; }
        public string BackupDirectory { get; set; }
        public string Status { get; set; }
        public string Version { get; set; }
        public string InstalledAt { get; set; }
        public List<InstallFileState> Files { get; set; }
        public List<string> ShortcutPaths { get; set; }
        // Update only: shortcuts that did not exist before the update and the
        // version shown in Windows before it, so a restore can undo just that.
        public List<string> CreatedShortcutPaths { get; set; }
        public string PreviousDisplayVersion { get; set; }
    }

    public sealed class InstallFileState
    {
        public string Path { get; set; }
        public bool ExistedBefore { get; set; }
        public string InstalledSha256 { get; set; }
        public string OriginalSha256 { get; set; }
        public bool IsPortableMarker { get; set; }
        public bool IsUserData { get; set; }
        // Installed by an earlier MO2 Revamped setup (kept across updates):
        // uninstalling removes it instead of putting the older version back.
        public bool OwnedByRevamped { get; set; }
        // A file of the previous version that the update removed; Restore
        // puts it back from the update's backup.
        public bool RemovedByUpdate { get; set; }
    }

    internal sealed class SetupRequest
    {
        public SetupMode Mode;
        public InstanceKind InstanceKind;
        public string TargetDirectory;
        public string SourceInstallDirectory;
        public string SourceDataDirectory;
        public bool CopyProfiles;
        public bool CopyMods;
        public bool CopyDownloads;
        public bool CopyOverwrite;
        public bool CopySettings;
        public bool CopyCategories;
        public bool StartMenuShortcut;
        public bool DesktopShortcut;
        public bool LaunchWhenDone;
        public bool ForceCloseMo2;
        public bool RemoveAllRemainingContents;
        public bool RemoveRevampedAppData;
        // a new installation into a folder that kept its data after an uninstall
        public bool ReinstallOverKeptData;
    }

    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            if (InstallerBackend.TryHandleCommandLine(args)) return;
            SetupDiagnostics.Initialize(args);
            try
            {
                if (InstallerBackend.TryLaunchQtInstaller(args)) return;
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
                Application.ThreadException += delegate(object sender, System.Threading.ThreadExceptionEventArgs eventArgs)
                {
                    SetupDiagnostics.Error("Unhandled exception on the installer UI thread.", eventArgs.Exception);
                    MessageBox.Show(
                        String.Format(
                            InstallerText.T("El asistente se cerrará por un error inesperado.\n\n{0}{1}"),
                            eventArgs.Exception.GetBaseException().Message,
                            String.IsNullOrEmpty(SetupDiagnostics.CurrentPath) ? "" : "\n\n" +
                                String.Format(InstallerText.T("Registro detallado: {0}"), SetupDiagnostics.CurrentPath)),
                        InstallerText.T("MO2 Revamped - Error"),
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                    Application.Exit();
                };
                AppDomain.CurrentDomain.UnhandledException += delegate(object sender, UnhandledExceptionEventArgs eventArgs)
                {
                    Exception exception = eventArgs.ExceptionObject as Exception;
                    SetupDiagnostics.Error("Unhandled exception in the installer process.", exception);
                };
                bool uninstallRequest = args != null && args.Length >= 2 &&
                    (String.Equals(args[0], "--uninstall", StringComparison.OrdinalIgnoreCase) ||
                     String.Equals(args[0], "--restore", StringComparison.OrdinalIgnoreCase));
#if UNINSTALLER_STUB
                string target = uninstallRequest ? args[1] : Path.GetDirectoryName(Application.ExecutablePath);
                Application.Run(new CompactSetupForm(SetupMode.Uninstall, target, true));
#elif ELDENRING_ONLY_INSTALLER
                if (uninstallRequest)
                    Application.Run(new CompactSetupForm(SetupMode.Uninstall, args[1], true));
                else
                    Application.Run(new CompactSetupForm(SetupMode.EldenRingPortable, null, false));
#else
                Application.Run(new CompactSetupForm(uninstallRequest ? SetupMode.Uninstall : (SetupMode?)null,
                    uninstallRequest ? args[1] : null, uninstallRequest));
#endif
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("The installer UI terminated unexpectedly.", ex);
                string logPath = SetupDiagnostics.CurrentPath;
                MessageBox.Show(
                    String.Format(
                        InstallerText.T("El asistente encontró un error inesperado.\n\n{0}{1}"),
                        ex.GetBaseException().Message,
                        String.IsNullOrEmpty(logPath) ? "" : "\n\n" +
                            String.Format(InstallerText.T("Registro detallado: {0}"), logPath)),
                    InstallerText.T("MO2 Revamped - Error"),
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
            }
        }
    }

    internal sealed class OperationResult
    {
        public bool Success;
        public string Message;
        public string Details;
        public string LaunchPath;
    }

    internal static class SetupDiagnostics
    {
        private static readonly object Sync = new object();
        private static string CurrentLog;
        private static string LastProgressStage = "";

        public static string CurrentPath
        {
            get { lock (Sync) return CurrentLog ?? ""; }
        }

        public static void Initialize(string[] args)
        {
            try
            {
                string root = Path.Combine(Path.GetTempPath(), "MO2 Revamped", "Installer Logs");
                Directory.CreateDirectory(root);
                string suffix = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Process.GetCurrentProcess().Id;
                lock (Sync) CurrentLog = Path.Combine(root, "setup-" + suffix + ".log");

                Info("Installer session started.");
                Info("Product: MO2 Revamped setup 1.0.2; assembly " + (Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 0)) +
                    "; OS: " + Environment.OSVersion.VersionString +
                    "; .NET: " + Environment.Version +
                    "; 64-bit process: " + Environment.Is64BitProcess + ".");
                Info("Command: " + Environment.CommandLine);
                Info("Review this local log before sharing; operation paths may contain private Windows account or folder names.");
                PruneOldLogs(root);
            }
            catch
            {
                lock (Sync) CurrentLog = null;
            }
        }

        public static void BeginOperation(SetupRequest request)
        {
            lock (Sync) LastProgressStage = "";
            if (request == null)
            {
                Info("Operation started without a request object.");
                return;
            }
            Info("Operation started: mode=" + request.Mode +
                "; instance=" + request.InstanceKind +
                "; target='" + request.TargetDirectory + "'" +
                "; source install='" + request.SourceInstallDirectory + "'" +
                "; source data='" + request.SourceDataDirectory + "'.");
            Info("Options: profiles=" + request.CopyProfiles +
                "; mods=" + request.CopyMods +
                "; downloads=" + request.CopyDownloads +
                "; overwrite=" + request.CopyOverwrite +
                "; settings=" + request.CopySettings +
                "; categories=" + request.CopyCategories +
                "; Start menu shortcut=" + request.StartMenuShortcut +
                "; desktop shortcut=" + request.DesktopShortcut +
                "; launch when done=" + request.LaunchWhenDone +
                "; force close MO2=" + request.ForceCloseMo2 +
                "; remove all contents=" + request.RemoveAllRemainingContents +
                "; remove MO2 Revamped AppData=" + request.RemoveRevampedAppData + ".");
        }

        public static void Progress(int percent, string message)
        {
            if (String.IsNullOrWhiteSpace(message)) return;
            string stage = message;
            int marker = stage.IndexOf(" (", StringComparison.Ordinal);
            if (marker >= 0) stage = stage.Substring(0, marker);
            stage = stage.TrimEnd('.', '…');
            lock (Sync)
            {
                if (String.Equals(stage, LastProgressStage, StringComparison.Ordinal)) return;
                LastProgressStage = stage;
            }
            Info("Progress " + Math.Max(0, Math.Min(100, percent)) + "%: " + stage);
        }

        public static void Info(string message)
        {
            Write("INFO", message);
        }

        public static void Error(string message, Exception error)
        {
            Write("ERROR", message + Environment.NewLine +
                (error == null ? "No exception details were supplied." : error.ToString()));
        }

        private static void Write(string level, string message)
        {
            string path = CurrentPath;
            if (String.IsNullOrEmpty(path)) return;
            try
            {
                string stamp = DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz");
                string normalized = (message ?? "").Replace("\r\n", "\n").Replace("\r", "\n");
                string[] lines = normalized.Split('\n');
                StringBuilder text = new StringBuilder();
                for (int i = 0; i < lines.Length; i++)
                {
                    text.Append(i == 0 ? "[" + stamp + " " + level + "] " : "    ")
                        .AppendLine(lines[i]);
                }
                lock (Sync)
                    File.AppendAllText(path, text.ToString(), new UTF8Encoding(false));
            }
            catch
            {
                // Diagnostics must never block installation or removal.
            }
        }

        private static void PruneOldLogs(string root)
        {
            try
            {
                string current = CurrentPath;
                FileInfo[] logs = new DirectoryInfo(root).GetFiles("setup-*.log")
                    .OrderByDescending(file => file.LastWriteTimeUtc).ToArray();
                foreach (FileInfo file in logs.Skip(10))
                {
                    if (String.Equals(file.FullName, current, StringComparison.OrdinalIgnoreCase)) continue;
                    try { file.Delete(); }
                    catch { }
                }
            }
            catch
            {
                // Log rotation is best effort.
            }
        }
    }

    internal static class SetupEngine
    {
        private const string StateFolder = "MO2 Revamped Installer";
        private const string LegacyStateFolder = "MO2 Fork Installer";
        private const string UninstallerFileName = "unins000.exe";

        internal static string RevampedAppDataDirectory()
        {
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (String.IsNullOrWhiteSpace(localAppData))
                throw new InvalidOperationException("No se pudo determinar la carpeta AppData local.");

            string normalizedLocalAppData = NormalizeCleanupDirectory(localAppData);
            string dataDirectory = NormalizeCleanupDirectory(Path.Combine(normalizedLocalAppData, "MO2 Revamped"));
            if (!PathsEqual(Path.GetDirectoryName(dataDirectory), normalizedLocalAppData))
                throw new InvalidOperationException("La carpeta de datos de MO2 Revamped no está dentro de AppData local.");

            return dataDirectory;
        }

        internal static bool UsesRevampedGlobalData(string target)
        {
            if (String.IsNullOrWhiteSpace(target)) return false;
            try
            {
                string directory = Path.GetFullPath(target);
                if (!IsRevampedInstallation(directory)) return false;
                return !File.Exists(Path.Combine(directory, "eldenring-only.portable")) &&
                    !File.Exists(Path.Combine(directory, "portable.txt")) &&
                    !File.Exists(Path.Combine(directory, "ModOrganizer.ini"));
            }
            catch { return false; }
        }

        private static readonly HashSet<string> Mo2ProgramDirectories = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "dlls", "explorer++", "licenses", "loot", "platforms", "plugins",
            "qml", "resources", "styles", "stylesheets", "translations", "tutorials"
        };
        private static readonly HashSet<string> Mo2ProgramFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "ModOrganizer.exe", "helper.exe", "nxmhandler.exe", "QtWebEngineProcess.exe",
            "uibase.dll", "libcrypto-3-x64.dll", "libssl-3-x64.dll",
            "usvfs_proxy_x64.exe", "usvfs_proxy_x86.exe", "usvfs_x64.dll", "usvfs_x86.dll",
            "dump_running_process.bat", "MO2ForkUninstaller.exe", "MO2-Revamped.ico", "MO2-Revamped.install", "unins000.exe",
            "splash.png"
        };
#if INSTALLER_TEST
        private const RegistryHive UninstallHive = RegistryHive.CurrentUser;
        private const string UninstallKeyRoot = "Software\\MO2 Revamped Installer Test\\Uninstall\\";
#else
        private const RegistryHive UninstallHive = RegistryHive.LocalMachine;
        private const string UninstallKeyRoot = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\";
#endif
        private const string PayloadZipResource = "Mo2RevampedSetup.payload.zip";
        private const string EldenRingPayloadZipResource = "Mo2RevampedSetup.eldenring-payload.zip";
        private const string PayloadManifestResource = "Mo2RevampedSetup.payload-manifest.json";
        private const string EldenRingPayloadManifestResource = "Mo2RevampedSetup.eldenring-payload-manifest.json";
        private static readonly JavaScriptSerializer Json = CreateJsonSerializer();

        private static JavaScriptSerializer CreateJsonSerializer()
        {
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            serializer.MaxJsonLength = Int32.MaxValue;
            serializer.RecursionLimit = 100;
            return serializer;
        }

        public static OperationResult Execute(SetupRequest request, BackgroundWorker worker)
        {
#if ELDENRING_ONLY_INSTALLER
            if (request.Mode != SetupMode.EldenRingPortable && request.Mode != SetupMode.Update &&
                request.Mode != SetupMode.Restore && request.Mode != SetupMode.Uninstall)
                throw new InvalidOperationException(InstallerText.T("Este instalador solo administra la edición portable aislada de Elden Ring y Nightreign."));
            if (request.Mode != SetupMode.EldenRingPortable &&
                !IsEldenRingOnlyInstallation(request.TargetDirectory))
                throw new InvalidOperationException(InstallerText.T("El instalador de Elden Ring y Nightreign solo puede actualizar, restaurar o quitar una instalación marcada como aislada para Elden Ring y Nightreign."));
#endif
            if (request.Mode == SetupMode.Update && !IsRevampedInstallation(request.TargetDirectory))
                throw new InvalidOperationException(InstallerText.T("La actualización solo se admite en una instalación identificada de MO2 Revamped. No se modificó la carpeta seleccionada."));
            if (request.Mode == SetupMode.Restore || request.Mode == SetupMode.Uninstall)
                return ReverseInstall(request, worker);
            if (IsMo2Running(request.TargetDirectory))
                throw new InvalidOperationException(InstallerText.T("Cierra Mod Organizer 2 antes de continuar."));
            if (request.Mode == SetupMode.Migrate && IsMo2Running(request.SourceInstallDirectory))
                throw new InvalidOperationException(InstallerText.T("Cierra también la instalación de MO2 de origen antes de copiar sus datos."));

            string target = Path.GetFullPath(request.TargetDirectory);
            bool eldenRingOnly = request.Mode == SetupMode.EldenRingPortable ||
                (request.Mode == SetupMode.Update &&
                 File.Exists(Path.Combine(target, "eldenring-only.portable")));
            PayloadManifest manifest = ReadManifest(eldenRingOnly);
            ValidateManifest(manifest);
            Directory.CreateDirectory(target);
            if (request.Mode == SetupMode.Fresh || request.Mode == SetupMode.EldenRingPortable || request.Mode == SetupMode.Migrate)
            {
                if (Directory.GetFileSystemEntries(target).Length > 0 &&
                    !(request.ReinstallOverKeptData && request.Mode != SetupMode.Migrate &&
                      !File.Exists(Path.Combine(target, "ModOrganizer.exe")) && HasPortableUserData(target)))
                    throw new InvalidOperationException(InstallerText.T("La carpeta de destino dejó de estar vacía. No se modificó."));
            }

            Report(worker, 1, InstallerText.T("Comprobando los componentes del paquete…"));
            PayloadBundle payload = ValidatePayload(manifest, worker, eldenRingOnly);
            try
            {
            List<CopyEntry> dataCopies = request.Mode == SetupMode.Migrate ? EnumerateMigrationFiles(request) : new List<CopyEntry>();
            long dataBytes = 0;
            foreach (CopyEntry copy in dataCopies)
                dataBytes += copy.Data == null ? new FileInfo(copy.SourcePath).Length : copy.Data.LongLength;
            CheckSpace(target, manifest.TotalBytes + dataBytes, request.Mode == SetupMode.Update ? manifest.TotalBytes : 0);

            string backupRoot = BackupRoot(target);
            string backup = Path.Combine(backupRoot, DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"));
            List<InstallFileState> states = new List<InstallFileState>();
            // what the earlier MO2 Revamped setup installed: those files stay
            // Revamped's own after this update
            InstallState earlierState = request.Mode == SetupMode.Update ? LoadState(target) : null;
            Dictionary<string, InstallFileState> earlierFiles = new Dictionary<string, InstallFileState>(StringComparer.OrdinalIgnoreCase);
            if (earlierState != null && earlierState.Files != null)
                foreach (InstallFileState earlier in earlierState.Files)
                    if (!String.IsNullOrEmpty(earlier.Path) && !earlierFiles.ContainsKey(earlier.Path)) earlierFiles.Add(earlier.Path, earlier);
            HashSet<string> payloadPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (PayloadFile file in manifest.Files)
            {
                string destination = SafeCombine(target, file.Path);
                bool existed = File.Exists(destination);
                if (Directory.Exists(destination))
                    throw new InvalidOperationException(InstallerText.T("La instalación contiene una carpeta donde debería ir un archivo del programa: ") + file.Path);
                InstallFileState earlier;
                // with no earlier record, the program files of the recognized
                // MO2 Revamped folder being updated are Revamped's own: an
                // uninstall removes them instead of bringing them back from
                // this update's backup (as the record-less uninstall does)
                bool owned = earlierState == null
                    ? request.Mode == SetupMode.Update && existed
                    : earlierFiles.TryGetValue(file.Path, out earlier) &&
                      (!earlier.ExistedBefore || earlier.OwnedByRevamped) && !earlier.IsUserData;
                payloadPaths.Add(file.Path);
                states.Add(new InstallFileState
                {
                    Path = file.Path,
                    ExistedBefore = existed,
                    InstalledSha256 = file.Sha256,
                    OriginalSha256 = existed ? HashFile(destination) : null,
                    OwnedByRevamped = owned
                });
            }
            // program files of the previous version that this one no longer
            // ships: removed (an old plugin would keep loading), backed up so
            // Restore can bring them back. Only unchanged ones are removed.
            List<string> staleFiles = new List<string>();
            foreach (InstallFileState earlier in earlierFiles.Values)
            {
                if (payloadPaths.Contains(earlier.Path) || earlier.IsUserData || earlier.IsPortableMarker ||
                    !(!earlier.ExistedBefore || earlier.OwnedByRevamped)) continue;
                string stale;
                try { stale = SafeCombine(target, earlier.Path); }
                catch { continue; }
                if (!File.Exists(stale)) continue;
                string staleHash = HashFile(stale);
                if (!HashEquals(staleHash, earlier.InstalledSha256)) continue;
                staleFiles.Add(earlier.Path);
                states.Add(new InstallFileState
                {
                    Path = earlier.Path,
                    ExistedBefore = true,
                    InstalledSha256 = null,
                    OriginalSha256 = staleHash,
                    OwnedByRevamped = true,
                    RemovedByUpdate = true
                });
            }

            // the kept data stays as it is: a program file already there (with no
            // backup made for a new installation) stops the reinstall instead
            if (request.ReinstallOverKeptData)
                foreach (InstallFileState existing in states)
                    if (existing.ExistedBefore && !existing.IsUserData)
                        throw new InvalidOperationException(InstallerText.T("La carpeta contiene archivos del programa. Usa «Actualizar MO2 Revamped» o elige otra carpeta: ") + existing.Path);

            if (request.Mode == SetupMode.Migrate)
            {
                foreach (CopyEntry copy in dataCopies)
                {
                    string relative = copy.RelativePath;
                    string dest = SafeCombine(target, relative);
                    if (File.Exists(dest))
                        throw new InvalidOperationException(InstallerText.T("El destino contiene un archivo que entra en conflicto con los datos que se van a copiar: ") + relative);
                    states.Add(new InstallFileState
                    {
                        Path = relative,
                        ExistedBefore = false,
                        InstalledSha256 = copy.Data == null ? HashFile(copy.SourcePath) : HashBytes(copy.Data),
                        OriginalSha256 = null,
                        IsUserData = true
                    });
                }
                if (request.InstanceKind == InstanceKind.Portable)
                {
                    states.Add(new InstallFileState
                    {
                        Path = "portable.txt",
                        ExistedBefore = false,
                        InstalledSha256 = HashBytes(new byte[0]),
                        OriginalSha256 = null,
                        IsPortableMarker = true
                    });
                }
            }
            if ((request.Mode == SetupMode.Fresh || request.Mode == SetupMode.EldenRingPortable) && request.InstanceKind == InstanceKind.Portable)
            {
                states.Add(new InstallFileState { Path = "portable.txt", ExistedBefore = false, InstalledSha256 = HashBytes(new byte[0]), OriginalSha256 = null, IsPortableMarker = true });
            }
            if (request.Mode == SetupMode.EldenRingPortable)
            {
                states.Add(new InstallFileState
                {
                    Path = "eldenring-only.portable",
                    ExistedBefore = false,
                    InstalledSha256 = HashBytes(new byte[0]),
                    OriginalSha256 = null,
                    IsPortableMarker = true
                });
            }

            List<string> requestedShortcutPaths = GetShortcutPaths(request);
            List<string> shortcutPaths = new List<string>();
            InstallState previousState = request.Mode == SetupMode.Update ? LoadState(target) : null;
            if (previousState != null && previousState.ShortcutPaths != null)
                foreach (string previousPath in previousState.ShortcutPaths)
                    if (!shortcutPaths.Exists(path => PathsEqual(path, previousPath))) shortcutPaths.Add(previousPath);
            foreach (string requestedPath in requestedShortcutPaths)
                if (!shortcutPaths.Exists(path => PathsEqual(path, requestedPath))) shortcutPaths.Add(requestedPath);
            List<string> shortcutsCreatedByThisOperation = new List<string>();
            string targetExecutable = Path.Combine(target, "ModOrganizer.exe");
            foreach (string requestedPath in requestedShortcutPaths)
                if (!File.Exists(requestedPath) || !ShortcutTargets(requestedPath, targetExecutable))
                    shortcutsCreatedByThisOperation.Add(requestedPath);
            InstallState state = new InstallState
            {
                TargetDirectory = target,
                BackupDirectory = request.Mode == SetupMode.Update ? backup : "",
                Status = "Preparing",
                Version = manifest.Version,
                InstalledAt = DateTime.Now.ToString("o"),
                Files = states,
                ShortcutPaths = shortcutPaths,
                CreatedShortcutPaths = request.Mode == SetupMode.Update ? shortcutsCreatedByThisOperation : null,
                PreviousDisplayVersion = request.Mode == SetupMode.Update ? RegisteredDisplayVersion(target) : null
            };
            string uninstallWarning = null;

            if (request.Mode == SetupMode.Update)
            {
                EnsureInstallationFree(target, false, worker);
                Directory.CreateDirectory(backup);
                int n = 0;
                foreach (InstallFileState fileState in states)
                {
                    if (!fileState.ExistedBefore) continue;
                    string src = SafeCombine(target, fileState.Path);
                    string dst = SafeCombine(backup, fileState.Path);
                    Directory.CreateDirectory(Path.GetDirectoryName(dst));
                    File.Copy(src, dst, true);
                    n++;
                    if ((n % 50) == 0) Report(worker, 9, String.Format(InstallerText.T("Guardando copia previa ({0})…"), n));
                }
            }

            SaveState(state);
            try
            {
                state.Status = "Installing";
                SaveState(state);
                int index = 0;
                foreach (PayloadFile file in manifest.Files)
                {
                    WriteZipEntry(payload.Entries[file.Path], SafeCombine(target, file.Path));
                    index++;
                    Report(worker, 10 + (int)(65.0 * index / manifest.Files.Count), String.Format(InstallerText.T("Instalando componentes ({0} / {1})…"), index, manifest.Files.Count));
                }
                // the previous version's files this one dropped (backed up above)
                foreach (string stalePath in staleFiles)
                {
                    string stale = SafeCombine(target, stalePath);
                    if (!File.Exists(stale)) continue;
                    File.Delete(stale);
                    RemoveEmptyParents(Path.GetDirectoryName(stale), target);
                }

                int dataIndex = 0;
                foreach (CopyEntry copy in dataCopies)
                {
                    string destination = SafeCombine(target, copy.RelativePath);
                    Directory.CreateDirectory(Path.GetDirectoryName(destination));
                    if (copy.Data == null) CopyFileAtomically(copy.SourcePath, destination);
                    else WriteBytesAtomically(copy.Data, destination);
                    dataIndex++;
                    Report(worker, 76 + (int)(17.0 * dataIndex / Math.Max(1, dataCopies.Count)), String.Format(InstallerText.T("Copiando datos seleccionados ({0} / {1})…"), dataIndex, dataCopies.Count));
                }

                if ((request.Mode == SetupMode.Fresh || request.Mode == SetupMode.EldenRingPortable) && request.InstanceKind == InstanceKind.Portable ||
                    request.Mode == SetupMode.Migrate && request.InstanceKind == InstanceKind.Portable)
                {
                    string marker = Path.Combine(target, "portable.txt");
                    File.WriteAllBytes(marker, new byte[0]);
                }
                if (request.Mode == SetupMode.EldenRingPortable)
                {
                    File.WriteAllBytes(Path.Combine(target, "eldenring-only.portable"), new byte[0]);
                    // A fresh isolated copy starts in the setup language; an existing choice is kept.
                    string settings = Path.Combine(target, "EldenRingOnly.ini");
                    if (!File.Exists(settings))
                        File.WriteAllText(settings, "[General]\r\nLanguage=" + InstallerText.Language.Replace('-', '_') + "\r\n", new UTF8Encoding(false));
                }

                CreateShortcuts(request, state);
                state.Status = "Completed";
                SaveState(state);
                // Restore only uses this update's copy: earlier ones just took space
                if (request.Mode == SetupMode.Update) PruneOlderBackups(target, state.BackupDirectory);
                Report(worker, 100, InstallerText.T("Instalación preparada correctamente."));
            }
            catch (Exception installError)
            {
                state.Status = "RollingBack";
                SaveState(state);
                bool deferredCleanup;
                InstallState rollbackState = new InstallState
                {
                    TargetDirectory = state.TargetDirectory,
                    BackupDirectory = state.BackupDirectory,
                    Status = state.Status,
                    Version = state.Version,
                    InstalledAt = state.InstalledAt,
                    Files = state.Files,
                    ShortcutPaths = shortcutsCreatedByThisOperation
                };
                List<string> rollbackNotes = RestoreFiles(rollbackState, false, false, false, false, worker, out deferredCleanup);
                state.Status = "Failed";
                SaveState(state);
                // a cleanly reverted update leaves the folder as the earlier
                // version: its record goes back (or none, as before), so a later
                // uninstall or restore works from what is really installed
                if (request.Mode == SetupMode.Update && rollbackNotes.Count == 0 && !deferredCleanup)
                {
                    try
                    {
                        if (earlierState != null) SaveState(earlierState);
                        else DeleteState(target);
                        if (Directory.Exists(backup)) Directory.Delete(backup, true);
                    }
                    catch (Exception recordError)
                    {
                        SetupDiagnostics.Error("Could not put back the earlier install record of '" + target + "' after a reverted update.", recordError);
                    }
                }
                if (rollbackNotes.Count > 0)
                    throw new InvalidOperationException(installError.Message + Environment.NewLine + InstallerText.T("Revisión de recuperación: ") + String.Join("; ", rollbackNotes.ToArray()), installError);
                throw new InvalidOperationException(installError.Message + Environment.NewLine + InstallerText.T("Los archivos tocados se revirtieron a su estado previo."), installError);
            }

            try { RegisterUninstallEntry(target, manifest.Version, manifest.TotalBytes); }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Windows could not register the uninstaller entry for '" + target + "'.", ex);
                uninstallWarning = InstallerText.T("Windows no pudo registrar la desinstalación: ") + ex.Message;
            }

            StringBuilder details = new StringBuilder();
            details.Append(InstallerText.T("Destino: ")).Append(target).Append(Environment.NewLine);
            if (request.Mode == SetupMode.Update)
                details.Append(InstallerText.T("Copia de seguridad: ")).Append(backup).Append(Environment.NewLine);
            if (request.Mode == SetupMode.Migrate)
                details.Append(InstallerText.T("Origen conservado: ")).Append(request.SourceDataDirectory).Append(Environment.NewLine);
            if (request.Mode == SetupMode.Update && eldenRingOnly)
                details.Append(InstallerText.T("Se conservó la edición portable aislada de Elden Ring y Nightreign y sus datos locales."));
            else if (request.Mode == SetupMode.Update)
                details.Append(InstallerText.T("Se mantuvo la configuración de instancia y datos que ya tenía MO2."));
            else if (request.Mode == SetupMode.EldenRingPortable)
                details.Append(InstallerText.T("MO2 se configuró como portable y aislado para Elden Ring y Nightreign; sus datos y preferencias se guardan junto a esta copia, sin consultar las instancias globales de otras instalaciones."));
            else if (request.Mode == SetupMode.Migrate || request.InstanceKind == InstanceKind.Portable)
                details.Append(InstallerText.T("MO2 se configuró como portable."));
            else
                details.Append(InstallerText.T("MO2 se configuró como estándar."));
            if (!String.IsNullOrEmpty(uninstallWarning))
                details.Append(Environment.NewLine).Append(uninstallWarning).Append(InstallerText.T(" El asistente de instalación todavía permite restaurar esta operación."));
            return new OperationResult
            {
                // MO2 is installed and works; only the Windows entry is missing, so
                // the window must not offer to retry (the folder is no longer empty)
                Success = true,
                Message = !String.IsNullOrEmpty(uninstallWarning) ? "La instalación terminó, pero sin entrada de desinstalación." :
                    request.Mode == SetupMode.Update ? "Se actualizaron los archivos del programa." :
                    request.Mode == SetupMode.Migrate ? "La instalación nueva recibió los datos seleccionados." : "La instalación nueva está lista.",
                Details = details.ToString(),
                LaunchPath = request.LaunchWhenDone ? Path.Combine(target, "ModOrganizer.exe") : null
            };
            }
            finally { payload.Dispose(); }
        }

        public static InstallState LoadState(string target)
        {
            if (String.IsNullOrWhiteSpace(target)) return null;
            try
            {
                string[] statePaths = new string[] { StatePathFor(target), LegacyStatePathFor(target) };
                foreach (string statePath in statePaths)
                {
                    if (!File.Exists(statePath)) continue;
                    InstallState state = Json.Deserialize<InstallState>(File.ReadAllText(statePath, Encoding.UTF8));
                    if (state != null && state.Files != null && PathsEqual(state.TargetDirectory, target)) return state;
                }
                return null;
            }
            catch { return null; }
        }

        public static bool IsRevampedInstallation(string target)
        {
            if (String.IsNullOrWhiteSpace(target)) return false;
            try
            {
                string root = Path.GetFullPath(target);
                if (!Directory.Exists(root) || !File.Exists(Path.Combine(root, "ModOrganizer.exe"))) return false;
                if (File.Exists(Path.Combine(root, "eldenring-only.portable"))) return true;

                string markerPath = Path.Combine(root, "MO2-Revamped.install");
                if (File.Exists(markerPath) && String.Equals(
                        File.ReadAllText(markerPath, Encoding.UTF8).Trim(),
                        "MO2-REVAMPED-INSTALLATION-MARKER",
                        StringComparison.Ordinal))
                    return true;

                // v1.0.0 did not ship a dedicated marker. Keep a narrow legacy
                // upgrade path for its branded INI and icon pair.
                string legacyIni = Path.Combine(root, "MO2-Revamped.ini");
                string legacyIcon = Path.Combine(root, "MO2-Revamped.ico");
                if (!File.Exists(legacyIni) || !File.Exists(legacyIcon)) return false;
                string[] lines = File.ReadAllText(legacyIni, Encoding.UTF8)
                    .Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
                bool inSettings = false;
                bool hasSettings = false;
                bool hasLanguage = false;
                foreach (string rawLine in lines)
                {
                    string line = rawLine.Trim();
                    if (line.Length == 0 || line[0] == ';' || line[0] == '#') continue;
                    if (line[0] == '[' && line[line.Length - 1] == ']')
                    {
                        inSettings = String.Equals(line.Substring(1, line.Length - 2).Trim(), "Settings", StringComparison.OrdinalIgnoreCase);
                        hasSettings |= inSettings;
                        continue;
                    }
                    if (!inSettings) continue;
                    int separator = line.IndexOf('=');
                    if (separator <= 0) continue;
                    if (String.Equals(line.Substring(0, separator).Trim(), "language", StringComparison.OrdinalIgnoreCase) &&
                        !String.IsNullOrWhiteSpace(line.Substring(separator + 1)))
                        hasLanguage = true;
                }
                return hasSettings && hasLanguage;
            }
            catch { return false; }
        }

        internal static bool IsEldenRingOnlyInstallation(string target)
        {
            if (String.IsNullOrWhiteSpace(target)) return false;
            try
            {
                string root = Path.GetFullPath(target);
                return Directory.Exists(root) &&
                    File.Exists(Path.Combine(root, "ModOrganizer.exe")) &&
                    File.Exists(Path.Combine(root, "eldenring-only.portable"));
            }
            catch { return false; }
        }

        private static OperationResult ReverseInstall(SetupRequest request, BackgroundWorker worker)
        {
            string target = request.TargetDirectory;
            SetupMode mode = request.Mode;
            bool restoreMode = mode == SetupMode.Restore;
            if (!IsRevampedInstallation(target))
                throw new InvalidOperationException(InstallerText.T("La restauración o desinstalación solo se admite en una instalación identificada de MO2 Revamped. Las instalaciones originales de MO2 no se modifican."));
#if ELDENRING_ONLY_INSTALLER
            if (!IsEldenRingOnlyInstallation(target))
                throw new InvalidOperationException(InstallerText.T("El instalador aislado solo administra una instalación portable marcada para Elden Ring y Nightreign."));
#endif
            InstallState state = LoadState(target);
            if (state == null || String.Equals(state.Status, "Restored", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(state.Status, "Uninstalled", StringComparison.OrdinalIgnoreCase))
            {
                if (restoreMode)
                    throw new InvalidOperationException(InstallerText.T("No se encontró una instalación de MO2 Revamped con copia previa para restaurar."));
                return UninstallUnregisteredMo2(request, worker);
            }
            if (!restoreMode)
                ValidateSafeUninstallTarget(target);
            if (!String.IsNullOrWhiteSpace(state.BackupDirectory) && !IsBackupPathForTarget(state.BackupDirectory, target))
                throw new InvalidOperationException(InstallerText.T("La ruta de copia de seguridad no coincide con esta instalación."));
            if (restoreMode)
            {
                if (String.IsNullOrWhiteSpace(state.BackupDirectory) || !Directory.Exists(state.BackupDirectory))
                    throw new InvalidOperationException(InstallerText.T("No se encontró la copia de seguridad de la versión anterior."));
                bool hasBackup = false;
                foreach (InstallFileState file in state.Files)
                {
                    if (!file.ExistedBefore || file.IsUserData) continue;
                    try
                    {
                        string backupFile = SafeCombine(state.BackupDirectory, file.Path);
                        if (File.Exists(backupFile)) { hasBackup = true; break; }
                    }
                    catch { }
                }
                if (!hasBackup) throw new InvalidOperationException(InstallerText.T("La copia de seguridad no contiene archivos originales recuperables."));
            }
            bool removeAllContents = !restoreMode && request.RemoveAllRemainingContents;
            bool forceCloseMo2 = !restoreMode && request.ForceCloseMo2;
            EnsureInstallationFree(target, forceCloseMo2, worker);
            if (!restoreMode) EnsureRevampedGlobalDataNotInUse(request);

            bool deferredCleanup;
            InstallState filesState = state;
            if (restoreMode && state.CreatedShortcutPaths != null)
            {
                // A restore returns to the previous version, which keeps its own
                // shortcuts; only those added by the update are removed.
                filesState = new InstallState
                {
                    TargetDirectory = state.TargetDirectory,
                    BackupDirectory = state.BackupDirectory,
                    Status = state.Status,
                    Version = state.Version,
                    InstalledAt = state.InstalledAt,
                    Files = state.Files,
                    ShortcutPaths = state.CreatedShortcutPaths
                };
            }
            List<string> notes = RestoreFiles(filesState, true, !removeAllContents,
                removeAllContents, !restoreMode, worker, out deferredCleanup);
            if (!restoreMode && !removeAllContents)
                notes.AddRange(RemoveLegacyUninstallerFiles(target));
            if (!restoreMode && removeAllContents)
            {
                List<string> cleanupNotes = RemoveAllRemainingContents(target);
                notes.AddRange(cleanupNotes);
                if (notes.Count == 0) RemoveEmptyInstallDirectoryAndParent(target);
            }
            else if (!restoreMode)
            {
                RemoveEmptyDirectories(target);
            }
            bool keptUninstallEntry = false;
            if (notes.Count == 0)
            {
                // The restored version still lives in this folder. Keep it listed
                // in Windows while its uninstaller exists; otherwise the entry
                // would point to a missing file.
                if (restoreMode && File.Exists(Path.Combine(Path.GetFullPath(target), UninstallerFileName)))
                {
                    keptUninstallEntry = true;
                    try { RestoreRegisteredDisplayVersion(target, state.PreviousDisplayVersion); }
                    catch (Exception ex) { SetupDiagnostics.Error("Could not update the uninstall entry after restoring '" + target + "'.", ex); }
                }
                else
                {
                    string registryNote = RemoveUninstallEntry(target);
                    if (!String.IsNullOrEmpty(registryNote)) notes.Add(registryNote);
                }
                if (removeAllContents)
                    notes.AddRange(RemoveMo2UninstallEntriesForTarget(target));
            }

            if (notes.Count == 0)
            {
                try
                {
                    CleanupBackup(state.BackupDirectory, target);
                    DeleteState(target);
                    CleanupInstallerDataRoot();
                }
                catch (Exception cleanupError)
                {
                    SetupDiagnostics.Error("Cleanup of installer recovery data failed for '" + target + "'.", cleanupError);
                    notes.Add(InstallerText.T("No se pudieron limpiar todos los archivos de recuperación: ") + cleanupError.Message);
                }
            }

            if (!restoreMode && notes.Count == 0 && request.RemoveRevampedAppData)
            {
                Report(worker, 95, InstallerText.T("Eliminando los datos globales de MO2 Revamped…"));
                notes.AddRange(RemoveRevampedGlobalData());
            }

            if (notes.Count > 0)
            {
                state.Status = restoreMode ? "RestoreNeedsAttention" : "UninstallNeedsAttention";
                SaveState(state);
            }

            string message;
            string details;
            string pendingDetails = InstallerText.T("Se conservaron archivos modificados o que no pudieron procesarse: ") + String.Join(", ", notes.ToArray()) +
                Environment.NewLine + InstallerText.T("La copia de seguridad y el estado se mantienen para poder revisar la operación.");
            if (restoreMode)
            {
                message = notes.Count == 0 ? "MO2 anterior restaurado." : "La restauración terminó con elementos pendientes.";
                details = notes.Count == 0
                    ? InstallerText.T(keptUninstallEntry
                        ? "Se recuperaron los archivos de la versión anterior. Sus accesos directos y su entrada de desinstalación se mantienen, y los datos personales se conservaron."
                        : "Se recuperaron los archivos de la versión anterior y se retiró la entrada de desinstalación de MO2 Revamped. Los datos personales se conservaron.")
                    : pendingDetails;
            }
            else
            {
                message = notes.Count == 0 ? "Mod Organizer 2 se desinstaló." : "La desinstalación terminó con archivos conservados o pendientes.";
                details = notes.Count == 0
                    ? removeAllContents
                        ? InstallerText.T("Se eliminaron MO2, sus perfiles, mods, descargas, ajustes y el resto del contenido de la carpeta.") +
                          (!String.IsNullOrWhiteSpace(state.BackupDirectory) ? InstallerText.T(" También se eliminó la copia de la versión anterior.") : "") +
                          InstallerText.T(" Las carpetas contenedoras vacías se borrarán hasta la primera con contenido o protegida.")
                        : InstallerText.T("Se retiraron los archivos de MO2 Revamped, se restauró la instalación previa cuando correspondía y se conservaron los datos personales.")
                    : pendingDetails;
            }
            if (!restoreMode && notes.Count == 0 && request.RemoveRevampedAppData)
                details += Environment.NewLine + String.Format(
                    InstallerText.T("Se completó la limpieza de datos globales de MO2 Revamped en:\n{0}"),
                    RevampedAppDataDirectory());
            if (deferredCleanup) details += Environment.NewLine + InstallerText.T("El desinstalador abierto se retirará al cerrar esta ventana; si Windows mantiene un bloqueo, se completará al reiniciar.");
            return new OperationResult { Success = notes.Count == 0, Message = message, Details = details, LaunchPath = null };
        }

        private static OperationResult UninstallUnregisteredMo2(SetupRequest request, BackgroundWorker worker)
        {
            string target = Path.GetFullPath(request.TargetDirectory);
            string executable = Path.Combine(target, "ModOrganizer.exe");
            if (!Directory.Exists(target) || !File.Exists(executable))
                throw new InvalidOperationException(InstallerText.T("Selecciona la carpeta de una instalación que contenga ModOrganizer.exe."));
            ValidateSafeUninstallTarget(target);
            EnsureInstallationFree(target, request.ForceCloseMo2, worker);
            EnsureRevampedGlobalDataNotInUse(request);

            List<string> notes;
            if (request.RemoveAllRemainingContents)
            {
                Report(worker, 10, InstallerText.T("Eliminando el contenido completo de la instalación…"));
                notes = RemoveAllRemainingContents(target);
                if (notes.Count == 0) RemoveEmptyInstallDirectoryAndParent(target);
            }
            else
            {
                Report(worker, 10, InstallerText.T("Quitando los archivos del programa y conservando los datos…"));
                notes = RemoveUnregisteredProgramFiles(target, worker);
                if (notes.Count == 0) RemoveEmptyDirectories(target);
            }

            if (notes.Count == 0)
            {
                List<string> shortcutNotes = RemoveShortcutsToMo2(target);
                notes.AddRange(shortcutNotes);
            }
            if (notes.Count == 0)
            {
                List<string> registryNotes = RemoveMo2UninstallEntriesForTarget(target);
                notes.AddRange(registryNotes);
            }

            if (notes.Count == 0 && request.RemoveRevampedAppData)
            {
                Report(worker, 95, InstallerText.T("Eliminando los datos globales de MO2 Revamped…"));
                notes.AddRange(RemoveRevampedGlobalData());
            }

            string runningExecutable = Path.GetFullPath(Application.ExecutablePath);
            bool deferredCleanup = false;
            if (notes.Count == 0 && IsPathWithinDirectory(runningExecutable, target) && File.Exists(runningExecutable))
            {
                if (ScheduleRunningUninstaller(runningExecutable, null, target)) deferredCleanup = true;
                else notes.Add(Path.GetFileName(runningExecutable) + InstallerText.T(" (Windows no pudo programar su retirada)"));
            }

            string message = notes.Count == 0
                ? "Mod Organizer 2 se desinstaló."
                : "La desinstalación terminó con elementos pendientes.";
            string details = notes.Count == 0
                ? request.RemoveAllRemainingContents
                    ? InstallerText.T("Se eliminó todo el contenido de la carpeta elegida. Las carpetas contenedoras se borran solo si quedan vacías y no están protegidas.")
                    : InstallerText.T("Se quitaron los componentes conocidos del programa. Los datos de usuario y los archivos que no se reconocieron se conservaron.")
                : InstallerText.T("No se completaron estos elementos: ") + String.Join(", ", notes.ToArray()) +
                  Environment.NewLine + InstallerText.T("Vuelve a ejecutar la desinstalación sobre la misma carpeta para terminar la limpieza.");
            if (notes.Count == 0 && request.RemoveRevampedAppData)
                details += Environment.NewLine + String.Format(
                    InstallerText.T("Se completó la limpieza de datos globales de MO2 Revamped en:\n{0}"),
                    RevampedAppDataDirectory());
            if (deferredCleanup)
                details += Environment.NewLine + InstallerText.T("El desinstalador se retirará al cerrar esta ventana y eliminará las carpetas que queden vacías.");
            return new OperationResult { Success = notes.Count == 0, Message = message, Details = details, LaunchPath = null };
        }

        private static List<string> RemoveUnregisteredProgramFiles(string target, BackgroundWorker worker)
        {
            List<string> notes = new List<string>();
            string runningExecutable = Path.GetFullPath(Application.ExecutablePath);
            string[] entries;
            try { entries = Directory.GetFileSystemEntries(target); }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not enumerate install directory '" + target + "' during uninstall.", ex);
                return new List<string> { InstallerText.T("no se pudo leer la carpeta de instalación (") + ex.Message + ")" };
            }

            int index = 0;
            foreach (string entry in entries)
            {
                index++;
                try
                {
                    string full = Path.GetFullPath(entry);
                    string name = Path.GetFileName(full);
                    FileAttributes attributes = File.GetAttributes(full);
                    bool isDirectory = (attributes & FileAttributes.Directory) != 0;
                    if (isDirectory)
                    {
                        if (!Mo2ProgramDirectories.Contains(name)) continue;
                        if ((attributes & FileAttributes.ReparsePoint) != 0)
                        {
                            Directory.Delete(full, false);
                        }
                        else if (UserExtensibleDirectories.Contains(name))
                        {
                            // users add their own plugins and themes here: without an
                            // install record only the files this setup ships are removed,
                            // and with no package list at hand the folder is kept
                            HashSet<string> known = KnownPayloadPaths();
                            if (known == null) continue;
                            foreach (string file in Directory.GetFiles(full, "*", SearchOption.AllDirectories))
                            {
                                string relative = file.Substring(target.Length).TrimStart('\\', '/');
                                if (!known.Contains(relative.Replace('\\', '/')) || PathsEqual(file, runningExecutable)) continue;
                                // not through a junction inside the folder
                                try { SafeCombine(target, relative); }
                                catch { continue; }
                                FileAttributes fileAttributes = File.GetAttributes(file);
                                if ((fileAttributes & FileAttributes.ReadOnly) != 0)
                                    File.SetAttributes(file, fileAttributes & ~FileAttributes.ReadOnly);
                                File.Delete(file);
                                RemoveEmptyParents(Path.GetDirectoryName(file), target);
                            }
                        }
                        else
                        {
                            DeleteDirectoryContents(full, runningExecutable, notes);
                            if (Directory.Exists(full) && Directory.GetFileSystemEntries(full).Length == 0)
                                Directory.Delete(full, false);
                            else if (Directory.Exists(full))
                                notes.Add(name + InstallerText.T(" (quedaron archivos que no se pudieron retirar)"));
                        }
                    }
                    else
                    {
                        if (PathsEqual(full, runningExecutable) || !IsKnownMo2ProgramFile(name)) continue;
                        if ((attributes & FileAttributes.ReadOnly) != 0)
                            File.SetAttributes(full, attributes & ~FileAttributes.ReadOnly);
                        File.Delete(full);
                    }
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove program entry '" + entry + "'.", ex);
                    notes.Add(Path.GetFileName(entry) + " (" + ex.Message + ")");
                }
                if (worker != null && worker.WorkerReportsProgress)
                Report(worker, Math.Min(80, 10 + index * 70 / Math.Max(1, entries.Length)), InstallerText.T("Quitando archivos del programa…"));
            }
            return notes;
        }

        // users add their own plugins, themes and translations, and Explorer++
        // keeps its settings next to it
        private static readonly HashSet<string> UserExtensibleDirectories = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "plugins", "stylesheets", "translations", "explorer++"
        };

        // every file path this setup's packages contain (forward slashes), or
        // null when the running copy carries no package list (the uninstallers)
        private static HashSet<string> KnownPayloadPaths()
        {
            HashSet<string> paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (bool eldenRingOnly in new bool[] { false, true })
            {
                try
                {
                    PayloadManifest manifest = ReadManifest(eldenRingOnly);
                    if (manifest == null || manifest.Files == null) continue;
                    foreach (PayloadFile file in manifest.Files)
                        if (!String.IsNullOrEmpty(file.Path)) paths.Add(file.Path.Replace('\\', '/'));
                }
                catch { }
            }
            return paths.Count == 0 ? null : paths;
        }

        private static bool IsKnownMo2ProgramFile(string name)
        {
            if (Mo2ProgramFiles.Contains(name)) return true;
            string extension = Path.GetExtension(name);
            if (name.StartsWith("unins", StringComparison.OrdinalIgnoreCase) &&
                (String.Equals(extension, ".exe", StringComparison.OrdinalIgnoreCase) ||
                 String.Equals(extension, ".dat", StringComparison.OrdinalIgnoreCase) ||
                 String.Equals(extension, ".msg", StringComparison.OrdinalIgnoreCase))) return true;
            if (name.StartsWith("Qt", StringComparison.OrdinalIgnoreCase) &&
                name.EndsWith(".dll", StringComparison.OrdinalIgnoreCase)) return true;
            return false;
        }

        private static List<string> RemoveLegacyUninstallerFiles(string target)
        {
            List<string> notes = new List<string>();
            string runningExecutable = Path.GetFullPath(Application.ExecutablePath);
            string[] legacyNames = new string[] { "MO2ForkUninstaller.exe" };
            foreach (string name in legacyNames)
            {
                string path = Path.Combine(target, name);
                if (!File.Exists(path) || PathsEqual(path, runningExecutable)) continue;
                try
                {
                    FileAttributes attributes = File.GetAttributes(path);
                    if ((attributes & FileAttributes.ReadOnly) != 0)
                        File.SetAttributes(path, attributes & ~FileAttributes.ReadOnly);
                    File.Delete(path);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove legacy uninstaller '" + path + "'.", ex);
                    notes.Add(name + " (" + ex.Message + ")");
                }
            }
            return notes;
        }

        private static List<string> RemoveShortcutsToMo2(string target)
        {
            List<string> notes = new List<string>();
            string expected = Path.Combine(Path.GetFullPath(target), "ModOrganizer.exe");
            string[] roots = new string[]
            {
                Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                Environment.GetFolderPath(Environment.SpecialFolder.StartMenu),
                Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu)
            };
            foreach (string root in roots)
            {
                if (String.IsNullOrWhiteSpace(root) || !Directory.Exists(root)) continue;
                Stack<string> pending = new Stack<string>();
                pending.Push(root);
                while (pending.Count > 0)
                {
                    string directory = pending.Pop();
                    string[] links;
                    string[] children;
                    try
                    {
                        links = Directory.GetFiles(directory, "*.lnk", SearchOption.TopDirectoryOnly);
                        children = Directory.GetDirectories(directory, "*", SearchOption.TopDirectoryOnly);
                    }
                    catch { continue; }
                    foreach (string link in links)
                    {
                        try
                        {
                            if (PathsEqual(GetShortcutTarget(link), expected)) File.Delete(link);
                        }
                        catch (Exception ex)
                        {
                            SetupDiagnostics.Error("Could not inspect or remove MO2 shortcut '" + link + "'.", ex);
                            notes.Add(InstallerText.T("acceso directo ") + Path.GetFileName(link) + " (" + ex.Message + ")");
                        }
                    }
                    foreach (string child in children)
                    {
                        try
                        {
                            if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) == 0) pending.Push(child);
                        }
                        catch { }
                    }
                }
            }
            return notes;
        }

        private static List<string> RestoreFiles(InstallState state, bool report, bool preserveUserData,
            bool removeTargetFolderAfterSelfCleanup, bool uninstallMode, BackgroundWorker worker, out bool deferredCleanup)
        {
            List<string> notes = new List<string>();
            deferredCleanup = false;
            string target = Path.GetFullPath(state.TargetDirectory);
            string runningExecutable = Path.GetFullPath(Application.ExecutablePath);
            bool runningUninstallerInTarget = uninstallMode && IsPathWithinDirectory(runningExecutable, target);
            if (runningUninstallerInTarget && File.Exists(runningExecutable))
            {
                string restoreRunningUninstaller = null;
                if (!removeTargetFolderAfterSelfCleanup && !String.IsNullOrWhiteSpace(state.BackupDirectory))
                {
                    foreach (InstallFileState file in state.Files)
                    {
                        try
                        {
                            if (!file.ExistedBefore || file.OwnedByRevamped ||
                                !PathsEqual(SafeCombine(target, file.Path), runningExecutable)) continue;
                            string backup = SafeCombine(state.BackupDirectory, file.Path);
                            if (File.Exists(backup)) restoreRunningUninstaller = backup;
                            break;
                        }
                        catch { }
                    }
                }
                if (ScheduleRunningUninstaller(runningExecutable, restoreRunningUninstaller, target))
                    deferredCleanup = true;
                else
                    notes.Add(UninstallerFileName + InstallerText.T(" (Windows no pudo programar su retirada)"));
            }
            int index = 0;
            foreach (InstallFileState file in state.Files)
            {
                index++;
                if (removeTargetFolderAfterSelfCleanup)
                {
                if (report && index % 40 == 0) Report(worker, Math.Min(96, index * 90 / state.Files.Count), InstallerText.T("Preparando la eliminación completa…"));
                    continue;
                }
                if (preserveUserData && (file.IsUserData || IsMigrationDataPath(file.Path))) continue;
                // uninstalling removes what an earlier Revamped setup installed,
                // instead of putting that older version back from an update's backup
                bool existedBefore = file.ExistedBefore && !(uninstallMode && file.OwnedByRevamped);
                string destination;
                try { destination = SafeCombine(target, file.Path); }
                catch { notes.Add(InstallerText.T("ruta ignorada: ") + file.Path); continue; }
                try
                {
                    if (runningUninstallerInTarget && PathsEqual(runningExecutable, destination))
                        continue;
                    bool exists = File.Exists(destination);
                    if (Directory.Exists(destination))
                    {
                        notes.Add(file.Path + InstallerText.T(" (hay una carpeta en su lugar)"));
                        continue;
                    }
                    if (file.IsPortableMarker && preserveUserData && HasPortableUserData(target))
                    {
                        if (!exists) File.WriteAllBytes(destination, new byte[0]);
                        continue;
                    }
                    string currentHash = exists ? HashFile(destination) : null;
                    if (existedBefore && exists && !String.IsNullOrEmpty(file.OriginalSha256) && HashEquals(currentHash, file.OriginalSha256))
                        continue;
                    if (exists && !HashEquals(currentHash, file.InstalledSha256))
                    {
                        if (removeTargetFolderAfterSelfCleanup)
                        {
                            File.Delete(destination);
                            RemoveEmptyParents(Path.GetDirectoryName(destination), target);
                            continue;
                        }
                        notes.Add(file.Path + InstallerText.T(" (modificado; se conservó)"));
                        continue;
                    }
                    if (!exists && !existedBefore) continue;

                    if (existedBefore)
                    {
                        if (String.IsNullOrEmpty(state.BackupDirectory))
                        {
                            notes.Add(file.Path + InstallerText.T(" (sin copia)"));
                            continue;
                        }
                        string backup = SafeCombine(state.BackupDirectory, file.Path);
                        if (!File.Exists(backup))
                        {
                            notes.Add(file.Path + InstallerText.T(" (copia ausente)"));
                            continue;
                        }
                        if (PathsEqual(Application.ExecutablePath, destination))
                        {
                            if (ScheduleRunningUninstaller(destination, backup, null))
                            {
                                deferredCleanup = true;
                                continue;
                            }
                            notes.Add(file.Path + InstallerText.T(" (Windows no pudo programar su sustitución)"));
                            continue;
                        }
                        CopyFileAtomically(backup, destination);
                    }
                    else
                    {
                        if (PathsEqual(Application.ExecutablePath, destination))
                        {
                            if (ScheduleRunningUninstaller(destination, null, target))
                            {
                                deferredCleanup = true;
                                continue;
                            }
                            notes.Add(file.Path + InstallerText.T(" (Windows no pudo programar su retirada)"));
                            continue;
                        }
                        File.Delete(destination);
                        RemoveEmptyParents(Path.GetDirectoryName(destination), target);
                    }
                }
                catch (Exception fileError)
                {
                    SetupDiagnostics.Error("Could not restore or remove install file '" + file.Path + "'.", fileError);
                    notes.Add(file.Path + " (" + fileError.Message + ")");
                }
                if (report && index % 40 == 0) Report(worker, Math.Min(96, index * 90 / state.Files.Count), InstallerText.T("Restaurando archivos…"));
            }

            foreach (string shortcut in state.ShortcutPaths ?? new List<string>())
            {
                try
                {
                    if (File.Exists(shortcut) && PathsEqual(GetShortcutTarget(shortcut), Path.Combine(target, "ModOrganizer.exe")))
                        File.Delete(shortcut);
                }
                catch { notes.Add(InstallerText.T("acceso directo: ") + Path.GetFileName(shortcut)); }
            }
                if (report) Report(worker, 100, InstallerText.T("Restauración revisada."));
            return notes;
        }

        private static void RemoveEmptyParents(string directory, string stop)
        {
            string current = directory;
            string basePath = Path.GetFullPath(stop).TrimEnd(Path.DirectorySeparatorChar);
            while (!String.IsNullOrEmpty(current))
            {
                string full = Path.GetFullPath(current).TrimEnd(Path.DirectorySeparatorChar);
                if (String.Equals(full, basePath, StringComparison.OrdinalIgnoreCase)) return;
                if (!full.StartsWith(basePath + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) return;
                if (Directory.GetFileSystemEntries(full).Length != 0) return;
                Directory.Delete(full);
                current = Path.GetDirectoryName(full);
            }
        }

        private static bool HasPortableUserData(string target)
        {
            string[] files = new string[] { "ModOrganizer.ini", "categories.dat", "nexuscatmap.dat" };
            foreach (string name in files)
                if (File.Exists(Path.Combine(target, name))) return true;
            // the Elden Ring / Nightreign plugins keep shared saves and save
            // backups here: they can be a save's only copy
            string[] directories = new string[] { "profiles", "mods", "downloads", "overwrite", "cache",
                "Elden Ring Shared Saves", "Elden Ring Global Save Backups",
                "Nightreign Shared Saves", "Nightreign Global Save Backups" };
            foreach (string name in directories)
            {
                string path = Path.Combine(target, name);
                if (Directory.Exists(path) && Directory.GetFileSystemEntries(path).Length > 0) return true;
            }
            return false;
        }

        private static bool IsMigrationDataPath(string relativePath)
        {
            if (String.IsNullOrWhiteSpace(relativePath)) return false;
            string path = relativePath.Replace('/', '\\').TrimStart('\\');
            string[] roots = new string[] { "profiles\\", "mods\\", "downloads\\", "overwrite\\" };
            foreach (string root in roots)
                if (path.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return true;
            return String.Equals(path, "ModOrganizer.ini", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(path, "categories.dat", StringComparison.OrdinalIgnoreCase) ||
                String.Equals(path, "nexuscatmap.dat", StringComparison.OrdinalIgnoreCase);
        }

        [System.Runtime.InteropServices.DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool MoveFileEx(string existingName, string newName, int flags);

        private static bool ScheduleRunningUninstaller(string destination, string backup, string emptyDirectory)
        {
            const int MoveFileReplaceExisting = 0x1;
            const int MoveFileDelayUntilReboot = 0x4;
            try
            {
                string restoreCopy = null;
                if (!String.IsNullOrEmpty(backup))
                {
                    restoreCopy = Path.Combine(Path.GetTempPath(), "MO2RevampedUninstallerRestore-" + Guid.NewGuid().ToString("N") + ".exe");
                    File.Copy(backup, restoreCopy, true);
                }
                // the helper deletes the file once this process exits and queues
                // the reboot deletion only if that fails: queued here, it would
                // also delete the uninstaller of a reinstall made before rebooting
                if (StartPostExitCleanup(destination, restoreCopy, emptyDirectory))
                    return true;
                if (restoreCopy != null && File.Exists(restoreCopy)) File.Delete(restoreCopy);
            }
            catch
            {
            }
            if (String.IsNullOrEmpty(backup))
                return ScheduleDeleteOnReboot(destination, emptyDirectory, MoveFileDelayUntilReboot);
            string pending = destination + ".restore-pending";
            try
            {
                if (File.Exists(pending)) File.Delete(pending);
                File.Copy(backup, pending, true);
                if (MoveFileEx(pending, destination, MoveFileReplaceExisting | MoveFileDelayUntilReboot)) return true;
                File.Delete(pending);
            }
            catch { try { if (File.Exists(pending)) File.Delete(pending); } catch { } }
            return false;
        }

        private static bool ScheduleDeleteOnReboot(string executable, string emptyDirectory, int delayUntilReboot)
        {
            bool executableScheduled = MoveFileEx(executable, null, delayUntilReboot);
            if (String.IsNullOrEmpty(emptyDirectory)) return executableScheduled;

            bool directoriesScheduled = true;
            foreach (string directory in GetSafeDirectoryCleanupCandidates(emptyDirectory))
            {
                try
                {
                    // Never queue non-empty parents: they may contain unrelated user files.
                    if (!Directory.Exists(directory) || Directory.GetFileSystemEntries(directory).Length != 0) break;
                    directoriesScheduled = MoveFileEx(directory, null, delayUntilReboot) && directoriesScheduled;
                }
                catch { break; }
            }
            return executableScheduled && directoriesScheduled;
        }

        private static bool StartPostExitCleanup(string executable, string restoreCopy, string emptyDirectory)
        {
            string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            if (!File.Exists(powershell)) return false;
#if INSTALLER_TEST
            string script = String.Empty;
#else
            int ownerProcessId = Process.GetCurrentProcess().Id;
            string script = "$ownerPid = " + ownerProcessId.ToString() + "; try { $owner = [System.Diagnostics.Process]::GetProcessById($ownerPid); $owner.WaitForExit(); $owner.Dispose() } catch {}; ";
#endif
            script += "$exe = '" + EscapePowerShellLiteral(executable) + "'; ";
            if (!String.IsNullOrEmpty(restoreCopy))
                script += "$src = '" + EscapePowerShellLiteral(restoreCopy) + "'; try { [System.IO.File]::Copy($src, $exe, $true); [System.IO.File]::Delete($src) } catch { exit 2 }; ";
            else
                script += "for ($attempt = 0; $attempt -lt 30 -and [System.IO.File]::Exists($exe); $attempt++) { try { [System.IO.File]::SetAttributes($exe, [System.IO.FileAttributes]::Normal); [System.IO.File]::Delete($exe) } catch {}; if ([System.IO.File]::Exists($exe)) { Start-Sleep -Milliseconds 500 } }; " +
                    "if ([System.IO.File]::Exists($exe)) { try { Add-Type -Namespace Mo2Revamped -Name Reboot -MemberDefinition '[System.Runtime.InteropServices.DllImport(\"kernel32.dll\", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern bool MoveFileEx(string existingName, string newName, int flags);'; [void][Mo2Revamped.Reboot]::MoveFileEx($exe, [NullString]::Value, 4) } catch {}; exit 2 }; ";
            if (!String.IsNullOrEmpty(emptyDirectory))
            {
                foreach (string directory in GetSafeDirectoryCleanupCandidates(emptyDirectory))
                {
                    string escapedDirectory = EscapePowerShellLiteral(directory);
                    script += "$dir = '" + escapedDirectory + "'; try { " +
                        "if (-not [System.IO.Directory]::Exists($dir)) { exit 0 }; " +
                        "$attributes = [System.IO.File]::GetAttributes($dir); " +
                        "if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { exit 0 }; " +
                        "if ([System.IO.Directory]::GetFileSystemEntries($dir).Length -ne 0) { exit 0 }; " +
                        "[System.IO.Directory]::Delete($dir, $false) } catch { exit 0 }; ";
                }
            }
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
            ProcessStartInfo start = new ProcessStartInfo
            {
                FileName = powershell,
                Arguments = "-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + encoded,
                UseShellExecute = false,
                CreateNoWindow = true,
                WindowStyle = ProcessWindowStyle.Hidden
            };
            using (Process helper = Process.Start(start)) return helper != null;
        }

        private static string EscapePowerShellLiteral(string value)
        {
            return (value ?? "").Replace("'", "''");
        }

        private static PayloadManifest ReadManifest(bool eldenRingOnly)
        {
            Assembly assembly = Assembly.GetExecutingAssembly();
            string resourceName = eldenRingOnly ? EldenRingPayloadManifestResource : PayloadManifestResource;
            using (Stream stream = assembly.GetManifestResourceStream(resourceName))
            {
            if (stream == null) throw new InvalidOperationException(InstallerText.T("Falta el manifiesto de archivos integrado en el instalador."));
                using (StreamReader reader = new StreamReader(stream, Encoding.UTF8))
                    return Json.Deserialize<PayloadManifest>(reader.ReadToEnd());
            }
        }

        private static void ValidateManifest(PayloadManifest manifest)
        {
            if (manifest == null || manifest.Files == null || manifest.Files.Count == 0)
                throw new InvalidOperationException(InstallerText.T("El paquete del instalador está vacío o dañado."));
            HashSet<string> paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            long sum = 0;
            foreach (PayloadFile file in manifest.Files)
            {
                if (String.IsNullOrEmpty(file.Path) || !IsSafeRelativePath(file.Path) || !paths.Add(file.Path))
                throw new InvalidOperationException(InstallerText.T("El manifiesto contiene una ruta inválida o duplicada."));
                if (file.Length < 0 || String.IsNullOrEmpty(file.Sha256) || file.Sha256.Length != 64)
                throw new InvalidOperationException(InstallerText.T("El manifiesto contiene datos de archivo inválidos."));
                sum += file.Length;
            }
            if (sum != manifest.TotalBytes) throw new InvalidOperationException(InstallerText.T("El tamaño del manifiesto no coincide."));
        }

        private static PayloadBundle ValidatePayload(PayloadManifest manifest, BackgroundWorker worker, bool eldenRingOnly)
        {
            Dictionary<string, ZipArchiveEntry> entries = new Dictionary<string, ZipArchiveEntry>(StringComparer.OrdinalIgnoreCase);
            string payloadResource = eldenRingOnly ? EldenRingPayloadZipResource : PayloadZipResource;
            Stream stream = Assembly.GetExecutingAssembly().GetManifestResourceStream(payloadResource);
            if (stream == null) throw new InvalidOperationException(InstallerText.T("Falta el paquete de archivos integrado en el instalador."));
            ZipArchive archive = null;
            try
            {
                archive = new ZipArchive(stream, ZipArchiveMode.Read, true);
                foreach (ZipArchiveEntry entry in archive.Entries)
                {
                    string path = entry.FullName.Replace('/', '\\');
                    if (!IsSafeRelativePath(path) || entries.ContainsKey(path))
                        throw new InvalidOperationException(InstallerText.T("El paquete contiene una ruta duplicada o no válida."));
                    entries.Add(path, entry);
                }
                if (entries.Count != manifest.Files.Count)
                    throw new InvalidOperationException(InstallerText.T("El manifiesto y el contenido del paquete no coinciden."));
                int index = 0;
                foreach (PayloadFile file in manifest.Files)
                {
                    ZipArchiveEntry entry;
                    if (!entries.TryGetValue(file.Path, out entry) || entry.Length != file.Length)
                        throw new InvalidOperationException(InstallerText.T("Falta un archivo del paquete: ") + file.Path);
                    using (Stream item = entry.Open())
                    using (SHA256 sha = SHA256.Create())
                    {
                        string hash = BytesToHex(sha.ComputeHash(item));
                        if (!HashEquals(hash, file.Sha256))
                            throw new InvalidOperationException(InstallerText.T("La verificación de integridad falló: ") + file.Path);
                    }
                    index++;
                    if (index % 150 == 0)
                        Report(worker, 2 + (int)(7.0 * index / manifest.Files.Count),
                            String.Format(InstallerText.T("Verificando paquete ({0} / {1})…"), index, manifest.Files.Count));
                }
                return new PayloadBundle(stream, archive, entries);
            }
            catch
            {
                if (archive != null) archive.Dispose();
                stream.Dispose();
                throw;
            }
        }

        private static void WriteZipEntry(ZipArchiveEntry entry, string destination)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            string temp = destination + ".mo2setup.tmp";
            if (File.Exists(temp)) File.Delete(temp);
            using (Stream source = entry.Open())
            using (FileStream output = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                source.CopyTo(output);
            ReplaceFile(temp, destination);
        }

        private static void CopyFileAtomically(string source, string destination)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            string temp = destination + ".mo2setup.tmp";
            if (File.Exists(temp)) File.Delete(temp);
            File.Copy(source, temp, true);
            ReplaceFile(temp, destination);
        }

        private static void WriteBytesAtomically(byte[] bytes, string destination)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(destination));
            string temp = destination + ".mo2setup.tmp";
            if (File.Exists(temp)) File.Delete(temp);
            File.WriteAllBytes(temp, bytes);
            ReplaceFile(temp, destination);
        }

        private static void ReplaceFile(string temp, string destination)
        {
            if (File.Exists(destination))
            {
                try { File.Replace(temp, destination, null, true); }
                catch
                {
                    File.Delete(destination);
                    File.Move(temp, destination);
                }
            }
            else File.Move(temp, destination);
        }

        private static List<CopyEntry> EnumerateMigrationFiles(SetupRequest request)
        {
            List<CopyEntry> files = new List<CopyEntry>();
            string root = Path.GetFullPath(request.SourceDataDirectory);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "profiles_directory", "profiles"), "profiles", request.CopyProfiles);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "mod_directory", "mods"), "mods", request.CopyMods);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "download_directory", "downloads"), "downloads", request.CopyDownloads);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "overwrite_directory", "overwrite"), "overwrite", request.CopyOverwrite);
            if (request.CopySettings)
            {
                string ini = Path.Combine(root, "ModOrganizer.ini");
                if (File.Exists(ini))
                    files.Add(new CopyEntry { Data = MakePortableIni(ini), RelativePath = "ModOrganizer.ini" });
            }
            if (request.CopyCategories)
            {
                AddSelectedFile(files, root, "categories.dat");
                AddSelectedFile(files, root, "nexuscatmap.dat");
            }
            return files;
        }

        public static bool HasSelectedMigrationData(SetupRequest request)
        {
            // stops at the first file found: the window waits on this answer,
            // and a full list of a large mods or downloads folder takes minutes
            try
            {
                string root = Path.GetFullPath(request.SourceDataDirectory);
                if (request.CopyProfiles && TreeHasFile(ResolveConfiguredDirectory(root, "profiles_directory", "profiles"))) return true;
                if (request.CopyMods && TreeHasFile(ResolveConfiguredDirectory(root, "mod_directory", "mods"))) return true;
                if (request.CopyDownloads && TreeHasFile(ResolveConfiguredDirectory(root, "download_directory", "downloads"))) return true;
                if (request.CopyOverwrite && TreeHasFile(ResolveConfiguredDirectory(root, "overwrite_directory", "overwrite"))) return true;
                if (request.CopySettings && File.Exists(Path.Combine(root, "ModOrganizer.ini"))) return true;
                return request.CopyCategories && (File.Exists(Path.Combine(root, "categories.dat")) ||
                    File.Exists(Path.Combine(root, "nexuscatmap.dat")));
            }
            catch { return false; }
        }

        // the same files AddSelectedTree would copy: any one of them is enough
        private static bool TreeHasFile(string start)
        {
            if (!Directory.Exists(start)) return false;
            Stack<string> pending = new Stack<string>();
            pending.Push(start);
            while (pending.Count > 0)
            {
                DirectoryInfo di = new DirectoryInfo(pending.Pop());
                if ((di.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                foreach (FileInfo file in di.EnumerateFiles())
                    if ((file.Attributes & FileAttributes.ReparsePoint) == 0) return true;
                foreach (DirectoryInfo child in di.EnumerateDirectories())
                    if ((child.Attributes & FileAttributes.ReparsePoint) == 0) pending.Push(child.FullName);
            }
            return false;
        }

        private static void AddSelectedFile(List<CopyEntry> result, string root, string name)
        {
            string full = Path.Combine(root, name);
            if (File.Exists(full)) result.Add(new CopyEntry { SourcePath = full, RelativePath = name });
        }

        private static string ResolveConfiguredDirectory(string dataRoot, string key, string defaultName)
        {
            string iniPath = Path.Combine(dataRoot, "ModOrganizer.ini");
            string iniBase = ReadIniSetting(iniPath, "base_directory");
            string baseDirectory = String.IsNullOrEmpty(iniBase) ? dataRoot : ResolveIniPath(iniBase, dataRoot, dataRoot);
            string configured = ReadIniSetting(iniPath, key);
            if (String.IsNullOrEmpty(configured)) configured = "%BASE_DIR%/" + defaultName;
            return ResolveIniPath(configured, baseDirectory, dataRoot);
        }

        private static string ResolveIniPath(string value, string baseDirectory, string fallbackRoot)
        {
            string path = value.Trim().Trim('"').Replace("%BASE_DIR%", baseDirectory);
            path = Environment.ExpandEnvironmentVariables(path);
            path = path.Replace('/', Path.DirectorySeparatorChar).Replace('\\', Path.DirectorySeparatorChar);
            if (!Path.IsPathRooted(path)) path = Path.Combine(baseDirectory, path);
            if (!Path.IsPathRooted(path)) path = Path.Combine(fallbackRoot, path);
            return Path.GetFullPath(path);
        }

        private static byte[] MakePortableIni(string iniPath)
        {
            List<string> lines = new List<string>(File.ReadAllLines(iniPath, Encoding.UTF8));
            List<string> output = new List<string>();
            HashSet<string> normalizedKeys = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
            {
                "base_directory", "profiles_directory", "mod_directory", "download_directory",
                "overwrite_directory", "cache_directory"
            };
            bool inSettings = false;
            bool settingsFound = false;
            bool inserted = false;
            for (int i = 0; i < lines.Count; i++)
            {
                string line = lines[i];
                string trimmed = line.Trim();
                if (trimmed.StartsWith("[", StringComparison.Ordinal) && trimmed.EndsWith("]", StringComparison.Ordinal))
                {
                    if (inSettings && !inserted)
                    {
                        AddPortablePathSettings(output);
                        inserted = true;
                    }
                    inSettings = String.Equals(trimmed, "[Settings]", StringComparison.OrdinalIgnoreCase);
                    if (inSettings) settingsFound = true;
                    output.Add(line);
                    continue;
                }
                if (inSettings)
                {
                    int equals = line.IndexOf('=');
                    if (equals > 0 && normalizedKeys.Contains(line.Substring(0, equals).Trim()))
                        continue;
                }
                output.Add(line);
            }
            if (inSettings && !inserted) AddPortablePathSettings(output);
            if (!settingsFound)
            {
                output.Add("[Settings]");
                AddPortablePathSettings(output);
            }
            string text = String.Join(Environment.NewLine, output.ToArray()) + Environment.NewLine;
            return new UTF8Encoding(false).GetBytes(text);
        }

        private static void AddPortablePathSettings(List<string> lines)
        {
            lines.Add("profiles_directory=%BASE_DIR%/profiles");
            lines.Add("mod_directory=%BASE_DIR%/mods");
            lines.Add("download_directory=%BASE_DIR%/downloads");
            lines.Add("overwrite_directory=%BASE_DIR%/overwrite");
            lines.Add("cache_directory=%BASE_DIR%/cache");
        }

        private static string ReadIniSetting(string iniPath, string key)
        {
            if (!File.Exists(iniPath)) return null;
            bool inSettings = false;
            foreach (string line in File.ReadAllLines(iniPath, Encoding.UTF8))
            {
                string trimmed = line.Trim();
                if (trimmed.StartsWith("[", StringComparison.Ordinal) && trimmed.EndsWith("]", StringComparison.Ordinal))
                {
                    inSettings = String.Equals(trimmed, "[Settings]", StringComparison.OrdinalIgnoreCase);
                    continue;
                }
                if (!inSettings || trimmed.Length == 0 || trimmed.StartsWith(";", StringComparison.Ordinal) || trimmed.StartsWith("#", StringComparison.Ordinal))
                    continue;
                int equals = line.IndexOf('=');
                if (equals <= 0 || !String.Equals(line.Substring(0, equals).Trim(), key, StringComparison.OrdinalIgnoreCase))
                    continue;
                return line.Substring(equals + 1).Trim().Trim('"');
            }
            return null;
        }

        private static void AddSelectedTree(List<CopyEntry> result, string sourceDirectory, string destinationName, bool selected)
        {
            string start = sourceDirectory;
            if (!selected || !Directory.Exists(start)) return;
            Stack<string> pending = new Stack<string>();
            pending.Push(start);
            while (pending.Count > 0)
            {
                string directory = pending.Pop();
                DirectoryInfo di = new DirectoryInfo(directory);
                if ((di.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                foreach (FileInfo file in di.GetFiles())
                {
                    if ((file.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                    // a configured path may end in a separator ("D:\Mods\")
                    string relative = destinationName + Path.DirectorySeparatorChar +
                        file.FullName.Substring(start.Length).TrimStart(Path.DirectorySeparatorChar);
                    result.Add(new CopyEntry { SourcePath = file.FullName, RelativePath = relative });
                }
                foreach (DirectoryInfo child in di.GetDirectories())
                {
                    if ((child.Attributes & FileAttributes.ReparsePoint) == 0) pending.Push(child.FullName);
                }
            }
        }

        private static void CheckSpace(string target, long payloadBytes, long backupBytes)
        {
            string root = Path.GetPathRoot(Path.GetFullPath(target));
            // an update's backup goes to AppData, usually on another drive
            string backupDrive = backupBytes > 0 ? Path.GetPathRoot(Path.GetFullPath(BackupRoot(target))) : root;
            bool sameDrive = String.Equals(root, backupDrive, StringComparison.OrdinalIgnoreCase);
            RequireSpace(root, payloadBytes + (sameDrive ? backupBytes : 0));
            if (!sameDrive) RequireSpace(backupDrive, backupBytes);
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool GetDiskFreeSpaceEx(string directoryName, out ulong freeBytesAvailable,
            out ulong totalNumberOfBytes, out ulong totalNumberOfFreeBytes);

        private static void RequireSpace(string root, long required)
        {
            // DriveInfo rejects network shares (\\server\share); Windows answers for both
            ulong available, total, totalFree;
            string directory = root.EndsWith("\\", StringComparison.Ordinal) ? root : root + "\\";
            if (!GetDiskFreeSpaceEx(directory, out available, out total, out totalFree))
            {
                SetupDiagnostics.Error("Could not read the free space of '" + root + "'; the check was skipped.",
                    new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()));
                return;
            }
            long free = available > (ulong)Int64.MaxValue ? Int64.MaxValue : (long)available;
            if (free < required)
                throw new InvalidOperationException(String.Format(
                    InstallerText.T("No hay espacio libre suficiente. Se necesitan aproximadamente {0} y hay {1} disponibles."),
                    FormatBytes(required), FormatBytes(free)) + " (" + root + ")");
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct RmUniqueProcess
        {
            public int dwProcessId;
            public System.Runtime.InteropServices.ComTypes.FILETIME ProcessStartTime;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct RmProcessInfo
        {
            public RmUniqueProcess Process;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string strAppName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string strServiceShortName;
            public int ApplicationType;
            public uint AppStatus;
            public uint TSSessionId;
            [MarshalAs(UnmanagedType.Bool)] public bool bRestartable;
        }

        [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
        private static extern int RmStartSession(out uint pSessionHandle, int dwSessionFlags, StringBuilder strSessionKey);
        [DllImport("rstrtmgr.dll")]
        private static extern int RmEndSession(uint pSessionHandle);
        [DllImport("rstrtmgr.dll", CharSet = CharSet.Unicode)]
        private static extern int RmRegisterResources(uint pSessionHandle, uint nFiles, string[] rgsFilenames,
            uint nApplications, IntPtr rgApplications, uint nServices, string[] rgsServiceNames);
        [DllImport("rstrtmgr.dll")]
        private static extern int RmGetList(uint dwSessionHandle, out uint pnProcInfoNeeded, ref uint pnProcInfo,
            [In, Out] RmProcessInfo[] rgAffectedApps, ref uint lpdwRebootReasons);

        // The programs Windows reports as holding the installation's program
        // files (an Explorer preview, a game started from MO2, another tool),
        // so setup can name them instead of failing half-way.
        private static List<string> ProgramsUsingInstallation(string target)
        {
            List<string> names = new List<string>();
            List<string> files = new List<string>();
            try
            {
                foreach (string file in Directory.GetFiles(target, "*.*", SearchOption.TopDirectoryOnly))
                {
                    string extension = Path.GetExtension(file);
                    if (String.Equals(extension, ".exe", StringComparison.OrdinalIgnoreCase) ||
                        String.Equals(extension, ".dll", StringComparison.OrdinalIgnoreCase)) files.Add(file);
                }
                // the program folders too (plugins, Python modules, Qt
                // platforms...): a file held there also stops the update half-way
                foreach (string directoryName in Mo2ProgramDirectories)
                {
                    Stack<string> pending = new Stack<string>();
                    pending.Push(Path.Combine(target, directoryName));
                    while (pending.Count > 0)
                    {
                        try
                        {
                            DirectoryInfo directory = new DirectoryInfo(pending.Pop());
                            if (!directory.Exists || (directory.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                            foreach (FileInfo file in directory.GetFiles())
                            {
                                string extension = file.Extension;
                                if (String.Equals(extension, ".exe", StringComparison.OrdinalIgnoreCase) ||
                                    String.Equals(extension, ".dll", StringComparison.OrdinalIgnoreCase) ||
                                    String.Equals(extension, ".pyd", StringComparison.OrdinalIgnoreCase)) files.Add(file.FullName);
                            }
                            foreach (DirectoryInfo child in directory.GetDirectories()) pending.Push(child.FullName);
                        }
                        catch { }
                    }
                }
            }
            catch { return names; }
            if (files.Count == 0) return names;

            uint session;
            StringBuilder key = new StringBuilder(64);
            if (RmStartSession(out session, 0, key) != 0) return names;
            try
            {
                if (RmRegisterResources(session, (uint)files.Count, files.ToArray(), 0, IntPtr.Zero, 0, null) != 0) return names;
                uint needed;
                uint count = 0;
                uint reasons = 0;
                int result = RmGetList(session, out needed, ref count, null, ref reasons);
                if (result == 234 && needed > 0) // ERROR_MORE_DATA
                {
                    RmProcessInfo[] processes = new RmProcessInfo[needed];
                    count = needed;
                    int self = Process.GetCurrentProcess().Id;
                    if (RmGetList(session, out needed, ref count, processes, ref reasons) == 0)
                        for (int i = 0; i < count; i++)
                        {
                            // the uninstaller itself runs from the installation
                            if (processes[i].Process.dwProcessId == self) continue;
                            string name = processes[i].strAppName;
                            if (!String.IsNullOrEmpty(name) && !names.Contains(name)) names.Add(name);
                        }
                }
            }
            catch (Exception ex) { SetupDiagnostics.Error("Restart Manager could not list the programs using the installation.", ex); }
            finally { RmEndSession(session); }
            return names;
        }

        // backups of earlier updates of this installation; the one Restore uses is kept
        private static void PruneOlderBackups(string target, string keep)
        {
            try
            {
                string root = BackupRoot(target);
                if (!Directory.Exists(root) || String.IsNullOrEmpty(keep)) return;
                foreach (string folder in Directory.GetDirectories(root))
                {
                    if (PathsEqual(folder, keep)) continue;
                    try { Directory.Delete(folder, true); }
                    catch (Exception ex) { SetupDiagnostics.Error("Could not remove an older update backup '" + folder + "'.", ex); }
                }
            }
            catch (Exception ex) { SetupDiagnostics.Error("Could not prune older update backups.", ex); }
        }

        private static List<string> GetShortcutPaths(SetupRequest request)
        {
            List<string> paths = new List<string>();
            string target = Path.GetFullPath(request.TargetDirectory).TrimEnd('\\');
            string targetExe = Path.Combine(target, "ModOrganizer.exe");
            if (request.StartMenuShortcut)
            {
                string directory = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "MO2 Revamped");
                paths.Add(ResolveShortcutPath(directory, "MO2 Revamped", target, targetExe));
            }
            if (request.DesktopShortcut)
            {
                string directory = Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory);
                paths.Add(ResolveShortcutPath(directory, "MO2 Revamped", target, targetExe));
            }
            return paths;
        }

        private static string ResolveShortcutPath(string directory, string baseName, string targetDirectory, string targetExe)
        {
            string preferred = Path.Combine(directory, baseName + ".lnk");
            if (!File.Exists(preferred) || ShortcutTargets(preferred, targetExe)) return preferred;

            string targetKey = HashBytes(Encoding.UTF8.GetBytes(Path.GetFullPath(targetDirectory).TrimEnd('\\').ToLowerInvariant())).Substring(0, 8);
            string hashed = Path.Combine(directory, baseName + " - " + targetKey + ".lnk");
            if (!File.Exists(hashed) || ShortcutTargets(hashed, targetExe)) return hashed;

            for (int suffix = 2; suffix < 1000; suffix++)
            {
                string candidate = Path.Combine(directory, baseName + " - " + targetKey + " (" + suffix + ").lnk");
                if (!File.Exists(candidate) || ShortcutTargets(candidate, targetExe)) return candidate;
            }
            throw new IOException(InstallerText.T("No se pudo elegir un nombre libre para el acceso directo de MO2."));
        }

        private static bool ShortcutTargets(string link, string targetExe)
        {
            try { return PathsEqual(GetShortcutTarget(link), targetExe); }
            catch { return false; }
        }

        private static void CreateShortcuts(SetupRequest request, InstallState state)
        {
            string target = Path.Combine(request.TargetDirectory, "ModOrganizer.exe");
            List<string> shortcutPaths = GetShortcutPaths(request);
            foreach (string link in shortcutPaths)
            {
                Directory.CreateDirectory(Path.GetDirectoryName(link));
                Type shellType = Type.GetTypeFromProgID("WScript.Shell");
                if (shellType == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo crear un acceso directo."));
                object shell = Activator.CreateInstance(shellType);
                object shortcut = null;
                try
                {
                    shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
                    Type shortcutType = shortcut.GetType();
                    shortcutType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
                    shortcutType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { request.TargetDirectory });
                    shortcutType.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, new object[] { "MO2 Revamped" });
                    string shortcutIcon = Path.Combine(request.TargetDirectory, "MO2-Revamped.ico");
                    string iconLocation = File.Exists(shortcutIcon) ? shortcutIcon + ",0" : target + ",0";
                    shortcutType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, shortcut, new object[] { iconLocation });
                    shortcutType.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
                }
                finally
                {
                    if (shortcut != null && System.Runtime.InteropServices.Marshal.IsComObject(shortcut))
                        System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
                    if (System.Runtime.InteropServices.Marshal.IsComObject(shell))
                        System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
                }
            }
            RemoveObsoleteHashedShortcuts(request, target, shortcutPaths);
        }

        private static void RemoveObsoleteHashedShortcuts(SetupRequest request, string targetExe, List<string> activePaths)
        {
            string target = Path.GetFullPath(request.TargetDirectory).TrimEnd('\\');
            string folderName = new DirectoryInfo(target).Name;
            List<Tuple<string, string>> searchRoots = new List<Tuple<string, string>>();
            if (request.StartMenuShortcut)
            {
                string startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "MO2 Revamped");
                searchRoots.Add(Tuple.Create(
                    startMenu,
                    "MO2 " + folderName + " - "));
                searchRoots.Add(Tuple.Create(startMenu, "MO2 - " + folderName + " - "));
            }
            if (request.DesktopShortcut)
                searchRoots.Add(Tuple.Create(
                    Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                    "MO2 Revamped - " + folderName + " - "));

            List<string> legacyPaths = new List<string>();
            if (request.StartMenuShortcut)
            {
                string startMenu = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu), "Programs", "MO2 Revamped");
                legacyPaths.Add(Path.Combine(startMenu, "MO2 " + folderName + ".lnk"));
                legacyPaths.Add(Path.Combine(startMenu, "MO2 - " + folderName + ".lnk"));
            }
            if (request.DesktopShortcut)
                legacyPaths.Add(Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory),
                    "MO2 Revamped - " + folderName + ".lnk"));

            foreach (string legacyPath in legacyPaths)
            {
                if (activePaths.Any(active => PathsEqual(active, legacyPath))) continue;
                if (!ShortcutTargets(legacyPath, targetExe)) continue;
                try { File.Delete(legacyPath); }
                catch { }
            }

            foreach (Tuple<string, string> root in searchRoots)
            {
                if (String.IsNullOrWhiteSpace(root.Item1) || !Directory.Exists(root.Item1)) continue;
                string[] links;
                try { links = Directory.GetFiles(root.Item1, "*.lnk", SearchOption.TopDirectoryOnly); }
                catch { continue; }
                foreach (string link in links)
                {
                    string name = Path.GetFileNameWithoutExtension(link);
                    if (!name.StartsWith(root.Item2, StringComparison.OrdinalIgnoreCase)) continue;
                    string suffix = name.Substring(root.Item2.Length);
                    if (suffix.Length != 8 || !suffix.All(Uri.IsHexDigit)) continue;
                    if (activePaths.Any(active => PathsEqual(active, link))) continue;
                    if (!ShortcutTargets(link, targetExe)) continue;
                    try { File.Delete(link); }
                    catch { }
                }
            }
        }

        private static string GetShortcutTarget(string link)
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null) return "";
            object shell = Activator.CreateInstance(shellType);
            object shortcut = null;
            try
            {
                shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
                return Convert.ToString(shortcut.GetType().InvokeMember("TargetPath", BindingFlags.GetProperty, null, shortcut, null));
            }
            finally
            {
                if (shortcut != null && System.Runtime.InteropServices.Marshal.IsComObject(shortcut))
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shortcut);
                if (System.Runtime.InteropServices.Marshal.IsComObject(shell))
                    System.Runtime.InteropServices.Marshal.FinalReleaseComObject(shell);
            }
        }

        private static void EnsureMo2Closed(string target, bool forceClose, BackgroundWorker worker)
        {
            string expected = Path.GetFullPath(Path.Combine(target, "ModOrganizer.exe"));
            // a game started from this MO2 holds its files (usvfs) and, while it
            // runs, the Steam saves MO2 set aside: it is never closed by setup
            if (File.Exists(expected))
            {
                foreach (string game in new string[] { "eldenring", "nightreign" })
                {
                    Process[] running = Process.GetProcessesByName(game);
                    bool found = running.Length > 0;
                    foreach (Process process in running) process.Dispose();
                    if (found)
                        throw new InvalidOperationException(InstallerText.T("Elden Ring o Nightreign siguen abiertos. Ciérralos antes de continuar; al cerrar el juego, MO2 devuelve los guardados de Steam a su sitio."));
                }
            }
            List<Process> matches = FindMo2Processes(expected);
            if (matches.Count == 0) return;
            if (!forceClose)
            {
                foreach (Process process in matches) process.Dispose();
                throw new InvalidOperationException(InstallerText.T("MO2 sigue abierto en esta carpeta. Cierra MO2 o marca la opción para forzar su cierre."));
            }

            Report(worker, 1, InstallerText.T("Cerrando MO2 de esta instalación…"));
            foreach (Process process in matches)
            {
                try { process.CloseMainWindow(); }
                catch { }
            }
            foreach (Process process in matches)
            {
                try { process.WaitForExit(2500); }
                catch { }
            }
            foreach (Process process in matches)
            {
                try
                {
                    if (!process.HasExited && PathsEqual(process.MainModule.FileName, expected))
                    {
                        process.Kill();
                        if (!process.WaitForExit(5000))
                            throw new InvalidOperationException(InstallerText.T("Windows no pudo cerrar MO2 de esta instalación."));
                    }
                }
                catch (InvalidOperationException) { throw; }
                catch (Exception ex)
                {
                    if (!process.HasExited)
                        throw new InvalidOperationException(InstallerText.T("No se pudo forzar el cierre de MO2 en esta carpeta: ") + ex.Message, ex);
                }
                finally { process.Dispose(); }
            }
        }

        private static void EnsureRevampedGlobalDataNotInUse(SetupRequest request)
        {
            if (request == null || !request.RemoveRevampedAppData) return;
            // elevated with another administrator's account, AppData\Local is
            // that account's, not the one of the person at the screen
            if (RunsAsAnotherAccount())
                throw new InvalidOperationException(InstallerText.T("El instalador se está ejecutando con otra cuenta de administrador y no puede borrar los datos globales de tu cuenta. Desmarca esa opción o desinstala desde una cuenta de administrador."));
            string dataDirectory = RevampedAppDataDirectory();
            if (Directory.Exists(dataDirectory) && IsAnyRevampedGlobalMo2Running())
                throw new InvalidOperationException(InstallerText.T("Cierra todas las instancias no portables de MO2 Revamped antes de borrar sus datos globales."));
        }

        [DllImport("wtsapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool WTSQuerySessionInformation(IntPtr server, int sessionId, int infoClass, out IntPtr buffer, out int bytesReturned);
        [DllImport("wtsapi32.dll")]
        private static extern void WTSFreeMemory(IntPtr memory);

        private static string SessionText(int infoClass)
        {
            IntPtr buffer;
            int bytes;
            // the session this process runs in (-1): its signed-in user
            if (!WTSQuerySessionInformation(IntPtr.Zero, -1, infoClass, out buffer, out bytes) || buffer == IntPtr.Zero) return null;
            try { return Marshal.PtrToStringUni(buffer); }
            finally { WTSFreeMemory(buffer); }
        }

        // true when the user signed in to this session is not the account
        // this process runs as (an administrator's credentials typed at the
        // elevation prompt); unknown counts as the same account
        private static bool RunsAsAnotherAccount()
        {
            try
            {
                const int WtsUserName = 5;
                const int WtsDomainName = 7;
                string user = SessionText(WtsUserName);
                string domain = SessionText(WtsDomainName);
                if (String.IsNullOrEmpty(user)) return false;
                return !String.Equals(user, Environment.UserName, StringComparison.OrdinalIgnoreCase) ||
                    (!String.IsNullOrEmpty(domain) && !String.Equals(domain, Environment.UserDomainName, StringComparison.OrdinalIgnoreCase));
            }
            catch { return false; }
        }

        // MO2 of this folder closed (or refused), the games closed, and no
        // other program holding the program files
        private static void EnsureInstallationFree(string target, bool forceClose, BackgroundWorker worker)
        {
            EnsureMo2Closed(target, forceClose, worker);
            List<string> users = ProgramsUsingInstallation(target);
            if (users.Count > 0)
                throw new InvalidOperationException(InstallerText.T("Estos programas están usando archivos de la instalación: ") +
                    String.Join(", ", users.ToArray()) + InstallerText.T(". Ciérralos y vuelve a intentarlo."));
        }

        private static List<Process> FindMo2Processes(string expectedExecutable)
        {
            List<Process> matches = new List<Process>();
            foreach (Process process in Process.GetProcessesByName("ModOrganizer"))
            {
                bool match = false;
                try { match = PathsEqual(process.MainModule.FileName, expectedExecutable); }
                catch { }
                if (match) matches.Add(process);
                else process.Dispose();
            }
            return matches;
        }

        private static bool IsAnyRevampedGlobalMo2Running()
        {
            foreach (Process process in Process.GetProcessesByName("ModOrganizer"))
            {
                try
                {
                    if (process.HasExited) continue;
                    string executable = process.MainModule.FileName;
                    string directory = Path.GetDirectoryName(executable);
                    if (String.IsNullOrWhiteSpace(directory)) return true;

                    bool portable = File.Exists(Path.Combine(directory, "eldenring-only.portable")) ||
                        File.Exists(Path.Combine(directory, "portable.txt")) ||
                        File.Exists(Path.Combine(directory, "ModOrganizer.ini"));
                    if (!portable && IsRevampedInstallation(directory)) return true;
                }
                catch
                {
                    return true;
                }
                finally { process.Dispose(); }
            }
            return false;
        }

        private static List<string> RemoveAllRemainingContents(string target)
        {
            List<string> notes = new List<string>();
            string root = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string runningExecutable = Path.GetFullPath(Application.ExecutablePath);
            DeleteDirectoryContents(root, runningExecutable, notes);
            return notes;
        }

        private static List<string> RemoveRevampedGlobalData()
        {
            List<string> notes = new List<string>();
            string dataDirectory;
            try
            {
                dataDirectory = RevampedAppDataDirectory();
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not resolve the MO2 Revamped AppData directory.", ex);
                notes.Add(InstallerText.T("MO2 Revamped AppData (no se pudo determinar la ruta: ") + ex.Message + ")");
                return notes;
            }

            if (!Directory.Exists(dataDirectory))
            {
                SetupDiagnostics.Info("MO2 Revamped global data directory did not exist: '" + dataDirectory + "'.");
                return notes;
            }

            if (IsAnyRevampedGlobalMo2Running())
            {
                string message = InstallerText.T("Cierra todas las instancias no portables de MO2 Revamped antes de borrar sus datos globales.");
                SetupDiagnostics.Info(message);
                notes.Add(message);
                return notes;
            }

            try
            {
                FileAttributes attributes = File.GetAttributes(dataDirectory);
                if ((attributes & FileAttributes.ReparsePoint) != 0)
                {
                    SetupDiagnostics.Info("MO2 Revamped AppData directory is a reparse point; cleanup was skipped.");
                    notes.Add(InstallerText.T("MO2 Revamped AppData (la carpeta es un vínculo del sistema y no se tocó)"));
                    return notes;
                }

                SetupDiagnostics.Info("Removing MO2 Revamped global data from '" + dataDirectory + "'.");
                DeleteDirectoryContents(dataDirectory, Path.GetFullPath(Application.ExecutablePath), notes);
                if (notes.Count == 0 && Directory.Exists(dataDirectory))
                {
                    if (Directory.GetFileSystemEntries(dataDirectory).Length == 0)
                        Directory.Delete(dataDirectory, false);
                    else
                        notes.Add(InstallerText.T("MO2 Revamped AppData (quedaron elementos en ") + dataDirectory + ")");
                }
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not remove MO2 Revamped AppData directory '" + dataDirectory + "'.", ex);
                notes.Add("MO2 Revamped AppData (" + dataDirectory + ": " + ex.Message + ")");
            }

            return notes;
        }

        private static void DeleteDirectoryContents(string directory, string runningExecutable, List<string> notes)
        {
            string[] entries;
            try { entries = Directory.GetFileSystemEntries(directory); }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not enumerate directory '" + directory + "' during complete removal.", ex);
                notes.Add(InstallerText.T("no se pudo leer ") + directory + " (" + ex.Message + ")");
                return;
            }
            foreach (string entry in entries)
            {
                try
                {
                    string full = Path.GetFullPath(entry);
                    FileAttributes attributes = File.GetAttributes(full);
                    bool isDirectory = (attributes & FileAttributes.Directory) != 0;
                    bool isReparsePoint = (attributes & FileAttributes.ReparsePoint) != 0;
                    if ((attributes & FileAttributes.ReadOnly) != 0)
                    {
                        File.SetAttributes(full, attributes & ~FileAttributes.ReadOnly);
                        attributes &= ~FileAttributes.ReadOnly;
                    }
                    if (PathsEqual(full, runningExecutable)) continue;
                    if (isDirectory && !isReparsePoint)
                    {
                        DeleteDirectoryContents(full, runningExecutable, notes);
                        if (Directory.Exists(full) && Directory.GetFileSystemEntries(full).Length == 0)
                            Directory.Delete(full, false);
                    }
                    else if (isDirectory) Directory.Delete(full, false);
                    else File.Delete(full);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove complete-removal entry '" + entry + "'.", ex);
                    notes.Add(Path.GetFileName(entry) + " (" + ex.Message + ")");
                }
            }
        }

        private static void RemoveEmptyDirectories(string target)
        {
            // only inside the program folders: an empty folder elsewhere may be
            // the user's own (an empty profile, mod or downloads folder)
            string root = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            foreach (string name in Mo2ProgramDirectories)
            {
                string directory = Path.Combine(root, name);
                try
                {
                    if (!Directory.Exists(directory) || (File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) continue;
                    RemoveEmptyDirectoriesBelow(directory);
                    if (Directory.GetFileSystemEntries(directory).Length == 0) Directory.Delete(directory, false);
                }
                catch { }
            }
        }

        private static bool IsPathWithinDirectory(string path, string directory)
        {
            try
            {
                string fullPath = Path.GetFullPath(path);
                string fullDirectory = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                string prefix = fullDirectory + Path.DirectorySeparatorChar;
                return fullPath.StartsWith(prefix, StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        private static string NormalizeCleanupDirectory(string path)
        {
            string fullPath = Path.GetFullPath(path);
            string root = Path.GetPathRoot(fullPath);
            return PathsEqual(fullPath, root)
                ? root
                : fullPath.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
        }

        private static HashSet<string> GetProtectedCleanupBoundaries()
        {
            HashSet<string> boundaries = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (Environment.SpecialFolder folder in new Environment.SpecialFolder[]
            {
                Environment.SpecialFolder.Windows,
                Environment.SpecialFolder.System,
                Environment.SpecialFolder.SystemX86,
                Environment.SpecialFolder.ProgramFiles,
                Environment.SpecialFolder.ProgramFilesX86,
                Environment.SpecialFolder.CommonProgramFiles,
                Environment.SpecialFolder.CommonProgramFilesX86,
                Environment.SpecialFolder.CommonApplicationData,
                Environment.SpecialFolder.ApplicationData,
                Environment.SpecialFolder.LocalApplicationData,
                Environment.SpecialFolder.MyDocuments,
                Environment.SpecialFolder.DesktopDirectory,
                Environment.SpecialFolder.UserProfile,
                Environment.SpecialFolder.StartMenu,
                Environment.SpecialFolder.CommonStartMenu,
                Environment.SpecialFolder.Startup,
                Environment.SpecialFolder.CommonStartup,
                Environment.SpecialFolder.Fonts
            })
            {
                string specialPath = Environment.GetFolderPath(folder);
                if (String.IsNullOrWhiteSpace(specialPath)) continue;
                try { boundaries.Add(NormalizeCleanupDirectory(specialPath)); }
                catch { }
            }
            return boundaries;
        }

        private static bool IsProtectedCleanupBoundary(string directory, HashSet<string> boundaries)
        {
            string root = Path.GetPathRoot(directory);
            return String.IsNullOrEmpty(root) || PathsEqual(directory, root) || boundaries.Contains(directory);
        }

        private static void ValidateSafeUninstallTarget(string target)
        {
            string fullTarget = NormalizeCleanupDirectory(target);
            HashSet<string> boundaries = GetProtectedCleanupBoundaries();
            if (IsProtectedCleanupBoundary(fullTarget, boundaries))
                throw new InvalidOperationException(InstallerText.T("No se permite borrar una unidad ni una carpeta protegida de Windows o del usuario."));

            foreach (string boundary in boundaries)
            {
                if (IsPathWithinDirectory(boundary, fullTarget))
                    throw new InvalidOperationException(InstallerText.T("La carpeta elegida contiene una ubicación protegida de Windows o del usuario."));
            }

            string current = fullTarget;
            while (!String.IsNullOrEmpty(current) && !PathsEqual(current, Path.GetPathRoot(current)))
            {
                if (Directory.Exists(current) &&
                    (File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidOperationException(InstallerText.T("No se puede eliminar esta instalación a través de un vínculo del sistema."));
                DirectoryInfo parent = Directory.GetParent(current);
                if (parent == null) break;
                current = NormalizeCleanupDirectory(parent.FullName);
            }
        }

        private static List<string> GetSafeDirectoryCleanupCandidates(string directory)
        {
            List<string> candidates = new List<string>();
            if (String.IsNullOrWhiteSpace(directory)) return candidates;

            HashSet<string> boundaries = GetProtectedCleanupBoundaries();
            string current;
            try { current = NormalizeCleanupDirectory(directory); }
            catch { return candidates; }

            while (!String.IsNullOrEmpty(current) && !IsProtectedCleanupBoundary(current, boundaries))
            {
                try
                {
                    if (!Directory.Exists(current)) break;
                    if ((File.GetAttributes(current) & FileAttributes.ReparsePoint) != 0) break;
                }
                catch { break; }

                candidates.Add(current);
                DirectoryInfo parent = Directory.GetParent(current);
                if (parent == null) break;
                string next;
                try { next = NormalizeCleanupDirectory(parent.FullName); }
                catch { break; }
                if (PathsEqual(current, next)) break;
                current = next;
            }

            return candidates;
        }

        private static void RemoveEmptyInstallDirectoryAndParent(string target)
        {
            foreach (string directory in GetSafeDirectoryCleanupCandidates(target))
            {
                try
                {
                    if (!Directory.Exists(directory)) continue;
                    if (Directory.GetFileSystemEntries(directory).Length != 0) break;
                    Directory.Delete(directory, false);
                }
                catch { break; }
            }
        }

        private static void RemoveEmptyDirectoriesBelow(string directory)
        {
            string[] entries;
            try { entries = Directory.GetDirectories(directory); }
            catch { return; }
            foreach (string child in entries)
            {
                try
                {
                    FileAttributes attributes = File.GetAttributes(child);
                    if ((attributes & FileAttributes.ReparsePoint) != 0) continue;
                    RemoveEmptyDirectoriesBelow(child);
                    if (Directory.Exists(child) && Directory.GetFileSystemEntries(child).Length == 0)
                        Directory.Delete(child, false);
                }
                catch { }
            }
        }

        private static bool IsMo2Running(string target)
        {
            string expected = Path.GetFullPath(Path.Combine(target, "ModOrganizer.exe"));
            foreach (Process process in Process.GetProcessesByName("ModOrganizer"))
            {
                try
                {
                    if (PathsEqual(process.MainModule.FileName, expected)) return true;
                }
                catch { return true; }
                finally { process.Dispose(); }
            }
            return false;
        }

        private static void SaveState(InstallState state)
        {
            string path = StatePathFor(state.TargetDirectory);
            Directory.CreateDirectory(Path.GetDirectoryName(path));
            string temp = path + ".tmp";
            File.WriteAllText(temp, Json.Serialize(state), new UTF8Encoding(false));
            if (File.Exists(path))
            {
                try { File.Replace(temp, path, null, true); }
                catch { File.Delete(path); File.Move(temp, path); }
            }
            else File.Move(temp, path);
        }

        private static string StatePathFor(string target)
        {
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), StateFolder, "state");
            return Path.Combine(root, TargetKey(target) + ".json");
        }

        private static string LegacyStatePathFor(string target)
        {
            string root = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), LegacyStateFolder, "state");
            return Path.Combine(root, TargetKey(target) + ".json");
        }

        private static string BackupRoot(string target)
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), StateFolder, "backups", TargetKey(target));
        }

        private static string LegacyBackupRoot(string target)
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), LegacyStateFolder, "backups", TargetKey(target));
        }

        private static void CleanupBackup(string backupDirectory, string target)
        {
            if (String.IsNullOrWhiteSpace(backupDirectory)) return;
            string backup = Path.GetFullPath(backupDirectory).TrimEnd(Path.DirectorySeparatorChar);
            string[] allowedRootPaths = new string[] { BackupRoot(target), LegacyBackupRoot(target) };
            string matchedRootPath = null;
            foreach (string allowedRootPath in allowedRootPaths)
            {
                string fullRoot = Path.GetFullPath(allowedRootPath).TrimEnd(Path.DirectorySeparatorChar);
                if (backup.StartsWith(fullRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
                {
                    matchedRootPath = fullRoot;
                    break;
                }
            }
            if (matchedRootPath == null)
                throw new InvalidOperationException(InstallerText.T("La ruta de copia de seguridad no pertenece a esta instalación."));
            foreach (string allowedRootPath in allowedRootPaths)
            {
                string rootPath = Path.GetFullPath(allowedRootPath).TrimEnd(Path.DirectorySeparatorChar);
                if (Directory.Exists(rootPath)) Directory.Delete(rootPath, true);
                string backupsDirectory = Path.GetDirectoryName(rootPath);
                if (Directory.Exists(backupsDirectory) && Directory.GetFileSystemEntries(backupsDirectory).Length == 0)
                    Directory.Delete(backupsDirectory, false);
            }
        }

        private static bool IsBackupPathForTarget(string backupDirectory, string target)
        {
            try
            {
                string backup = Path.GetFullPath(backupDirectory).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                string[] allowedRoots = new string[] { BackupRoot(target), LegacyBackupRoot(target) };
                foreach (string allowedRoot in allowedRoots)
                {
                    string root = Path.GetFullPath(allowedRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                    if (backup.StartsWith(root, StringComparison.OrdinalIgnoreCase)) return true;
                }
                return false;
            }
            catch { return false; }
        }

        private static void DeleteState(string target)
        {
            string[] paths = new string[] { StatePathFor(target), LegacyStatePathFor(target) };
            foreach (string path in paths)
            {
                if (File.Exists(path)) File.Delete(path);
                string root = Path.GetDirectoryName(path);
                if (Directory.Exists(root) && Directory.GetFileSystemEntries(root).Length == 0)
                    Directory.Delete(root, false);
            }
        }

        private static void CleanupInstallerDataRoot()
        {
            string[] roots = new string[] {
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), StateFolder),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), LegacyStateFolder)
            };
            foreach (string root in roots)
            {
                try
                {
                    if (Directory.Exists(root) && Directory.GetFileSystemEntries(root).Length == 0)
                        Directory.Delete(root, false);
                }
                catch { }
            }
        }

        private static string TargetKey(string target)
        {
            return HashBytes(Encoding.UTF8.GetBytes(Path.GetFullPath(target).TrimEnd('\\').ToLowerInvariant())).Substring(0, 32);
        }

        private static string UninstallSubKey(string target)
        {
            return UninstallKeyRoot + "MO2Revamped-" + TargetKey(target);
        }

        private static string LegacyUninstallSubKey(string target)
        {
#if INSTALLER_TEST
            return "Software\\MO2 Fork Installer Test\\Uninstall\\MO2ForkOptimizado-" + TargetKey(target);
#else
            return "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\MO2ForkOptimizado-" + TargetKey(target);
#endif
        }

        private static void RegisterUninstallEntry(string target, string packageVersion, long installedBytes)
        {
            string normalizedTarget = Path.GetFullPath(target).TrimEnd('\\');
            using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
            using (RegistryKey key = root.CreateSubKey(UninstallSubKey(normalizedTarget)))
            {
                if (key == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo registrar la opción de desinstalación."));
                string executable = Path.Combine(normalizedTarget, "ModOrganizer.exe");
                string uninstaller = Path.Combine(normalizedTarget, UninstallerFileName);
                key.SetValue("DisplayName", "MO2 Revamped", RegistryValueKind.String);
                key.SetValue("DisplayVersion", packageVersion, RegistryValueKind.String);
                key.SetValue("Publisher", "ArialSenki", RegistryValueKind.String);
                key.SetValue("InstallLocation", normalizedTarget, RegistryValueKind.String);
                key.SetValue("DisplayIcon", executable + ",0", RegistryValueKind.String);
                key.SetValue("UninstallString", "\"" + uninstaller + "\" --uninstall \"" + normalizedTarget + "\"", RegistryValueKind.String);
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("EstimatedSize", (int)Math.Min(Int32.MaxValue, installedBytes / 1024), RegistryValueKind.DWord);
                key.SetValue("InstallDate", DateTime.Now.ToString("yyyyMMdd"), RegistryValueKind.String);
            }
            try
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
                using (RegistryKey legacy = root.OpenSubKey(LegacyUninstallSubKey(normalizedTarget), false))
                {
                    if (legacy != null && PathsEqual(Convert.ToString(legacy.GetValue("InstallLocation")), normalizedTarget))
                        root.DeleteSubKeyTree(LegacyUninstallSubKey(normalizedTarget));
                }
            }
            catch { }
        }

        private static string RegisteredDisplayVersion(string target)
        {
            try
            {
                string normalizedTarget = Path.GetFullPath(target).TrimEnd('\\');
                using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
                using (RegistryKey key = root.OpenSubKey(UninstallSubKey(normalizedTarget), false))
                {
                    if (key == null || !PathsEqual(Convert.ToString(key.GetValue("InstallLocation")), normalizedTarget)) return null;
                    string version = Convert.ToString(key.GetValue("DisplayVersion"));
                    return String.IsNullOrWhiteSpace(version) ? null : version;
                }
            }
            catch { return null; }
        }

        private static void RestoreRegisteredDisplayVersion(string target, string version)
        {
            if (String.IsNullOrWhiteSpace(version)) return;
            string normalizedTarget = Path.GetFullPath(target).TrimEnd('\\');
            using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
            using (RegistryKey key = root.OpenSubKey(UninstallSubKey(normalizedTarget), true))
            {
                if (key == null || !PathsEqual(Convert.ToString(key.GetValue("InstallLocation")), normalizedTarget)) return;
                key.SetValue("DisplayVersion", version, RegistryValueKind.String);
            }
        }

        internal static List<string> RegisteredInstallLocations()
        {
            List<string> locations = new List<string>();
            try
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
                using (RegistryKey uninstall = root.OpenSubKey(UninstallKeyRoot.TrimEnd('\\'), false))
                {
                    if (uninstall == null) return locations;
                    foreach (string name in uninstall.GetSubKeyNames())
                    {
                        if (!name.StartsWith("MO2Revamped-", StringComparison.OrdinalIgnoreCase) &&
                            !name.StartsWith("MO2ForkOptimizado-", StringComparison.OrdinalIgnoreCase)) continue;
                        using (RegistryKey entry = uninstall.OpenSubKey(name, false))
                        {
                            string location = entry == null ? null : Convert.ToString(entry.GetValue("InstallLocation"));
                            if (String.IsNullOrWhiteSpace(location) || !IsRevampedInstallation(location)) continue;
                            if (!locations.Exists(existing => PathsEqual(existing, location))) locations.Add(location);
                        }
                    }
                }
            }
            catch { }
            return locations;
        }

        private static string RemoveUninstallEntry(string target)
        {
            string[] subKeys = new string[] { UninstallSubKey(target), LegacyUninstallSubKey(target) };
            try
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
                {
                    foreach (string subKey in subKeys)
                    {
                        using (RegistryKey key = root.OpenSubKey(subKey, false))
                        {
                            if (key == null) continue;
                            string registeredPath = Convert.ToString(key.GetValue("InstallLocation"));
                            if (!PathsEqual(registeredPath, target))
                                return InstallerText.T("La entrada de desinstalación no coincidía con esta carpeta.");
                        }
                        try { root.DeleteSubKeyTree(subKey); }
                        catch (ArgumentException) { }
                    }
                }
                return null;
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not remove the uninstall registry entry for '" + target + "'.", ex);
                return InstallerText.T("No se pudo quitar la entrada de desinstalación: ") + ex.Message;
            }
        }

        private static List<string> RemoveMo2UninstallEntriesForTarget(string target)
        {
            List<string> notes = new List<string>();
            string normalizedTarget;
            try { normalizedTarget = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar); }
            catch { return notes; }
#if INSTALLER_TEST
            string[] uninstallRootPaths = new string[] {
                "Software\\MO2 Revamped Installer Test\\Uninstall",
                "Software\\MO2 Revamped Installer Test\\LegacyUninstall",
                "Software\\MO2 Fork Installer Test\\Uninstall",
                "Software\\MO2 Fork Installer Test\\LegacyUninstall"
            };
            RegistryHive[] hives = new RegistryHive[] { RegistryHive.CurrentUser };
            RegistryView[] views = new RegistryView[] { RegistryView.Registry64 };
#else
            string[] uninstallRootPaths = new string[] { "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" };
            RegistryHive[] hives = new RegistryHive[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine };
            RegistryView[] views = new RegistryView[] { RegistryView.Registry64, RegistryView.Registry32 };
#endif
            foreach (RegistryHive hive in hives)
            {
                foreach (RegistryView view in views)
                {
                    foreach (string uninstallRootPath in uninstallRootPaths)
                    {
                        try
                        {
                            using (RegistryKey baseKey = RegistryKey.OpenBaseKey(hive, view))
                            using (RegistryKey uninstallRoot = baseKey.OpenSubKey(uninstallRootPath, true))
                            {
                                if (uninstallRoot == null) continue;
                                foreach (string subKeyName in uninstallRoot.GetSubKeyNames())
                                {
                                    bool matches = false;
                                    using (RegistryKey entry = uninstallRoot.OpenSubKey(subKeyName, false))
                                    {
                                        if (entry == null) continue;
                                        string displayName = Convert.ToString(entry.GetValue("DisplayName")) ?? "";
                                        string installLocation = Convert.ToString(entry.GetValue("InstallLocation")) ?? "";
                                        string uninstallCommand = Convert.ToString(entry.GetValue("UninstallString")) ?? "";
                                        bool knownMo2Name = displayName.IndexOf("Mod Organizer", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                            displayName.IndexOf("MO2 Revamped", StringComparison.OrdinalIgnoreCase) >= 0 ||
                                            displayName.IndexOf("MO2 Fork", StringComparison.OrdinalIgnoreCase) >= 0;
                                        matches = knownMo2Name &&
                                            (PathsEqual(installLocation, normalizedTarget) || UninstallCommandTargetsDirectory(uninstallCommand, normalizedTarget));
                                    }
                                    if (!matches) continue;
                                    try { uninstallRoot.DeleteSubKeyTree(subKeyName); }
                                    catch (Exception ex)
                                    {
                                        SetupDiagnostics.Error("Could not remove MO2 uninstall registry entry '" + subKeyName + "'.", ex);
                                        notes.Add(InstallerText.T("entrada de MO2 ") + subKeyName + " (" + ex.Message + ")");
                                    }
                                }
                            }
                        }
                        catch
                        {
                            // Registry views can be unavailable on older Windows builds.
                        }
                    }
                }
            }
            return notes;
        }

        private static bool UninstallCommandTargetsDirectory(string command, string target)
        {
            if (String.IsNullOrWhiteSpace(command)) return false;
            string trimmed = command.Trim();
            string executable;
            if (trimmed.StartsWith("\"", StringComparison.Ordinal))
            {
                int endQuote = trimmed.IndexOf('"', 1);
                if (endQuote <= 1) return false;
                executable = trimmed.Substring(1, endQuote - 1);
            }
            else
            {
                int firstSpace = trimmed.IndexOf(' ');
                executable = firstSpace < 0 ? trimmed : trimmed.Substring(0, firstSpace);
            }
            try
            {
                string fullExecutable = Path.GetFullPath(executable);
                return PathsEqual(Path.GetDirectoryName(fullExecutable), target);
            }
            catch { return false; }
        }

        private static string SafeCombine(string root, string relative)
        {
            if (!IsSafeRelativePath(relative)) throw new InvalidOperationException(InstallerText.T("Ruta de archivo no válida: ") + relative);
            string fullRoot = Path.GetFullPath(root).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            string full = Path.GetFullPath(Path.Combine(fullRoot, relative.Replace('/', '\\')));
            if (!full.StartsWith(fullRoot, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(InstallerText.T("La ruta de archivo sale de su carpeta: ") + relative);
            // a junction or link inside the folder would carry the write or the
            // deletion somewhere else
            for (string directory = Path.GetDirectoryName(full);
                 directory != null && directory.Length >= fullRoot.Length;
                 directory = Path.GetDirectoryName(directory))
            {
                if (Directory.Exists(directory) && (File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0)
                    throw new InvalidOperationException(InstallerText.T("La ruta de archivo sale de su carpeta: ") + relative);
            }
            return full;
        }

        private static bool IsSafeRelativePath(string path)
        {
            if (String.IsNullOrWhiteSpace(path) || Path.IsPathRooted(path)) return false;
            string normalized = path.Replace('/', '\\');
            string[] parts = normalized.Split('\\');
            foreach (string part in parts)
                if (part.Length == 0 || part == "." || part == ".." || part.IndexOf(':') >= 0) return false;
            return true;
        }

        private static string HashFile(string path)
        {
            using (FileStream stream = File.OpenRead(path))
            using (SHA256 sha = SHA256.Create()) return BytesToHex(sha.ComputeHash(stream));
        }

        private static string HashBytes(byte[] bytes)
        {
            using (SHA256 sha = SHA256.Create()) return BytesToHex(sha.ComputeHash(bytes));
        }

        private static string BytesToHex(byte[] bytes)
        {
            StringBuilder output = new StringBuilder(bytes.Length * 2);
            for (int i = 0; i < bytes.Length; i++) output.Append(bytes[i].ToString("X2"));
            return output.ToString();
        }

        private static bool HashEquals(string a, string b)
        {
            return !String.IsNullOrEmpty(a) && !String.IsNullOrEmpty(b) && String.Equals(a, b, StringComparison.OrdinalIgnoreCase);
        }

        private static bool PathsEqual(string a, string b)
        {
            if (String.IsNullOrEmpty(a) || String.IsNullOrEmpty(b)) return false;
            try
            {
                return String.Equals(Path.GetFullPath(a).TrimEnd('\\'), Path.GetFullPath(b).TrimEnd('\\'), StringComparison.OrdinalIgnoreCase);
            }
            catch { return false; }
        }

        private static string FormatBytes(long value)
        {
            return String.Format("{0:0.0} GB", value / 1073741824.0);
        }

        private static void Report(BackgroundWorker worker, int percent, string message)
        {
            SetupDiagnostics.Progress(percent, message);
            if (worker != null && worker.WorkerReportsProgress)
                worker.ReportProgress(Math.Max(0, Math.Min(100, percent)), message);
        }

        private sealed class CopyEntry
        {
            public string SourcePath;
            public string RelativePath;
            public byte[] Data;
        }

        private sealed class PayloadBundle : IDisposable
        {
            public readonly Dictionary<string, ZipArchiveEntry> Entries;
            private readonly Stream Source;
            private readonly ZipArchive Archive;

            public PayloadBundle(Stream source, ZipArchive archive, Dictionary<string, ZipArchiveEntry> entries)
            {
                Source = source;
                Archive = archive;
                Entries = entries;
            }

            public void Dispose()
            {
                Archive.Dispose();
                Source.Dispose();
            }
        }
    }
}
