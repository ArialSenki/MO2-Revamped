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
        // the highest folder the first installation created (empty when the
        // folder already existed): an uninstall removes empty folders up to
        // it, never above. Kept across updates.
        public string CreatedDirectory { get; set; }
        // whether the folder was empty (or new) when MO2 was first installed
        public bool TargetWasEmpty { get; set; }
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

    // filled from the window's JSON request (JavaScriptSerializer) or the
    // silent switches; some fields only ever come from the JSON
#pragma warning disable 0649
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
        // HKCU\Software\Classes\nxm opens Nexus links with this nxmhandler.exe
        public bool RegisterNxmHandler;
        // the live handler of another program the user saw and chose to
        // replace; any other live handler is left as it is
        public string NxmHandlerToReplace;
    }
#pragma warning restore 0649

    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            // what this process and its helpers start must never hold the
            // folder being removed (a double-clicked unins000.exe starts in
            // it): Windows can't delete a folder that is a current directory
            InstallerBackend.LeaveStartDirectory();
            if (InstallerBackend.TryHandleCommandLine(args)) return;
            SetupDiagnostics.Initialize(args);
            if (InstallerBackend.TryRunSilent(args)) return;
            try
            {
                if (InstallerBackend.TryLaunchQtInstaller(args)) return;
                // every release embeds the Qt window (Build-Release.ps1 stops
                // without it); a file without it is damaged or incomplete
                throw new InvalidOperationException(InstallerText.T("Falta la ventana del instalador dentro de este archivo. Descarga de nuevo el instalador de MO2 Revamped."));
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
        private static string setupVersion;

        // the release this setup belongs to (AssemblyInformationalVersion,
        // which Build-Release.ps1 checks against its own version)
        public static string SetupVersion
        {
            get
            {
                if (setupVersion == null)
                {
                    string version = "";
                    try
                    {
                        object[] attributes = Assembly.GetExecutingAssembly().GetCustomAttributes(typeof(AssemblyInformationalVersionAttribute), false);
                        if (attributes.Length > 0) version = ((AssemblyInformationalVersionAttribute)attributes[0]).InformationalVersion ?? "";
                    }
                    catch { }
                    setupVersion = version;
                }
                return setupVersion;
            }
        }

        public static string CurrentPath
        {
            get { lock (Sync) return CurrentLog ?? ""; }
        }

        public static void Initialize(string[] args)
        {
            try
            {
                string root = Path.Combine(Path.GetTempPath(), "MO2 Revamped", "Installer Logs");
                // the log stays where the user can read it, but an elevated run
                // must not follow a link planted there to write somewhere else
                foreach (string folder in new string[] { Path.GetDirectoryName(root), root })
                {
                    if (Directory.Exists(folder) && (File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0)
                        throw new IOException("The log folder is a link.");
                    Directory.CreateDirectory(folder);
                }
                string suffix = DateTime.UtcNow.ToString("yyyyMMdd-HHmmss") + "-" + Process.GetCurrentProcess().Id;
                lock (Sync) CurrentLog = Path.Combine(root, "setup-" + suffix + ".log");

                Info("Installer session started.");
                Info("Product: MO2 Revamped setup " + SetupVersion + "; assembly " + (Assembly.GetExecutingAssembly().GetName().Version ?? new Version(0, 0)) +
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
                "; register nxm links=" + request.RegisterNxmHandler +
                "; reinstall over kept data=" + request.ReinstallOverKeptData +
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
                throw new InvalidOperationException(InstallerText.T("No se pudo determinar la carpeta AppData local."));

            string normalizedLocalAppData = NormalizeCleanupDirectory(localAppData);
            string dataDirectory = NormalizeCleanupDirectory(Path.Combine(normalizedLocalAppData, "MO2 Revamped"));
            if (!PathsEqual(Path.GetDirectoryName(dataDirectory), normalizedLocalAppData))
                throw new InvalidOperationException(InstallerText.T("La carpeta de datos de MO2 Revamped no está dentro de AppData local."));

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

        // one setup at a time per folder: a second window (or a silent run)
        // working on the same installation would mix two operations
        public static OperationResult Execute(SetupRequest request, BackgroundWorker worker)
        {
            if (request == null || String.IsNullOrWhiteSpace(request.TargetDirectory))
                throw new InvalidOperationException(InstallerText.T("Indica una carpeta válida."));
            string name = "MO2RevampedSetup-" + TargetKey(Path.GetFullPath(request.TargetDirectory));
            System.Threading.Mutex gate = null;
            try
            {
                bool created;
                try { gate = new System.Threading.Mutex(false, "Global\\" + name, out created); }
                catch (UnauthorizedAccessException) { gate = new System.Threading.Mutex(false, "Local\\" + name, out created); }
                bool owned;
                try { owned = gate.WaitOne(0); }
                catch (System.Threading.AbandonedMutexException) { owned = true; }
                if (!owned)
                    throw new InvalidOperationException(InstallerText.T("Otro instalador de MO2 Revamped ya está trabajando en esta carpeta. Espera a que termine y vuelve a intentarlo."));
                try { return ExecuteCore(request, worker); }
                finally { gate.ReleaseMutex(); }
            }
            finally { if (gate != null) gate.Dispose(); }
        }

        private static OperationResult ExecuteCore(SetupRequest request, BackgroundWorker worker)
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
            {
                if (!request.ForceCloseMo2)
                    throw new InvalidOperationException(InstallerText.T("Cierra Mod Organizer 2 antes de continuar o marca «Forzar el cierre de MO2 en esta carpeta si sigue abierto»."));
                EnsureMo2Closed(request.TargetDirectory, true, worker);
            }
            if (request.Mode == SetupMode.Migrate && IsMo2Running(request.SourceInstallDirectory))
            {
                if (!request.ForceCloseMo2)
                    throw new InvalidOperationException(InstallerText.T("Cierra también la instalación de MO2 de origen antes de copiar sus datos o marca «Forzar el cierre del MO2 de origen si sigue abierto»."));
                EnsureMo2Closed(request.SourceInstallDirectory, true, worker);
            }

            string target = Path.GetFullPath(request.TargetDirectory);
            bool eldenRingOnly = request.Mode == SetupMode.EldenRingPortable ||
                (request.Mode == SetupMode.Update &&
                 File.Exists(Path.Combine(target, "eldenring-only.portable")));
            PayloadManifest manifest = ReadManifest(eldenRingOnly);
            ValidateManifest(manifest);
            // the highest folder this run creates: a new installation that
            // fails takes it away again, instead of leaving an empty folder
            string createdRoot = null;
            for (string missing = target; !String.IsNullOrEmpty(missing) && !Directory.Exists(missing); missing = Path.GetDirectoryName(missing))
                createdRoot = missing;
            bool targetWasEmpty = createdRoot != null || Directory.GetFileSystemEntries(target).Length == 0;
            Directory.CreateDirectory(target);
            bool newInstallation = request.Mode == SetupMode.Fresh || request.Mode == SetupMode.EldenRingPortable || request.Mode == SetupMode.Migrate;
            try
            {
                return InstallInto(request, worker, target, eldenRingOnly, manifest, createdRoot, targetWasEmpty);
            }
            catch
            {
                if (newInstallation) CleanUpFailedNewInstallation(target, createdRoot);
                throw;
            }
        }

        // a new installation that stopped: what it wrote was rolled back, so
        // its "Failed" record and the folders it created (left empty) go too
        private static void CleanUpFailedNewInstallation(string target, string createdRoot)
        {
            try
            {
                InstallState state = LoadState(target);
                bool failed = state != null && (String.Equals(state.Status, "Failed", StringComparison.OrdinalIgnoreCase) ||
                                                String.Equals(state.Status, "Preparing", StringComparison.OrdinalIgnoreCase));
                // a rollback that left a file behind keeps the record, so
                // Uninstall can still find and remove it
                bool leftovers = false;
                if (failed)
                    foreach (InstallFileState file in state.Files)
                    {
                        if (file.ExistedBefore || file.IsUserData) continue;
                        try { if (File.Exists(SafeCombine(target, file.Path))) { leftovers = true; break; } }
                        catch { leftovers = true; break; }
                    }
                if (failed && !leftovers)
                {
                    DeleteState(target);
                    CleanupInstallerDataRoot();
                }
            }
            catch (Exception ex) { SetupDiagnostics.Error("Could not remove the record of the failed installation of '" + target + "'.", ex); }
            if (String.IsNullOrEmpty(createdRoot)) return;
            try
            {
                for (string directory = target; !String.IsNullOrEmpty(directory); directory = Path.GetDirectoryName(directory))
                {
                    if (!Directory.Exists(directory) || Directory.GetFileSystemEntries(directory).Length != 0) break;
                    Directory.Delete(directory, false);
                    if (PathsEqual(directory, createdRoot)) break;
                }
            }
            catch (Exception ex) { SetupDiagnostics.Error("Could not remove the folder the failed installation created at '" + target + "'.", ex); }
        }

        private static OperationResult InstallInto(SetupRequest request, BackgroundWorker worker, string target, bool eldenRingOnly, PayloadManifest manifest,
            string createdRoot, bool targetWasEmpty)
        {
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
            // an update cut short (a power cut, the setup closed by force) left
            // the folder mixing two versions, and its backup is the only copy
            // of the version before: updating again would back up the mix and
            // prune that copy, so Restore could only bring the mix back
            if (IsUnfinishedUpdate(earlierState, target))
                throw new InvalidOperationException(InstallerText.T("Una actualización anterior de esta carpeta no terminó y la carpeta mezcla dos versiones. Usa «Restaurar copia anterior» para volver a la versión que tenías y después actualiza de nuevo. No se modificó la carpeta."));
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
                states.Add(MarkerState(target, "portable.txt", request.ReinstallOverKeptData));
            }
            // MO2 reads it at its first start (an isolated copy has its own settings file below)
            bool leavesSetupLanguage = request.Mode == SetupMode.Fresh || request.Mode == SetupMode.Migrate;
            byte[] setupLanguage = Encoding.ASCII.GetBytes(InstallerText.Language == "en" ? "en_US" : InstallerText.Language.Replace('-', '_'));
            if (leavesSetupLanguage)
            {
                states.Add(new InstallFileState { Path = "setup-language.txt", ExistedBefore = false, InstalledSha256 = HashBytes(setupLanguage), OriginalSha256 = null });
            }
            if (request.Mode == SetupMode.EldenRingPortable)
            {
                states.Add(MarkerState(target, "eldenring-only.portable", request.ReinstallOverKeptData));
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
                PreviousDisplayVersion = request.Mode == SetupMode.Update ? RegisteredDisplayVersion(target) : null,
                // an update keeps what the first installation found; a record
                // from before these fields knows nothing, so nothing above the
                // folder itself is ever removed for it
                CreatedDirectory = request.Mode == SetupMode.Update ? (earlierState == null ? null : earlierState.CreatedDirectory) : createdRoot,
                TargetWasEmpty = request.Mode == SetupMode.Update ? (earlierState != null && earlierState.TargetWasEmpty) :
                    targetWasEmpty && !request.ReinstallOverKeptData
            };
            string uninstallWarning = null;
            string shortcutWarning = null;

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
                // first what makes the folder a recognised installation (its
                // markers, MO2-Revamped.install, ModOrganizer.exe): a setup cut
                // short then leaves a folder Uninstall can clear by its record,
                // not one no mode accepts
                if ((request.Mode == SetupMode.Fresh || request.Mode == SetupMode.EldenRingPortable) && request.InstanceKind == InstanceKind.Portable ||
                    request.Mode == SetupMode.Migrate && request.InstanceKind == InstanceKind.Portable)
                {
                    string marker = Path.Combine(target, "portable.txt");
                    if (!File.Exists(marker)) File.WriteAllBytes(marker, new byte[0]);
                }
                if (request.Mode == SetupMode.EldenRingPortable && !File.Exists(Path.Combine(target, "eldenring-only.portable")))
                    File.WriteAllBytes(Path.Combine(target, "eldenring-only.portable"), new byte[0]);
                int index = 0;
                foreach (PayloadFile file in manifest.Files.OrderBy(entry => InstallOrder(entry.Path)))
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

                if (leavesSetupLanguage)
                    File.WriteAllBytes(Path.Combine(target, "setup-language.txt"), setupLanguage);
                if (request.Mode == SetupMode.EldenRingPortable)
                {
                    // A fresh isolated copy starts in the setup language; an existing choice is kept.
                    string settings = Path.Combine(target, "EldenRingOnly.ini");
                    if (!File.Exists(settings))
                        File.WriteAllText(settings, "[General]\r\nLanguage=" + InstallerText.Language.Replace('-', '_') + "\r\n", new UTF8Encoding(false));
                }

                List<string> failedShortcuts = CreateShortcuts(request, state);
                if (failedShortcuts.Count > 0)
                {
                    // both are called "MO2 Revamped": the place tells them apart
                    string startMenu = Environment.GetFolderPath(Environment.SpecialFolder.StartMenu);
                    List<string> labels = new List<string>();
                    foreach (string link in failedShortcuts)
                        labels.Add(link.StartsWith(startMenu, StringComparison.OrdinalIgnoreCase)
                            ? InstallerText.T("Acceso en menú Inicio") : InstallerText.T("Acceso en escritorio"));
                    shortcutWarning = String.Format(InstallerText.T("No se pudo crear el acceso directo ({0}); MO2 está instalado y se abre desde {1}."),
                        String.Join(", ", labels.ToArray()), Path.Combine(target, "ModOrganizer.exe"));
                }
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

            try { RegisterUninstallEntry(target, manifest.Version, manifest.TotalBytes, eldenRingOnly); }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Windows could not register the uninstaller entry for '" + target + "'.", ex);
                uninstallWarning = InstallerText.T("Windows no pudo registrar la desinstalación: ") + ex.Message;
            }

            // Nexus "Mod manager download" links, as nxmhandler.exe registers
            // itself; a failure leaves MO2 working (it registers again when
            // it is asked to)
            string nxmWarning = null;
            // a working handler of another program (another MO2, Vortex) is
            // only replaced when the user saw that one and still chose this
            string currentHandler = RegisteredNxmHandler();
            bool otherLiveHandler = false;
            try
            {
                otherLiveHandler = currentHandler.Length > 0 && File.Exists(currentHandler) &&
                    !IsPathWithinDirectory(currentHandler, target);
            }
            catch { }
            if (request.RegisterNxmHandler && otherLiveHandler && !PathsEqual(currentHandler, request.NxmHandlerToReplace))
            {
                SetupDiagnostics.Info("nxm:// links stay with '" + currentHandler + "': the request did not name it as the one to replace.");
                nxmWarning = InstallerText.T("Los enlaces de Nexus (nxm://) siguen abriéndose con ") + currentHandler +
                    InstallerText.T("; no se cambió porque no se eligió reemplazarlo.");
            }
            else if (request.RegisterNxmHandler)
            {
                try { RegisterNxmHandler(target); }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not register nxmhandler.exe for nxm:// links.", ex);
                    nxmWarning = InstallerText.T("No se pudieron asociar los enlaces de Nexus (nxm://) a este MO2: ") + ex.Message;
                }
            }

            // the Microsoft Visual C++ runtime MO2 and usvfs need (see VcRuntime);
            // the Elden Ring edition only runs 64-bit games
            bool vcRestartNeeded;
            string vcWarning = VcRuntime.Ensure(!eldenRingOnly,
                delegate(string text) { Report(worker, 100, text); }, out vcRestartNeeded);

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
            if (!String.IsNullOrEmpty(shortcutWarning))
                details.Append(Environment.NewLine).Append(shortcutWarning);
            if (!String.IsNullOrEmpty(uninstallWarning))
                details.Append(Environment.NewLine).Append(uninstallWarning).Append(InstallerText.T(" El asistente de instalación todavía permite restaurar esta operación."));
            if (!String.IsNullOrEmpty(vcWarning))
                details.Append(Environment.NewLine).Append(vcWarning);
            else if (vcRestartNeeded)
                details.Append(Environment.NewLine).Append(InstallerText.T("Windows pide reiniciar para terminar de instalar Microsoft Visual C++."));
            if (!String.IsNullOrEmpty(nxmWarning))
                details.Append(Environment.NewLine).Append(nxmWarning);
            string otherAccount = OtherAccountWarning();
            if (!String.IsNullOrEmpty(otherAccount))
                details.Append(Environment.NewLine).Append(otherAccount);
            return new OperationResult
            {
                // MO2 is installed and works; only the Windows entry is missing, so
                // the window must not offer to retry (the folder is no longer empty)
                Success = true,
                Message = !String.IsNullOrEmpty(uninstallWarning) ? "La instalación terminó, pero sin entrada de desinstalación." :
                    !String.IsNullOrEmpty(vcWarning) ? "La instalación terminó, pero falta Microsoft Visual C++." :
                    !String.IsNullOrEmpty(shortcutWarning) ? "La instalación terminó, pero sin acceso directo." :
                    request.Mode == SetupMode.Update ? "Se actualizaron los archivos del programa." :
                    request.Mode == SetupMode.Migrate ? "La instalación nueva recibió los datos seleccionados." : "La instalación nueva está lista.",
                Details = details.ToString(),
                // without the runtime MO2 would only fail with a missing DLL
                LaunchPath = request.LaunchWhenDone && String.IsNullOrEmpty(vcWarning) ? Path.Combine(target, "ModOrganizer.exe") : null
            };
            }
            finally { payload.Dispose(); }
        }

        // the program's marker and executable go first (see Execute); the
        // rest keeps the manifest's order
        private static int InstallOrder(string path)
        {
            if (String.Equals(path, "MO2-Revamped.install", StringComparison.OrdinalIgnoreCase)) return 0;
            if (String.Equals(path, "ModOrganizer.exe", StringComparison.OrdinalIgnoreCase)) return 1;
            return 2;
        }

        // a portable marker; one a reinstall over kept data finds stays the
        // data's, so undoing a failed reinstall leaves it in place
        private static InstallFileState MarkerState(string target, string name, bool keptData)
        {
            string path = Path.Combine(target, name);
            bool existed = keptData && File.Exists(path);
            return new InstallFileState
            {
                Path = name,
                ExistedBefore = existed,
                InstalledSha256 = HashBytes(new byte[0]),
                OriginalSha256 = existed ? HashFile(path) : null,
                IsPortableMarker = true
            };
        }

        // the record of an update, or of the restore of one, that stopped
        // before its end, with the backup Restore needs still in place (a run
        // that rolled back cleanly put the earlier record back; a restore that
        // ends, with or without pending files, changes the status)
        private static bool IsUnfinishedUpdate(InstallState state, string target)
        {
            if (state == null || state.Files == null || String.IsNullOrWhiteSpace(state.BackupDirectory)) return false;
            if (!new string[] { "Preparing", "Installing", "RollingBack", "Failed", "Restoring" }.Contains(state.Status ?? "", StringComparer.OrdinalIgnoreCase))
                return false;
            if (!IsBackupPathForTarget(state.BackupDirectory, target) || !Directory.Exists(state.BackupDirectory)) return false;
            foreach (InstallFileState file in state.Files)
            {
                if (!file.ExistedBefore || file.IsUserData) continue;
                try
                {
                    if (File.Exists(SafeCombine(state.BackupDirectory, file.Path))) return true;
                }
                catch { }
            }
            return false;
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
            bool forceCloseMo2 = request.ForceCloseMo2;
            // decided now: once the program's files are gone the folder is
            // no longer recognised
            bool standardInstall = !restoreMode && UsesRevampedGlobalData(target);
            EnsureInstallationFree(target, forceCloseMo2, worker);
            if (!restoreMode) EnsureRevampedGlobalDataNotInUse(request, target);
            // what instances keep inside this folder: never touched unless
            // the user asked for that data to go
            List<string> protectedPaths = restoreMode ? new List<string>()
                : ProtectedInstancePaths(target, !removeAllContents && !request.RemoveRevampedAppData, !request.RemoveRevampedAppData);

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
            if (restoreMode)
            {
                // a restore cut short leaves the folder mixing both versions:
                // until it ends, an update must not take this backup's place
                state.Status = "Restoring";
                SaveState(state);
            }
            // "everything" is the whole folder only when the setup found it
            // empty: in a folder that held other things before (or a record
            // too old to say), only MO2's program and data go
            bool wholeFolder = removeAllContents && state.TargetWasEmpty;
            List<string> notes = RestoreFiles(filesState, true, !removeAllContents,
                wholeFolder, !restoreMode, worker, out deferredCleanup);
            if (!restoreMode && !wholeFolder)
                notes.AddRange(RemoveLegacyUninstallerFiles(target));
            if (wholeFolder)
            {
                notes.AddRange(RemoveAllRemainingContents(target, protectedPaths));
            }
            else if (!restoreMode)
            {
                if (removeAllContents) notes.AddRange(RemoveInstanceData(target, protectedPaths));
                notes.AddRange(RemoveRuntimeLeftovers(target, protectedPaths));
                RemoveEmptyDirectories(target, protectedPaths);
            }
            List<string> linkNotes = restoreMode ? new List<string>() : RemoveDeadLinks(target);
            bool keptUninstallEntry = false;
            // A kept file leaves a folder without MO2, which no setup can uninstall
            // again: Windows shouldn't keep offering an uninstaller that is gone.
            bool programGone = !restoreMode && !File.Exists(Path.Combine(Path.GetFullPath(target), "ModOrganizer.exe"));
            if (notes.Count == 0 || programGone)
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
                    if (restoreMode)
                    {
                        // the version brought back is still installed: its record
                        // lets a later uninstall or update find its files
                        SaveState(RecordOfRestoredVersion(state));
                    }
                    else
                    {
                        DeleteState(target);
                        CleanupInstallerDataRoot();
                    }
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
                // "my instances" of a standard installation include the
                // portable and isolated ones MO2 created inside its folder
                if (notes.Count == 0 && standardInstall) notes.AddRange(RemoveInstanceRootsInside(target));
            }
            // the folder itself once nothing is left in it, and the empty
            // folders the first installation created above it; a running
            // uninstaller inside it leaves that to the helper that waits for it
            if (!restoreMode && notes.Count == 0 && !deferredCleanup)
                RemoveEmptyInstallFolder(target, state.CreatedDirectory);

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
                message = notes.Count == 0 ? "MO2 Revamped se desinstaló." : "La desinstalación terminó con archivos conservados o pendientes.";
                details = notes.Count == 0
                    ? removeAllContents && !wholeFolder
                        ? InstallerText.T("Se eliminaron MO2 y sus datos: perfiles, mods, descargas, overwrite y ajustes. La carpeta no estaba vacía cuando se instaló MO2 (o el registro es antiguo), así que lo que no es de MO2 se conservó.")
                    : removeAllContents
                        ? InstallerText.T("Se eliminaron MO2, sus perfiles, mods, descargas, ajustes y el resto del contenido de la carpeta.") +
                          (!String.IsNullOrWhiteSpace(state.BackupDirectory) ? InstallerText.T(" También se eliminó la copia de la versión anterior.") : "") +
                          InstallerText.T(" Las carpetas que creó la instalación se quitan si quedan vacías.")
                        : standardInstall
                            ? InstallerText.T("Se retiraron los archivos de MO2 Revamped y lo que MO2 creó en su carpeta. Tus instancias, mods, descargas y ajustes de AppData\\Local\\MO2 Revamped se conservaron.")
                            : InstallerText.T("Se retiraron los archivos de MO2 Revamped, se restauró la instalación previa cuando correspondía y se conservaron los datos personales.")
                    : pendingDetails;
                if (notes.Count == 0) details += KeptEntriesDetails(target, deferredCleanup);
            }
            if (!restoreMode && notes.Count == 0 && request.RemoveRevampedAppData)
                details += Environment.NewLine + String.Format(
                    InstallerText.T("Se completó la limpieza de datos globales de MO2 Revamped en:\n{0}"),
                    RevampedAppDataDirectory());
            if (linkNotes.Count > 0)
                details += Environment.NewLine + InstallerText.T("No se pudieron quitar estos accesos o asociaciones: ") + String.Join(", ", linkNotes.ToArray());
            if (deferredCleanup) details += Environment.NewLine + InstallerText.T("El desinstalador abierto se retirará al cerrar esta ventana; si Windows mantiene un bloqueo, se completará al reiniciar.");
            return new OperationResult { Success = notes.Count == 0, Message = message, Details = details, LaunchPath = null };
        }

        // after a clean restore: the earlier version's program files are the
        // ones the update had found (and its backup brought back), so they are
        // recorded as installed by MO2 Revamped with the content they have now
        private static InstallState RecordOfRestoredVersion(InstallState update)
        {
            List<InstallFileState> files = new List<InstallFileState>();
            foreach (InstallFileState file in update.Files)
            {
                // a file of unknown origin stays out, so no uninstall can delete it
                if (!file.ExistedBefore || !file.OwnedByRevamped || file.IsUserData || file.IsPortableMarker || String.IsNullOrEmpty(file.OriginalSha256)) continue;
                files.Add(new InstallFileState { Path = file.Path, ExistedBefore = false, InstalledSha256 = file.OriginalSha256, OwnedByRevamped = true });
            }
            List<string> shortcuts = new List<string>();
            foreach (string shortcut in update.ShortcutPaths ?? new List<string>())
                if (update.CreatedShortcutPaths == null || !update.CreatedShortcutPaths.Exists(created => PathsEqual(created, shortcut)))
                    shortcuts.Add(shortcut);
            return new InstallState
            {
                TargetDirectory = update.TargetDirectory,
                BackupDirectory = "",
                Status = "Completed",
                Version = update.PreviousDisplayVersion ?? "",
                InstalledAt = DateTime.Now.ToString("o"),
                Files = files,
                ShortcutPaths = shortcuts,
                CreatedDirectory = update.CreatedDirectory,
                TargetWasEmpty = update.TargetWasEmpty
            };
        }

        private static OperationResult UninstallUnregisteredMo2(SetupRequest request, BackgroundWorker worker)
        {
            string target = Path.GetFullPath(request.TargetDirectory);
            string executable = Path.Combine(target, "ModOrganizer.exe");
            if (!Directory.Exists(target) || !File.Exists(executable))
                throw new InvalidOperationException(InstallerText.T("Selecciona la carpeta de una instalación que contenga ModOrganizer.exe."));
            ValidateSafeUninstallTarget(target);
            bool standardInstall = UsesRevampedGlobalData(target);
            EnsureInstallationFree(target, request.ForceCloseMo2, worker);
            EnsureRevampedGlobalDataNotInUse(request, target);
            // without a record nobody knows what the folder held before MO2:
            // only MO2's known program files and, when asked, its known data
            // go; whatever else is there stays
            List<string> protectedPaths = ProtectedInstancePaths(target,
                !request.RemoveAllRemainingContents && !request.RemoveRevampedAppData, !request.RemoveRevampedAppData);

            Report(worker, 10, InstallerText.T("Quitando los archivos del programa y conservando los datos…"));
            List<string> notes = RemoveUnregisteredProgramFiles(target, worker, protectedPaths);
            if (notes.Count == 0)
            {
                if (request.RemoveAllRemainingContents)
                {
                    Report(worker, 60, InstallerText.T("Eliminando los datos de MO2 de la carpeta…"));
                    notes.AddRange(RemoveInstanceData(target, protectedPaths));
                }
                notes.AddRange(RemoveRuntimeLeftovers(target, protectedPaths));
                RemoveEmptyDirectories(target, protectedPaths);
            }

            List<string> linkNotes = RemoveDeadLinks(target);
            bool programGone = !File.Exists(executable);
            if (notes.Count == 0 || programGone)
            {
                List<string> registryNotes = RemoveMo2UninstallEntriesForTarget(target);
                notes.AddRange(registryNotes);
            }

            if (notes.Count == 0 && request.RemoveRevampedAppData)
            {
                Report(worker, 95, InstallerText.T("Eliminando los datos globales de MO2 Revamped…"));
                notes.AddRange(RemoveRevampedGlobalData());
                if (notes.Count == 0 && standardInstall) notes.AddRange(RemoveInstanceRootsInside(target));
            }

            string runningExecutable = RunningUninstaller();
            bool deferredCleanup = false;
            if (notes.Count == 0 && IsPathWithinDirectory(runningExecutable, target) && File.Exists(runningExecutable))
            {
                if (ScheduleRunningUninstaller(runningExecutable, null, target, null)) deferredCleanup = true;
                else notes.Add(Path.GetFileName(runningExecutable) + InstallerText.T(" (Windows no pudo programar su retirada)"));
            }
            // without a record, only the folder itself, never its parents
            if (notes.Count == 0 && !deferredCleanup) RemoveEmptyInstallFolder(target, null);

            string message = notes.Count == 0
                ? "MO2 Revamped se desinstaló."
                : "La desinstalación terminó con elementos pendientes.";
            string details = notes.Count == 0
                ? request.RemoveAllRemainingContents
                    ? InstallerText.T("Se eliminaron MO2 y sus datos: perfiles, mods, descargas, overwrite y ajustes. La carpeta no estaba vacía cuando se instaló MO2 (o el registro es antiguo), así que lo que no es de MO2 se conservó.")
                    : InstallerText.T("Se quitaron los componentes conocidos del programa. Los datos de usuario y los archivos que no se reconocieron se conservaron.")
                : InstallerText.T("No se completaron estos elementos: ") + String.Join(", ", notes.ToArray()) +
                  (programGone ? "" : Environment.NewLine) + (programGone ? "" : InstallerText.T("Vuelve a ejecutar la desinstalación sobre la misma carpeta para terminar la limpieza."));
            if (notes.Count == 0) details += KeptEntriesDetails(target, deferredCleanup);
            if (notes.Count == 0 && request.RemoveRevampedAppData)
                details += Environment.NewLine + String.Format(
                    InstallerText.T("Se completó la limpieza de datos globales de MO2 Revamped en:\n{0}"),
                    RevampedAppDataDirectory());
            if (linkNotes.Count > 0)
                details += Environment.NewLine + InstallerText.T("No se pudieron quitar estos accesos o asociaciones: ") + String.Join(", ", linkNotes.ToArray());
            if (deferredCleanup)
                details += Environment.NewLine + InstallerText.T("El desinstalador se retirará al cerrar esta ventana y eliminará las carpetas que queden vacías.");
            return new OperationResult { Success = notes.Count == 0, Message = message, Details = details, LaunchPath = null };
        }

        private static List<string> RemoveUnregisteredProgramFiles(string target, BackgroundWorker worker, List<string> protectedPaths)
        {
            List<string> notes = new List<string>();
            string runningExecutable = RunningUninstaller();
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
                    // an instance keeps something here (its mods folder set
                    // to a program folder, say): left alone
                    if (TouchesProtected(full, protectedPaths)) continue;
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
                            foreach (string file in FilesBelowWithoutLinks(full))
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
            string runningExecutable = RunningUninstaller();
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

        // what points Windows at programs of this folder that are gone now:
        // shortcuts (the setup's, MO2's "Mod Organizer - <instance>" and
        // "Run program" ones, pinned ones), the nxm:// association and the
        // link handler's list. A failure is returned, never thrown.
        private static List<string> RemoveDeadLinks(string target)
        {
            List<string> notes = new List<string>();
            try { notes.AddRange(RemoveDeadShortcuts(target)); }
            catch (Exception ex) { SetupDiagnostics.Error("Could not look for shortcuts to '" + target + "'.", ex); }
            try { notes.AddRange(RemoveDeadNxmRegistration(target)); }
            catch (Exception ex) { SetupDiagnostics.Error("Could not check the nxm:// association for '" + target + "'.", ex); }
            return notes;
        }

        private static List<string> RemoveDeadShortcuts(string target)
        {
            List<string> notes = new List<string>();
            List<string> roots = new List<string>();
            roots.Add(Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory));
            roots.Add(Environment.GetFolderPath(Environment.SpecialFolder.StartMenu));
            string roaming = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);
            if (!String.IsNullOrEmpty(roaming))
                roots.Add(Path.Combine(roaming, "Microsoft", "Internet Explorer", "Quick Launch", "User Pinned", "TaskBar"));
            // the shared desktop and Start menu only change with elevation
            if (IsElevatedProcess())
            {
                roots.Add(Environment.GetFolderPath(Environment.SpecialFolder.CommonDesktopDirectory));
                roots.Add(Environment.GetFolderPath(Environment.SpecialFolder.CommonStartMenu));
            }
            int removed = 0;
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
                        string linkTarget;
                        try { linkTarget = GetShortcutTarget(link); }
                        catch { continue; }
                        // a shortcut into a folder kept with its data (a
                        // portable copy's mods, say) still works: only those
                        // whose program is gone are dead
                        if (String.IsNullOrEmpty(linkTarget) || !IsPathWithinDirectory(linkTarget, target) ||
                            File.Exists(linkTarget) || Directory.Exists(linkTarget)) continue;
                        try
                        {
                            FileAttributes attributes = File.GetAttributes(link);
                            if ((attributes & FileAttributes.ReadOnly) != 0) File.SetAttributes(link, attributes & ~FileAttributes.ReadOnly);
                            File.Delete(link);
                            removed++;
                            SetupDiagnostics.Info("Removed the shortcut '" + link + "' to '" + linkTarget + "'.");
                        }
                        catch (Exception ex)
                        {
                            SetupDiagnostics.Error("Could not remove the shortcut '" + link + "'.", ex);
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
            // the setup's Start menu folder, once it holds nothing
            foreach (Environment.SpecialFolder menu in new Environment.SpecialFolder[] { Environment.SpecialFolder.StartMenu, Environment.SpecialFolder.CommonStartMenu })
            {
                try
                {
                    string root = Environment.GetFolderPath(menu);
                    if (String.IsNullOrEmpty(root)) continue;
                    string folder = Path.Combine(root, "Programs", "MO2 Revamped");
                    if (Directory.Exists(folder) && (File.GetAttributes(folder) & FileAttributes.ReparsePoint) == 0 &&
                        Directory.GetFileSystemEntries(folder).Length == 0)
                        Directory.Delete(folder, false);
                }
                catch (Exception ex) { SetupDiagnostics.Error("Could not remove the empty MO2 Revamped Start menu folder.", ex); }
            }
            if (removed > 0) SetupDiagnostics.Info("Removed " + removed + " shortcut(s) to programs of '" + target + "'.");
            return notes;
        }

        internal static bool IsElevatedProcess()
        {
            try { return new System.Security.Principal.WindowsPrincipal(System.Security.Principal.WindowsIdentity.GetCurrent()).IsInRole(System.Security.Principal.WindowsBuiltInRole.Administrator); }
            catch { return false; }
        }

        private const string NxmClassKey = "Software\\Classes\\nxm";

        // the program HKCU\Software\Classes\nxm sends Nexus links to, or ""
        internal static string RegisteredNxmHandler()
        {
            try
            {
                using (RegistryKey command = Registry.CurrentUser.OpenSubKey(NxmClassKey + "\\shell\\open\\command", false))
                    return command == null ? "" : CommandExecutable(Convert.ToString(command.GetValue("")));
            }
            catch { return ""; }
        }

        // the same keys nxmhandler.exe writes (HandlerStorage::registerProxy)
        private static void RegisterNxmHandler(string target)
        {
            string handler = Path.Combine(Path.GetFullPath(target), "nxmhandler.exe");
            if (!File.Exists(handler))
                throw new FileNotFoundException(InstallerText.T("Falta nxmhandler.exe en la instalación."), handler);
            using (RegistryKey key = Registry.CurrentUser.CreateSubKey(NxmClassKey))
            {
                if (key == null) throw new InvalidOperationException(InstallerText.T("Windows no dejó escribir la asociación de enlaces nxm://."));
                key.SetValue("", "URL:NXM Protocol", RegistryValueKind.String);
                key.SetValue("URL Protocol", "", RegistryValueKind.String);
                using (RegistryKey command = key.CreateSubKey("shell\\open\\command"))
                {
                    if (command == null) throw new InvalidOperationException(InstallerText.T("Windows no dejó escribir la asociación de enlaces nxm://."));
                    command.SetValue("", "\"" + handler + "\" \"%1\"", RegistryValueKind.String);
                }
            }
            SetupDiagnostics.Info("nxm:// links now open with '" + handler + "'.");
        }

        // the association and the handler list's entries that point into this
        // folder at a program that is gone; one that points elsewhere (another
        // MO2, Vortex) stays as it is
        private static List<string> RemoveDeadNxmRegistration(string target)
        {
            List<string> notes = new List<string>();
            string handler = RegisteredNxmHandler();
            if (!String.IsNullOrEmpty(handler) && IsPathWithinDirectory(handler, target) && !File.Exists(handler))
            {
                try
                {
                    Registry.CurrentUser.DeleteSubKeyTree(NxmClassKey, false);
                    SetupDiagnostics.Info("Removed the nxm:// association to '" + handler + "'.");
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the nxm:// association to '" + handler + "'.", ex);
                    notes.Add(InstallerText.T("asociación nxm:// (") + ex.Message + ")");
                }
            }
            string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            if (!String.IsNullOrEmpty(localAppData))
            {
                string list = Path.Combine(localAppData, "ModOrganizer", "nxmhandler.ini");
                try { RemoveDeadNxmHandlerEntries(list, target); }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not update the link handler list '" + list + "'.", ex);
                    notes.Add("nxmhandler.ini (" + ex.Message + ")");
                }
            }
            return notes;
        }

        // nxmhandler.ini, as Qt's QSettings writes it: [handlers], size=N and
        // N\games, N\executable, N\arguments. The entries that stay keep their
        // text; only their numbers and the size change.
        private static void RemoveDeadNxmHandlerEntries(string iniPath, string target)
        {
            if (!File.Exists(iniPath)) return;
            string text = File.ReadAllText(iniPath, Encoding.UTF8);
            string newline = text.Contains("\r\n") ? "\r\n" : "\n";
            string[] lines = text.Replace("\r\n", "\n").Split('\n');
            int start = -1, end = lines.Length;
            for (int i = 0; i < lines.Length; i++)
            {
                string trimmed = lines[i].Trim();
                if (!trimmed.StartsWith("[", StringComparison.Ordinal) || !trimmed.EndsWith("]", StringComparison.Ordinal)) continue;
                if (start >= 0) { end = i; break; }
                if (String.Equals(trimmed, "[handlers]", StringComparison.OrdinalIgnoreCase)) start = i;
            }
            if (start < 0) return;
            SortedDictionary<int, List<KeyValuePair<string, string>>> entries = new SortedDictionary<int, List<KeyValuePair<string, string>>>();
            List<string> other = new List<string>();
            for (int i = start + 1; i < end; i++)
            {
                string line = lines[i];
                int equals = line.IndexOf('=');
                int slash = line.IndexOf('\\');
                int index;
                if (equals > 0 && slash > 0 && slash < equals && Int32.TryParse(line.Substring(0, slash).Trim(), out index))
                {
                    List<KeyValuePair<string, string>> fields;
                    if (!entries.TryGetValue(index, out fields)) { fields = new List<KeyValuePair<string, string>>(); entries.Add(index, fields); }
                    fields.Add(new KeyValuePair<string, string>(line.Substring(slash + 1, equals - slash - 1), line.Substring(equals + 1)));
                    continue;
                }
                if (equals > 0 && String.Equals(line.Substring(0, equals).Trim(), "size", StringComparison.OrdinalIgnoreCase)) continue;
                other.Add(line);
            }
            List<List<KeyValuePair<string, string>>> kept = new List<List<KeyValuePair<string, string>>>();
            int dropped = 0;
            foreach (KeyValuePair<int, List<KeyValuePair<string, string>>> entry in entries)
            {
                string executable = null;
                foreach (KeyValuePair<string, string> field in entry.Value)
                    if (String.Equals(field.Key.Trim(), "executable", StringComparison.OrdinalIgnoreCase)) executable = DecodeQtIniValue(field.Value);
                if (!String.IsNullOrEmpty(executable) && IsPathWithinDirectory(executable, target) && !File.Exists(executable))
                {
                    dropped++;
                    continue;
                }
                kept.Add(entry.Value);
            }
            if (dropped == 0) return;
            List<string> output = new List<string>();
            for (int i = 0; i <= start; i++) output.Add(lines[i]);
            output.Add("size=" + kept.Count);
            for (int i = 0; i < kept.Count; i++)
                foreach (KeyValuePair<string, string> field in kept[i])
                    output.Add((i + 1) + "\\" + field.Key + "=" + field.Value);
            // blank lines and anything unknown keep their place at the end
            output.AddRange(other);
            for (int i = end; i < lines.Length; i++) output.Add(lines[i]);
            string temporary = iniPath + ".mo2setup.tmp";
            File.WriteAllText(temporary, String.Join(newline, output.ToArray()), new UTF8Encoding(false));
            ReplaceFile(temporary, iniPath);
            SetupDiagnostics.Info("Removed " + dropped + " entr" + (dropped == 1 ? "y" : "ies") + " for '" + target + "' from '" + iniPath + "'.");
        }

        // a value as QSettings writes it in an INI file: maybe quoted, with
        // \\, \" and \xHHHH escapes
        private static string DecodeQtIniValue(string raw)
        {
            string value = (raw ?? "").Trim();
            if (value.Length >= 2 && value[0] == '"' && value[value.Length - 1] == '"') value = value.Substring(1, value.Length - 2);
            StringBuilder result = new StringBuilder(value.Length);
            for (int i = 0; i < value.Length; i++)
            {
                char character = value[i];
                if (character != '\\' || i + 1 >= value.Length) { result.Append(character); continue; }
                char next = value[++i];
                if (next == 'x')
                {
                    int code = 0, digits = 0;
                    while (digits < 4 && i + 1 < value.Length && Uri.IsHexDigit(value[i + 1]))
                    {
                        code = code * 16 + Convert.ToInt32(value[++i].ToString(), 16);
                        digits++;
                    }
                    result.Append((char)code);
                }
                else if (next == 't') result.Append('\t');
                else if (next == 'n') result.Append('\n');
                else if (next == 'r') result.Append('\r');
                else result.Append(next);
            }
            return result.ToString();
        }

        // the executable a command line starts ("C:\x\y.exe" "%1" or C:\x\y.exe %1)
        private static string CommandExecutable(string command)
        {
            if (String.IsNullOrWhiteSpace(command)) return "";
            string trimmed = command.Trim();
            if (trimmed.StartsWith("\"", StringComparison.Ordinal))
            {
                int endQuote = trimmed.IndexOf('"', 1);
                return endQuote <= 1 ? "" : trimmed.Substring(1, endQuote - 1);
            }
            int firstSpace = trimmed.IndexOf(' ');
            return firstSpace < 0 ? trimmed : trimmed.Substring(0, firstSpace);
        }

        // instance folders MO2 keeps inside its own folder: those of a
        // portable copy, and the portable and isolated instances MO2 Revamped
        // creates beside a standard installation
        private static readonly string[] InstanceDataDirectories = new string[]
        {
            "profiles", "mods", "downloads", "overwrite", "cache",
            "PortableInstances", "IsolatedInstances", "GlobalInstances",
            "Elden Ring Shared Saves", "Elden Ring Shared Saves Backups", "Elden Ring Global Save Backups",
            "Nightreign Shared Saves", "Nightreign Shared Saves Backups", "Nightreign Global Save Backups"
        };

        // the folders of InstanceDataDirectories in this folder that hold anything
        internal static List<string> InstanceFoldersInside(string target)
        {
            List<string> found = new List<string>();
            if (String.IsNullOrWhiteSpace(target)) return found;
            foreach (string name in InstanceDataDirectories)
            {
                try
                {
                    string path = Path.Combine(target, name);
                    if (Directory.Exists(path) && HasEntries(path)) found.Add(name);
                }
                catch { }
            }
            return found;
        }

        // what an instance keeps inside this folder: its base folder, mods,
        // downloads, profiles, overwrite and cache as every ModOrganizer.ini
        // places them (the folder's own, its portable, isolated and global
        // instances, and the standard instances in AppData), and the instance
        // folders themselves. An uninstall never removes these unless the user
        // asked for that data to go (insideInstances: the instances inside the
        // folder; globalInstances: those in AppData).
        internal static List<string> ProtectedInstancePaths(string target, bool insideInstances, bool globalInstances)
        {
            List<string> found = new List<string>();
            string root;
            try { root = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar); }
            catch { return found; }
            List<string> instances = new List<string>();
            if (insideInstances)
            {
                instances.Add(root);
                foreach (string parent in new string[] { "PortableInstances", "IsolatedInstances", "GlobalInstances" })
                {
                    try
                    {
                        string folder = Path.Combine(root, parent);
                        if (!Directory.Exists(folder) || (File.GetAttributes(folder) & FileAttributes.ReparsePoint) != 0) continue;
                        foreach (string instance in Directory.GetDirectories(folder))
                        {
                            instances.Add(instance);
                            found.Add(instance);
                        }
                    }
                    catch { }
                }
            }
            if (globalInstances)
            {
                try
                {
                    string global = Path.Combine(RevampedAppDataDirectory(), "Instances");
                    if (Directory.Exists(global)) instances.AddRange(Directory.GetDirectories(global));
                }
                catch { }
            }
            foreach (string instance in instances)
            {
                if (!File.Exists(Path.Combine(instance, "ModOrganizer.ini"))) continue;
                List<string> paths = new List<string>();
                try { paths.Add(ResolveBaseDirectory(instance)); }
                catch { }
                foreach (string[] setting in new string[][] {
                    new string[] { "mod_directory", "mods" }, new string[] { "download_directory", "downloads" },
                    new string[] { "profiles_directory", "profiles" }, new string[] { "overwrite_directory", "overwrite" } })
                {
                    try { paths.Add(ResolveConfiguredDirectory(instance, setting[0], setting[1])); }
                    catch { }
                }
                // the web cache only when it was moved on purpose: in its
                // default place it is a leftover the uninstall clears
                try
                {
                    if (!String.IsNullOrEmpty(ReadIniSetting(Path.Combine(instance, "ModOrganizer.ini"), "cache_directory")))
                        paths.Add(ResolveConfiguredDirectory(instance, "cache_directory", "webcache"));
                }
                catch { }
                foreach (string path in paths)
                {
                    if (String.IsNullOrEmpty(path)) continue;
                    // the folder itself as a portable instance's base is not a
                    // reason to keep all of it: its data folders are listed one by one
                    if (PathsEqual(path, root)) continue;
                    if (IsPathWithinDirectory(path, root) && !found.Exists(known => PathsEqual(known, path))) found.Add(path);
                }
            }
            return found;
        }

        // true when the entry is a protected folder, holds one, or lies inside one
        private static bool TouchesProtected(string entry, List<string> protectedPaths)
        {
            if (protectedPaths == null) return false;
            foreach (string path in protectedPaths)
                if (PathsEqual(entry, path) || IsPathWithinDirectory(path, entry) || IsPathWithinDirectory(entry, path)) return true;
            return false;
        }

        // every file below a folder, never through a junction or link
        private static List<string> FilesBelowWithoutLinks(string directory)
        {
            List<string> files = new List<string>();
            Stack<string> pending = new Stack<string>();
            pending.Push(directory);
            while (pending.Count > 0)
            {
                DirectoryInfo current = new DirectoryInfo(pending.Pop());
                try
                {
                    if ((current.Attributes & FileAttributes.ReparsePoint) != 0) continue;
                    foreach (FileInfo file in current.GetFiles())
                        if ((file.Attributes & FileAttributes.ReparsePoint) == 0) files.Add(file.FullName);
                    foreach (DirectoryInfo child in current.GetDirectories())
                        if ((child.Attributes & FileAttributes.ReparsePoint) == 0) pending.Push(child.FullName);
                }
                catch { }
            }
            return files;
        }

        // the data MO2 keeps beside a portable or isolated copy, by name
        private static readonly string[] InstanceDataFiles = new string[]
        {
            "ModOrganizer.ini", "categories.dat", "nexuscatmap.dat", "MO2-Revamped-Global.ini", "EldenRingOnly.ini",
            "eldenring_mo2_launch_methods.json", "portable.txt", "eldenring-only.portable"
        };

        // "Remove everything" in a folder that wasn't empty before MO2 (or
        // without a record that says so): MO2's own data folders and files by
        // name, nothing else
        private static List<string> RemoveInstanceData(string target, List<string> protectedPaths)
        {
            List<string> notes = new List<string>();
            string root = Path.GetFullPath(target);
            string runningExecutable = RunningUninstaller();
            foreach (string name in InstanceDataDirectories)
            {
                string directory = Path.Combine(root, name);
                try
                {
                    if (!Directory.Exists(directory) || TouchesProtected(directory, protectedPaths)) continue;
                    if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) { Directory.Delete(directory, false); continue; }
                    DeleteDirectoryContents(directory, runningExecutable, notes, protectedPaths);
                    if (Directory.Exists(directory) && Directory.GetFileSystemEntries(directory).Length == 0) Directory.Delete(directory, false);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the data folder '" + directory + "'.", ex);
                    notes.Add(name + " (" + ex.Message + ")");
                }
            }
            foreach (string name in InstanceDataFiles)
            {
                string file = Path.Combine(root, name);
                try
                {
                    if (!File.Exists(file)) continue;
                    FileAttributes attributes = File.GetAttributes(file);
                    if ((attributes & FileAttributes.ReadOnly) != 0) File.SetAttributes(file, attributes & ~FileAttributes.ReadOnly);
                    File.Delete(file);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the data file '" + file + "'.", ex);
                    notes.Add(name + " (" + ex.Message + ")");
                }
            }
            return notes;
        }

        // the portable and isolated instances MO2 Revamped created inside a
        // standard installation, once the user chose to remove the instances
        private static List<string> RemoveInstanceRootsInside(string target)
        {
            List<string> notes = new List<string>();
            string runningExecutable = RunningUninstaller();
            foreach (string name in new string[] { "PortableInstances", "IsolatedInstances", "GlobalInstances" })
            {
                string directory = Path.Combine(Path.GetFullPath(target), name);
                try
                {
                    if (!Directory.Exists(directory)) continue;
                    if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) { Directory.Delete(directory, false); continue; }
                    DeleteDirectoryContents(directory, runningExecutable, notes);
                    if (Directory.Exists(directory) && Directory.GetFileSystemEntries(directory).Length == 0) Directory.Delete(directory, false);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the instance folder '" + directory + "'.", ex);
                    notes.Add(name + " (" + ex.Message + ")");
                }
            }
            return notes;
        }

        // what an uninstall leaves in the folder: everything that is not
        // MO2's, kept and named, and whether the folder itself went
        private static string KeptEntriesDetails(string target, bool deferredCleanup)
        {
            try
            {
                if (!Directory.Exists(target))
                    return Environment.NewLine + InstallerText.T("La carpeta de instalación se quitó.");
                string runningExecutable = RunningUninstaller();
                List<string> left = new List<string>();
                foreach (string entry in Directory.GetFileSystemEntries(target))
                    if (!PathsEqual(entry, runningExecutable)) left.Add(Path.GetFileName(entry));
                if (left.Count == 0)
                    return deferredCleanup ? "" : Environment.NewLine + InstallerText.T("La carpeta de instalación se quitó.");
                List<string> shown = left.Take(12).ToList();
                if (left.Count > shown.Count) shown.Add("…");
                return Environment.NewLine + InstallerText.T("La carpeta se conservó porque contiene cosas que no son de MO2 o datos que decidiste mantener: ") +
                    String.Join(", ", shown.ToArray());
            }
            catch { return ""; }
        }

        // the top-level entries of an installation that are neither MO2's
        // program, its record's files, what it writes while it runs nor its
        // data: an uninstall always keeps them
        internal static List<string> UnknownEntries(string target)
        {
            List<string> unknown = new List<string>();
            try
            {
                string root = Path.GetFullPath(target);
                if (!Directory.Exists(root)) return unknown;
                HashSet<string> known = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                foreach (string name in Mo2ProgramDirectories) known.Add(name);
                foreach (string name in RuntimeLeftoverDirectories) known.Add(name);
                foreach (string name in InstanceDataDirectories) known.Add(name);
                foreach (string name in InstanceDataFiles) known.Add(name);
                foreach (string name in new string[] { "nxmhandler.ini", "nxmhandler.log", "MO2-Revamped.ini", "setup-language.txt" }) known.Add(name);
                InstallState state = LoadState(root);
                if (state != null && state.Files != null)
                    foreach (InstallFileState file in state.Files)
                        if (!String.IsNullOrEmpty(file.Path)) known.Add(file.Path.Replace('/', '\\').Split('\\')[0]);
                HashSet<string> payload = KnownPayloadPaths();
                if (payload != null)
                    foreach (string path in payload) known.Add(path.Split('/')[0]);
                foreach (string entry in Directory.GetFileSystemEntries(root))
                {
                    string name = Path.GetFileName(entry);
                    string extension = Path.GetExtension(name);
                    if (known.Contains(name) || IsKnownMo2ProgramFile(name) ||
                        String.Equals(extension, ".log", StringComparison.OrdinalIgnoreCase) ||
                        String.Equals(extension, ".dmp", StringComparison.OrdinalIgnoreCase)) continue;
                    unknown.Add(name);
                }
            }
            catch { }
            return unknown;
        }

        // what MO2 writes beside itself while it runs and isn't data: logs,
        // caches, crash dumps, the link handler's settings and Python's
        private static readonly string[] RuntimeLeftoverDirectories = new string[]
        {
            "logs", "webcache", "crashDumps", "crashes", "diagnostics"
        };
        private static readonly string[] RuntimeLeftoverFiles = new string[]
        {
            "nxmhandler.ini", "nxmhandler.log", "plugins\\plugin_python\\data\\settings.json"
        };

        private static List<string> RemoveRuntimeLeftovers(string target, List<string> protectedPaths)
        {
            List<string> notes = new List<string>();
            string root = Path.GetFullPath(target);
            string runningExecutable = RunningUninstaller();
            foreach (string name in RuntimeLeftoverDirectories)
            {
                try
                {
                    string directory = Path.Combine(root, name);
                    if (!Directory.Exists(directory) || TouchesProtected(directory, protectedPaths)) continue;
                    if ((File.GetAttributes(directory) & FileAttributes.ReparsePoint) != 0) { Directory.Delete(directory, false); continue; }
                    DeleteDirectoryContents(directory, runningExecutable, notes, protectedPaths);
                    if (Directory.Exists(directory) && Directory.GetFileSystemEntries(directory).Length == 0) Directory.Delete(directory, false);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the leftover folder '" + name + "'.", ex);
                    notes.Add(name + " (" + ex.Message + ")");
                }
            }
            List<string> files = new List<string>(RuntimeLeftoverFiles);
            // the crash dumps and logs MO2 leaves at the top
            try
            {
                foreach (string file in Directory.GetFiles(root))
                {
                    string extension = Path.GetExtension(file);
                    if (String.Equals(extension, ".log", StringComparison.OrdinalIgnoreCase) ||
                        String.Equals(extension, ".dmp", StringComparison.OrdinalIgnoreCase))
                        files.Add(Path.GetFileName(file));
                }
            }
            catch { }
            foreach (string relative in files)
            {
                try
                {
                    string path = SafeCombine(root, relative);
                    if (!File.Exists(path) || TouchesProtected(path, protectedPaths)) continue;
                    FileAttributes attributes = File.GetAttributes(path);
                    if ((attributes & FileAttributes.ReadOnly) != 0) File.SetAttributes(path, attributes & ~FileAttributes.ReadOnly);
                    File.Delete(path);
                    RemoveEmptyParents(Path.GetDirectoryName(path), root);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not remove the leftover file '" + relative + "'.", ex);
                    notes.Add(relative + " (" + ex.Message + ")");
                }
            }
            return notes;
        }

        // the installation folder once it is empty, and the empty folders
        // above it up to the one the first installation created (stopAt;
        // none without a record that says so). An antivirus scan or an
        // Explorer window can hold it a moment, so it is tried for a few seconds.
        private static void RemoveEmptyInstallFolder(string target, string stopAt)
        {
            for (int attempt = 0; attempt < 12; attempt++)
            {
                RemoveEmptyInstallDirectoryAndParent(target, stopAt);
                try
                {
                    if (!Directory.Exists(target) || Directory.GetFileSystemEntries(target).Length != 0) return;
                }
                catch { return; }
                System.Threading.Thread.Sleep(250);
            }
            SetupDiagnostics.Info("The empty installation folder '" + target + "' could not be removed; another program still holds it.");
        }

        // a silent uninstall run from a copy: the unins000.exe inside the
        // installation that started it, still running until the copy ends
        private static string heldUninstaller;
        private static int heldUninstallerProcessId;

        internal static void HoldUninstaller(string path, int processId)
        {
            heldUninstaller = path;
            heldUninstallerProcessId = processId;
        }

        // the uninstaller whose file can't be deleted yet: this process, or
        // the one that started it
        private static string RunningUninstaller()
        {
            return Path.GetFullPath(String.IsNullOrEmpty(heldUninstaller) ? Application.ExecutablePath : heldUninstaller);
        }

        private static int RunningUninstallerProcessId()
        {
            return String.IsNullOrEmpty(heldUninstaller) ? Process.GetCurrentProcess().Id : heldUninstallerProcessId;
        }

        // folders the instances' ModOrganizer.ini files place outside the
        // folders an uninstall removes: never touched, so the window can say so
        internal static List<string> ExternalDataFolders(string target, bool includeGlobalData)
        {
            List<string> roots = new List<string>();
            List<string> instances = new List<string>();
            try
            {
                if (!String.IsNullOrWhiteSpace(target) && Directory.Exists(target))
                {
                    string root = Path.GetFullPath(target);
                    roots.Add(root);
                    instances.Add(root);
                    foreach (string parent in new string[] { "PortableInstances", "IsolatedInstances", "GlobalInstances" })
                    {
                        string folder = Path.Combine(root, parent);
                        if (Directory.Exists(folder)) instances.AddRange(Directory.GetDirectories(folder));
                    }
                }
                if (includeGlobalData)
                {
                    string data = RevampedAppDataDirectory();
                    roots.Add(data);
                    string global = Path.Combine(data, "Instances");
                    if (Directory.Exists(global)) instances.AddRange(Directory.GetDirectories(global));
                }
            }
            catch { }
            List<string> outside = new List<string>();
            foreach (string instance in instances)
            {
                if (!File.Exists(Path.Combine(instance, "ModOrganizer.ini"))) continue;
                foreach (string[] setting in new string[][] {
                    new string[] { "mod_directory", "mods" }, new string[] { "download_directory", "downloads" },
                    new string[] { "profiles_directory", "profiles" }, new string[] { "overwrite_directory", "overwrite" } })
                {
                    try
                    {
                        string path = ResolveConfiguredDirectory(instance, setting[0], setting[1]);
                        if (String.IsNullOrEmpty(path) || !Directory.Exists(path)) continue;
                        bool inside = roots.Exists(root => PathsEqual(path, root) || IsPathWithinDirectory(path, root));
                        if (!inside && !outside.Exists(known => PathsEqual(known, path))) outside.Add(path);
                    }
                    catch { }
                }
            }
            return outside;
        }

        // the user signed in to this session when the setup runs as another
        // account (an administrator's credentials at the elevation prompt)
        internal static string OtherAccountWarning()
        {
            string sessionUser = SessionUserName();
            if (String.IsNullOrEmpty(sessionUser) || !RunsAsAnotherAccount()) return null;
            return String.Format(InstallerText.T("El instalador se ejecuta con la cuenta {0}, no con la tuya ({1}): los accesos directos, la asociación de enlaces nxm:// y el registro del instalador van a esa cuenta. Para tenerlos en la tuya, inicia sesión con una cuenta de administrador y ejecuta el instalador desde ella."),
                Environment.UserName, sessionUser);
        }

        private static List<string> RestoreFiles(InstallState state, bool report, bool preserveUserData,
            bool removeTargetFolderAfterSelfCleanup, bool uninstallMode, BackgroundWorker worker, out bool deferredCleanup)
        {
            List<string> notes = new List<string>();
            deferredCleanup = false;
            string target = Path.GetFullPath(state.TargetDirectory);
            string runningExecutable = RunningUninstaller();
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
                            if (File.Exists(backup) && (String.IsNullOrEmpty(file.OriginalSha256) || HashEquals(HashFile(backup), file.OriginalSha256)))
                                restoreRunningUninstaller = backup;
                            break;
                        }
                        catch { }
                    }
                }
                if (ScheduleRunningUninstaller(runningExecutable, restoreRunningUninstaller, target, state.CreatedDirectory))
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
                    // the copy a cut-short run was writing (WriteZipEntry,
                    // CopyFileAtomically): the setup's own, never the user's
                    string unfinishedCopy = destination + ".mo2setup.tmp";
                    try { if (File.Exists(unfinishedCopy)) File.Delete(unfinishedCopy); }
                    catch (Exception leftoverError) { SetupDiagnostics.Error("Could not remove '" + unfinishedCopy + "'.", leftoverError); }
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
                        // the copy lives in the user's AppData: what goes back
                        // is what the update saved, or nothing
                        if (!String.IsNullOrEmpty(file.OriginalSha256) && !HashEquals(HashFile(backup), file.OriginalSha256))
                        {
                            notes.Add(file.Path + InstallerText.T(" (la copia no coincide con el original)"));
                            continue;
                        }
                        if (PathsEqual(RunningUninstaller(), destination))
                        {
                            if (ScheduleRunningUninstaller(destination, backup, null, null))
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
                        if (PathsEqual(RunningUninstaller(), destination))
                        {
                            if (ScheduleRunningUninstaller(destination, null, target, state.CreatedDirectory))
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

        // a folder an uninstall left with its data, which a new installation
        // goes back into (the same test the installation itself makes)
        internal static bool HasKeptData(string target)
        {
            try
            {
                return !String.IsNullOrWhiteSpace(target) && Directory.Exists(target) &&
                    !File.Exists(Path.Combine(target, "ModOrganizer.exe")) && HasPortableUserData(target);
            }
            catch { return false; }
        }

        private static bool HasPortableUserData(string target)
        {
            string[] files = new string[] { "ModOrganizer.ini", "categories.dat", "nexuscatmap.dat" };
            foreach (string name in files)
                if (File.Exists(Path.Combine(target, name))) return true;
            // the Elden Ring / Nightreign plugins keep shared saves and save
            // backups here: they can be a save's only copy
            // with the instance folders a standard installation's uninstall
            // keeps (see InstanceDataDirectories): the same folder can be
            // installed to again
            foreach (string name in InstanceDataDirectories)
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

        private static bool ScheduleRunningUninstaller(string destination, string backup, string emptyDirectory, string stopAt)
        {
            const int MoveFileReplaceExisting = 0x1;
            const int MoveFileDelayUntilReboot = 0x4;
            string restoreCopy = null;
            try
            {
                if (!String.IsNullOrEmpty(backup))
                {
                    restoreCopy = Path.Combine(InstallerBackend.CreateWorkFolder(), "MO2RevampedUninstallerRestore.exe");
                    File.Copy(backup, restoreCopy, true);
                }
                // the helper deletes the file once this process exits and queues
                // the reboot deletion only if that fails: queued here, it would
                // also delete the uninstaller of a reinstall made before rebooting
                if (StartPostExitCleanup(destination, restoreCopy, emptyDirectory, stopAt))
                    return true;
            }
            catch
            {
            }
            if (restoreCopy != null) InstallerBackend.RemoveWorkFolder(Path.GetDirectoryName(restoreCopy));
            if (String.IsNullOrEmpty(backup))
                return ScheduleDeleteOnReboot(destination, emptyDirectory, stopAt, MoveFileDelayUntilReboot);
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

        private static bool ScheduleDeleteOnReboot(string executable, string emptyDirectory, string stopAt, int delayUntilReboot)
        {
            bool executableScheduled = MoveFileEx(executable, null, delayUntilReboot);
            if (String.IsNullOrEmpty(emptyDirectory)) return executableScheduled;

            bool directoriesScheduled = true;
            // queued after what they hold, so they are empty when Windows
            // gets to them at the next start
            string queued = executable;
            foreach (string directory in GetSafeDirectoryCleanupCandidates(emptyDirectory, stopAt))
            {
                try
                {
                    // Never queue non-empty parents: they may contain unrelated user files.
                    if (!Directory.Exists(directory)) break;
                    string pendingEntry = queued;
                    if (Directory.GetFileSystemEntries(directory).Any(entry => !PathsEqual(entry, pendingEntry))) break;
                    directoriesScheduled = MoveFileEx(directory, null, delayUntilReboot) && directoriesScheduled;
                    queued = directory;
                }
                catch { break; }
            }
            return executableScheduled && directoriesScheduled;
        }

        private static bool StartPostExitCleanup(string executable, string restoreCopy, string emptyDirectory, string stopAt)
        {
            string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            if (!File.Exists(powershell)) return false;
#if INSTALLER_TEST
            string script = String.Empty;
#else
            // the uninstaller whose file it deletes: this one, or the unins000.exe waiting for its copy
            int ownerProcessId = RunningUninstallerProcessId();
            string script = "$ownerPid = " + ownerProcessId.ToString() + "; try { $owner = [System.Diagnostics.Process]::GetProcessById($ownerPid); $owner.WaitForExit(); $owner.Dispose() } catch {}; ";
#endif
            script += "$exe = '" + EscapePowerShellLiteral(executable) + "'; ";
            if (!String.IsNullOrEmpty(restoreCopy))
                script += "$src = '" + EscapePowerShellLiteral(restoreCopy) + "'; $copied = $false; for ($attempt = 0; $attempt -lt 30 -and -not $copied; $attempt++) { try { [System.IO.File]::Copy($src, $exe, $true); $copied = $true } catch { Start-Sleep -Milliseconds 500 } }; if (-not $copied) { exit 2 }; try { [System.IO.File]::Delete($src) } catch {}; " +
                    "$work = [System.IO.Path]::GetDirectoryName($src); try { [System.IO.Directory]::Delete($work, $false); [System.IO.Directory]::Delete([System.IO.Path]::GetDirectoryName($work), $false) } catch {}; ";
            else
                // the reboot queue only gets the file once deleting it failed,
                // and the folder after it (it must be empty by then)
                script += "$reboot = $false; for ($attempt = 0; $attempt -lt 30 -and [System.IO.File]::Exists($exe); $attempt++) { try { [System.IO.File]::SetAttributes($exe, [System.IO.FileAttributes]::Normal); [System.IO.File]::Delete($exe) } catch {}; if ([System.IO.File]::Exists($exe)) { Start-Sleep -Milliseconds 500 } }; " +
                    "if ([System.IO.File]::Exists($exe)) { try { Add-Type -Namespace Mo2Revamped -Name Reboot -MemberDefinition '[System.Runtime.InteropServices.DllImport(\"kernel32.dll\", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern bool MoveFileEx(string existingName, string newName, int flags);'; [void][Mo2Revamped.Reboot]::MoveFileEx($exe, [NullString]::Value, 4); $reboot = $true } catch { exit 2 } }; ";
            if (!String.IsNullOrEmpty(emptyDirectory))
            {
                foreach (string directory in GetSafeDirectoryCleanupCandidates(emptyDirectory, stopAt))
                {
                    string escapedDirectory = EscapePowerShellLiteral(directory);
                    // a folder that still holds anything but the queued file
                    // stops here: its parents hold it
                    script += "$dir = '" + escapedDirectory + "'; try { " +
                        "if (-not [System.IO.Directory]::Exists($dir)) { exit 0 }; " +
                        "$attributes = [System.IO.File]::GetAttributes($dir); " +
                        "if (($attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { exit 0 }; " +
                        "if ($reboot) { $left = @([System.IO.Directory]::GetFileSystemEntries($dir) | Where-Object { $_ -ne $exe }); if ($left.Count -ne 0) { exit 0 }; [void][Mo2Revamped.Reboot]::MoveFileEx($dir, [NullString]::Value, 4); $exe = $dir } " +
                        "else { if ([System.IO.Directory]::GetFileSystemEntries($dir).Length -ne 0) { exit 0 }; " +
                        "for ($attempt = 0; $attempt -lt 20 -and [System.IO.Directory]::Exists($dir); $attempt++) { try { [System.IO.Directory]::Delete($dir, $false) } catch { Start-Sleep -Milliseconds 500 } }; " +
                        "if ([System.IO.Directory]::Exists($dir)) { exit 0 } } } catch { exit 0 }; ";
                }
            }
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
            ProcessStartInfo start = new ProcessStartInfo
            {
                FileName = powershell,
                Arguments = "-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + encoded,
                // never the folder it is about to delete
                WorkingDirectory = Environment.SystemDirectory,
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

        // the Elden Ring / Nightreign saves an instance keeps beside its
        // profiles: the saves shared by its profiles, their backups and the
        // backups of the Steam route ("Profiles and saves" takes them along),
        // as (source folder, name in the new installation). The shared saves
        // are found as the game plugins find them (instance_shared_directory):
        // in the instance's base folder, or beside a customised profiles folder
        // where earlier builds kept them, when only that one holds anything
        private static List<KeyValuePair<string, string>> InstanceSaveFolders(string dataRoot)
        {
            string baseDirectory = ResolveBaseDirectory(dataRoot);
            string profilesParent = Path.GetDirectoryName(
                ResolveConfiguredDirectory(dataRoot, "profiles_directory", "profiles").TrimEnd(Path.DirectorySeparatorChar));
            List<KeyValuePair<string, string>> folders = new List<KeyValuePair<string, string>>();
            foreach (string game in new string[] { "Elden Ring", "Nightreign" })
            {
                string name = game + " Shared Saves";
                string shared = Path.Combine(baseDirectory, name);
                if (!String.IsNullOrEmpty(profilesParent))
                {
                    string legacy = Path.Combine(profilesParent, name);
                    if (!PathsEqual(shared, legacy) && !HasEntries(shared) && HasEntries(legacy)) shared = legacy;
                }
                folders.Add(new KeyValuePair<string, string>(shared, name));
                folders.Add(new KeyValuePair<string, string>(shared + " Backups", name + " Backups"));
                folders.Add(new KeyValuePair<string, string>(Path.Combine(baseDirectory, game + " Global Save Backups"), game + " Global Save Backups"));
            }
            return folders;
        }

        private static bool HasEntries(string directory)
        {
            try { return Directory.Exists(directory) && Directory.EnumerateFileSystemEntries(directory).Any(); }
            catch { return false; }
        }

        private static List<CopyEntry> EnumerateMigrationFiles(SetupRequest request)
        {
            List<CopyEntry> files = new List<CopyEntry>();
            string root = Path.GetFullPath(request.SourceDataDirectory);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "profiles_directory", "profiles"), "profiles", request.CopyProfiles);
            foreach (KeyValuePair<string, string> folder in InstanceSaveFolders(root))
                AddSelectedTree(files, folder.Key, folder.Value, request.CopyProfiles);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "mod_directory", "mods"), "mods", request.CopyMods);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "download_directory", "downloads"), "downloads", request.CopyDownloads);
            AddSelectedTree(files, ResolveConfiguredDirectory(root, "overwrite_directory", "overwrite"), "overwrite", request.CopyOverwrite);
            if (request.CopySettings)
            {
                string ini = Path.Combine(root, "ModOrganizer.ini");
                if (File.Exists(ini))
                    files.Add(new CopyEntry { Data = MakePortableIni(ini), RelativePath = "ModOrganizer.ini" });
                // the Elden Ring plugins' instance settings: programs started
                // outside the VFS and whether Reforged online keeps its own save
                AddSelectedFile(files, ResolveBaseDirectory(root), "eldenring_mo2_launch_methods.json");
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
                if (request.CopyProfiles && InstanceSaveFolders(root).Any(folder => TreeHasFile(folder.Key))) return true;
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
            string baseDirectory = ResolveBaseDirectory(dataRoot);
            string configured = ReadIniSetting(iniPath, key);
            if (String.IsNullOrEmpty(configured)) configured = "%BASE_DIR%/" + defaultName;
            return ResolveIniPath(configured, baseDirectory, dataRoot);
        }

        // the instance's base_directory (MO2's basePath()), the data folder when unset
        private static string ResolveBaseDirectory(string dataRoot)
        {
            string iniBase = ReadIniSetting(Path.Combine(dataRoot, "ModOrganizer.ini"), "base_directory");
            return String.IsNullOrEmpty(iniBase) ? dataRoot : ResolveIniPath(iniBase, dataRoot, dataRoot);
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
                    // a silent copy's unins000.exe waits inside the folder for it
                    int held = RunningUninstallerProcessId();
                    if (RmGetList(session, out needed, ref count, processes, ref reasons) == 0)
                        for (int i = 0; i < count; i++)
                        {
                            // the uninstaller itself runs from the installation
                            if (processes[i].Process.dwProcessId == self || processes[i].Process.dwProcessId == held) continue;
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

        // a shortcut that can't be made is a warning, not a failed install:
        // MO2 is in place and runs without it (Wine's .NET, on a Steam Deck,
        // threw on the old way of making them and the install was undone).
        // Returns the shortcuts that couldn't be made.
        private static List<string> CreateShortcuts(SetupRequest request, InstallState state)
        {
            string target = Path.Combine(request.TargetDirectory, "ModOrganizer.exe");
            List<string> shortcutPaths = GetShortcutPaths(request);
            List<string> failed = new List<string>();
            string shortcutIcon = Path.Combine(request.TargetDirectory, "MO2-Revamped.ico");
            string iconPath = File.Exists(shortcutIcon) ? shortcutIcon : target;
            foreach (string link in shortcutPaths)
            {
                try
                {
                    Directory.CreateDirectory(Path.GetDirectoryName(link));
                    ShellLink.Create(link, target, request.TargetDirectory, "MO2 Revamped", iconPath, 0);
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not create the shortcut '" + link + "'.", ex);
                    failed.Add(link);
                }
            }
            try { RemoveObsoleteHashedShortcuts(request, target, shortcutPaths); }
            catch (Exception ex) { SetupDiagnostics.Error("Could not tidy up older shortcuts.", ex); }
            return failed;
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
            return ShellLink.Target(link);
        }

        private static void EnsureMo2Closed(string target, bool forceClose, BackgroundWorker worker)
        {
            string expected = Path.GetFullPath(Path.Combine(target, "ModOrganizer.exe"));
            List<Process> matches = FindMo2Processes(expected);
            // a game started from this MO2 holds its files (usvfs) and, while it
            // runs, the Steam saves MO2 set aside: it is never closed by setup.
            // One started elsewhere (Steam, another MO2) doesn't matter here.
            if (File.Exists(expected))
            {
                foreach (string game in new string[] { "eldenring", "nightreign" })
                {
                    Process[] running = Process.GetProcessesByName(game);
                    bool fromHere = false;
                    foreach (Process process in running)
                    {
                        if (!fromHere) fromHere = GameRunsFromInstallation(process, target, matches.Count > 0);
                        process.Dispose();
                    }
                    if (fromHere)
                    {
                        foreach (Process process in matches) process.Dispose();
                        throw new InvalidOperationException(InstallerText.T("Elden Ring o Nightreign siguen abiertos desde este MO2. Ciérralos antes de continuar; al cerrar el juego, MO2 devuelve los guardados de Steam a su sitio."));
                    }
                }
            }
            if (matches.Count == 0) return;
            if (!forceClose)
            {
                foreach (Process process in matches) process.Dispose();
                throw new InvalidOperationException(InstallerText.T("MO2 sigue abierto en esta carpeta. Ciérralo o marca «Forzar el cierre de MO2 en esta carpeta si sigue abierto»."));
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

        // the game runs through this installation when it loaded one of its
        // files (usvfs_x64.dll, the Elden Ring bridge). When Windows won't
        // list its modules (anti-cheat), it counts as this installation's
        // while MO2 of this folder is open, which is what starts it
        private static bool GameRunsFromInstallation(Process game, string target, bool mo2OfTargetRunning)
        {
            try
            {
                foreach (ProcessModule module in game.Modules)
                {
                    string file;
                    try { file = module.FileName; }
                    catch { continue; }
                    if (IsPathWithinDirectory(file, target)) return true;
                }
                return false;
            }
            catch
            {
                // Windows won't say (anti-cheat): the game counts as this
                // installation's, as it always did, so its saves are never
                // swapped under it
                return true;
            }
        }

        private static void EnsureRevampedGlobalDataNotInUse(SetupRequest request, string target)
        {
            if (request == null || !request.RemoveRevampedAppData) return;
            // every standard installation shares that folder: with another
            // one still installed, its instances would go too
            List<string> others = OtherStandardInstallations(target);
            if (others.Count > 0)
                throw new InvalidOperationException(InstallerText.T("Otras instalaciones estándar de MO2 Revamped usan las mismas instancias de AppData\\Local\\MO2 Revamped, así que no se borró nada: ") +
                    String.Join(", ", others.ToArray()) + InstallerText.T(". Desinstálalas antes o desmarca esa opción."));
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
        internal static string SessionUserName()
        {
            try
            {
                const int WtsUserName = 5;
                return SessionText(WtsUserName) ?? "";
            }
            catch { return ""; }
        }

        internal static bool RunsAsAnotherAccount()
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

        private static List<string> RemoveAllRemainingContents(string target, List<string> protectedPaths)
        {
            List<string> notes = new List<string>();
            string root = Path.GetFullPath(target).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string runningExecutable = RunningUninstaller();
            DeleteDirectoryContents(root, runningExecutable, notes, protectedPaths);
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
                DeleteDirectoryContents(dataDirectory, RunningUninstaller(), notes);
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
            DeleteDirectoryContents(directory, runningExecutable, notes, null);
        }

        // protectedPaths: instance folders that stay, with everything in them
        private static void DeleteDirectoryContents(string directory, string runningExecutable, List<string> notes, List<string> protectedPaths)
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
                    if (protectedPaths != null && protectedPaths.Exists(kept => PathsEqual(full, kept) || IsPathWithinDirectory(full, kept))) continue;
                    if (isReparsePoint && TouchesProtected(full, protectedPaths)) continue;
                    if (isDirectory && !isReparsePoint)
                    {
                        DeleteDirectoryContents(full, runningExecutable, notes, protectedPaths);
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

        private static void RemoveEmptyDirectories(string target, List<string> protectedPaths)
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
                    // an instance's folder set inside a program folder stays as it is
                    if (TouchesProtected(directory, protectedPaths)) continue;
                    RemovePythonCachesBelow(directory);
                    RemoveEmptyDirectoriesBelow(directory);
                    if (Directory.GetFileSystemEntries(directory).Length == 0) Directory.Delete(directory, false);
                }
                catch { }
            }
        }

        // Python makes these beside the plugins when it runs them; they'd keep the plugin folders behind
        private static void RemovePythonCachesBelow(string directory)
        {
            foreach (string child in Directory.GetDirectories(directory))
            {
                try
                {
                    if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) != 0) continue;
                    if (String.Equals(Path.GetFileName(child), "__pycache__", StringComparison.OrdinalIgnoreCase))
                    {
                        foreach (string file in Directory.GetFiles(child, "*.pyc")) File.Delete(file);
                    }
                    else
                    {
                        RemovePythonCachesBelow(child);
                    }
                }
                catch { }
            }
        }

        internal static bool IsPathWithinDirectory(string path, string directory)
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
            // the folders .NET's list leaves out: Downloads, Saved Games,
            // OneDrive, the media folders, the public ones
            foreach (Guid known in KnownFolderIds)
            {
                IntPtr pointer = IntPtr.Zero;
                try
                {
                    if (SHGetKnownFolderPath(known, 0x00008000 /* KF_FLAG_DONT_VERIFY */, IntPtr.Zero, out pointer) != 0 || pointer == IntPtr.Zero) continue;
                    string path = Marshal.PtrToStringUni(pointer);
                    if (!String.IsNullOrWhiteSpace(path)) boundaries.Add(NormalizeCleanupDirectory(path));
                }
                catch { }
                finally { if (pointer != IntPtr.Zero) Marshal.FreeCoTaskMem(pointer); }
            }
            foreach (string library in SteamLibraryFolders())
            {
                try { boundaries.Add(NormalizeCleanupDirectory(library)); }
                catch { }
            }
            return boundaries;
        }

        [DllImport("shell32.dll")]
        private static extern int SHGetKnownFolderPath([MarshalAs(UnmanagedType.LPStruct)] Guid id, uint flags, IntPtr token, out IntPtr path);

        private static readonly Guid[] KnownFolderIds = new Guid[]
        {
            new Guid("374DE290-123F-4565-9164-39C4925E467B"), // Downloads
            new Guid("4C5C32FF-BB9D-43B0-B5B4-2D72E54EAAA4"), // Saved Games
            new Guid("A52BBA46-E9E1-435F-B3D9-28DAA648C0F6"), // OneDrive
            new Guid("33E28130-4E1E-4676-835A-98395C3BC3BB"), // Pictures
            new Guid("4BD8D571-6D19-48D3-BE97-422220080E43"), // Music
            new Guid("18989B1D-99B5-455B-841C-AB7C74E4DDFC"), // Videos
            new Guid("FDD39AD0-238F-46AF-ADB4-6C85480369C7"), // Documents
            new Guid("B4BFCC3A-DB2C-424C-B029-7FE99A87C641"), // Desktop
            new Guid("5E6C858F-0E22-4760-9AFE-EA3317B67173"), // the user's profile
            new Guid("DFDF76A2-C82A-4D63-906A-5644AC457385"), // Public
            new Guid("3D644C9B-1FB8-4F30-9B45-F670235F79C0"), // Public Downloads
            new Guid("ED4824AF-DCE4-45A8-81E2-FC7965083634"), // Public Documents
            new Guid("C4AA340D-F20F-4863-AFEF-F87EF2E6BA25"), // Public Desktop
            new Guid("905E63B6-C1BF-494E-B29C-65B732D3D21A"), // Program Files
            new Guid("7C5A40EF-A0FB-4BFC-874A-C0F2E0B9FA8E"), // Program Files (x86)
            new Guid("62AB5D82-FDC1-4DC3-A9DD-070D1D495D97"), // ProgramData
            new Guid("F38BF404-1D43-42F2-9305-67DE0B28FC23"), // Windows
            new Guid("0762D272-C50A-4BB0-A382-697DCD729B80")  // Users
        };

        // Steam's own folder and every library it lists, with their
        // steamapps\common: nothing an uninstall removes empty folders up to
        private static List<string> SteamLibraryFolders()
        {
            List<string> folders = new List<string>();
            try
            {
                string steam = null;
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey("Software\\Valve\\Steam", false))
                    if (key != null) steam = Convert.ToString(key.GetValue("SteamPath"));
                if (String.IsNullOrWhiteSpace(steam)) return folders;
                steam = Path.GetFullPath(steam.Replace('/', '\\'));
                List<string> libraries = new List<string> { steam };
                string list = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                if (File.Exists(list))
                    foreach (System.Text.RegularExpressions.Match match in System.Text.RegularExpressions.Regex.Matches(
                                 File.ReadAllText(list), "\"path\"\\s+\"([^\"]+)\""))
                    {
                        try { libraries.Add(Path.GetFullPath(match.Groups[1].Value.Replace("\\\\", "\\"))); }
                        catch { }
                    }
                foreach (string library in libraries)
                {
                    folders.Add(library);
                    folders.Add(Path.Combine(library, "steamapps"));
                    folders.Add(Path.Combine(library, "steamapps", "common"));
                }
            }
            catch { }
            return folders;
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

        private static List<string> GetSafeDirectoryCleanupCandidates(string directory, string stopAt)
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
                // above the folder only what the first installation created
                // (stopAt and the folders between): never a folder the user had
                if (String.IsNullOrEmpty(stopAt) || PathsEqual(current, stopAt) || !IsPathWithinDirectory(current, stopAt)) break;
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

        private static void RemoveEmptyInstallDirectoryAndParent(string target, string stopAt)
        {
            foreach (string directory in GetSafeDirectoryCleanupCandidates(target, stopAt))
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

        private const string ProjectPage = "https://github.com/ArialSenki/MO2-Revamped";

        private static void RegisterUninstallEntry(string target, string packageVersion, long installedBytes, bool eldenRingOnly)
        {
            string normalizedTarget = Path.GetFullPath(target).TrimEnd('\\');
            string displayName = ChooseDisplayName(normalizedTarget, eldenRingOnly);
            using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
            using (RegistryKey key = root.CreateSubKey(UninstallSubKey(normalizedTarget)))
            {
                if (key == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo registrar la opción de desinstalación."));
                string executable = Path.Combine(normalizedTarget, "ModOrganizer.exe");
                string uninstaller = Path.Combine(normalizedTarget, UninstallerFileName);
                string uninstall = "\"" + uninstaller + "\" --uninstall \"" + normalizedTarget + "\"";
                key.SetValue("DisplayName", displayName, RegistryValueKind.String);
                key.SetValue("DisplayVersion", packageVersion, RegistryValueKind.String);
                key.SetValue("Publisher", "ArialSenki", RegistryValueKind.String);
                key.SetValue("InstallLocation", normalizedTarget, RegistryValueKind.String);
                key.SetValue("DisplayIcon", executable + ",0", RegistryValueKind.String);
                key.SetValue("UninstallString", uninstall, RegistryValueKind.String);
                // the default choice: the program goes, the mods and data stay
                key.SetValue("QuietUninstallString", uninstall + " /S", RegistryValueKind.String);
                key.SetValue("URLInfoAbout", ProjectPage, RegistryValueKind.String);
                key.SetValue("HelpLink", ProjectPage + "/issues", RegistryValueKind.String);
                key.SetValue("URLUpdateInfo", ProjectPage + "/releases", RegistryValueKind.String);
                int major, minor;
                if (TryReadMajorMinor(packageVersion, out major, out minor))
                {
                    key.SetValue("VersionMajor", major, RegistryValueKind.DWord);
                    key.SetValue("VersionMinor", minor, RegistryValueKind.DWord);
                }
                // the uninstaller carries no program files to repair with: a
                // repair is the setup's Update with the same version
                key.SetValue("NoModify", 1, RegistryValueKind.DWord);
                key.SetValue("NoRepair", 1, RegistryValueKind.DWord);
                key.SetValue("EstimatedSize", (int)Math.Min(Int32.MaxValue, installedBytes / 1024), RegistryValueKind.DWord);
                // the date of the first installation, kept by updates
                string installDate = Convert.ToString(key.GetValue("InstallDate"));
                if (String.IsNullOrWhiteSpace(installDate))
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

        // "MO2 Revamped" for the first installation, and a name that tells
        // the others apart in Installed apps; an entry keeps the name it has
        private static string ChooseDisplayName(string normalizedTarget, bool eldenRingOnly)
        {
            string preferred = eldenRingOnly ? "MO2 Revamped – Elden Ring isolated" : "MO2 Revamped";
            List<string> takenByOthers = new List<string>();
            try
            {
                using (RegistryKey root = RegistryKey.OpenBaseKey(UninstallHive, RegistryView.Registry64))
                using (RegistryKey uninstall = root.OpenSubKey(UninstallKeyRoot.TrimEnd('\\'), false))
                {
                    if (uninstall != null)
                        foreach (string name in uninstall.GetSubKeyNames())
                        {
                            if (!name.StartsWith("MO2Revamped-", StringComparison.OrdinalIgnoreCase)) continue;
                            using (RegistryKey entry = uninstall.OpenSubKey(name, false))
                            {
                                if (entry == null) continue;
                                string location = Convert.ToString(entry.GetValue("InstallLocation"));
                                string displayName = Convert.ToString(entry.GetValue("DisplayName"));
                                if (PathsEqual(location, normalizedTarget))
                                {
                                    // the bare old name of a second installation is renamed below
                                    if (!String.IsNullOrWhiteSpace(displayName) && displayName.StartsWith("MO2 Revamped", StringComparison.Ordinal) &&
                                        !String.Equals(displayName, "MO2 Revamped", StringComparison.Ordinal))
                                        return displayName;
                                    continue;
                                }
                                if (!String.IsNullOrWhiteSpace(displayName)) takenByOthers.Add(displayName);
                            }
                        }
                }
            }
            catch { }
            if (!takenByOthers.Contains(preferred)) return preferred;
            return preferred + " (" + normalizedTarget + ")";
        }

        private static bool TryReadMajorMinor(string version, out int major, out int minor)
        {
            major = minor = 0;
            if (String.IsNullOrWhiteSpace(version)) return false;
            string[] parts = version.Split('-')[0].Split('.');
            return parts.Length >= 2 && Int32.TryParse(parts[0], out major) && Int32.TryParse(parts[1], out minor);
        }

        // -1, 0 or 1 for two release names such as 1.1.0-beta.8 and 1.1.0:
        // numbers first, then a release beats its pre-releases, which compare
        // part by part (beta.10 after beta.9)
        internal static int CompareReleaseVersions(string left, string right)
        {
            string[] a = (left ?? "").Trim().TrimStart('v', 'V').Split(new char[] { '-' }, 2);
            string[] b = (right ?? "").Trim().TrimStart('v', 'V').Split(new char[] { '-' }, 2);
            int result = CompareDotted(a[0], b[0], true);
            if (result != 0) return result;
            bool aPre = a.Length > 1 && a[1].Length > 0, bPre = b.Length > 1 && b[1].Length > 0;
            if (aPre != bPre) return aPre ? -1 : 1;
            return aPre ? CompareDotted(a[1], b[1], false) : 0;
        }

        private static int CompareDotted(string left, string right, bool padNumbers)
        {
            string[] a = left.Split('.'), b = right.Split('.');
            int count = Math.Max(a.Length, b.Length);
            for (int i = 0; i < count; i++)
            {
                string x = i < a.Length ? a[i] : (padNumbers ? "0" : null);
                string y = i < b.Length ? b[i] : (padNumbers ? "0" : null);
                if (x == null) return -1;
                if (y == null) return 1;
                long nx, ny;
                bool xNumber = Int64.TryParse(x, out nx), yNumber = Int64.TryParse(y, out ny);
                int part = xNumber && yNumber ? nx.CompareTo(ny)
                    : xNumber != yNumber ? (xNumber ? -1 : 1)
                    : String.Compare(x, y, StringComparison.OrdinalIgnoreCase);
                if (part != 0) return part < 0 ? -1 : 1;
            }
            return 0;
        }

        // the version of MO2 Revamped in this folder: its record's, or the
        // one Windows lists
        internal static string InstalledReleaseVersion(string target)
        {
            InstallState state = LoadState(target);
            if (state != null && !String.IsNullOrWhiteSpace(state.Version)) return state.Version;
            return RegisteredDisplayVersion(target) ?? "";
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

        // the other standard installations Windows lists: they share the
        // instances in AppData\Local\MO2 Revamped with this one
        internal static List<string> OtherStandardInstallations(string target)
        {
            List<string> others = new List<string>();
            foreach (string location in RegisteredInstallLocations())
                if (!PathsEqual(location, target) && UsesRevampedGlobalData(location)) others.Add(location);
            return others;
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
