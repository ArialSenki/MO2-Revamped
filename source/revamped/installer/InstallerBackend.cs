using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;
using System.Web.Script.Serialization;

namespace Mo2RevampedSetup
{
    internal static class InstallerBackend
    {
        private const string QtRuntimeResource = "Mo2RevampedSetup.qt-runtime.zip";
        private static readonly object EventFileLock = new object();

        public static bool TryHandleCommandLine(string[] args)
        {
            if (args == null || args.Length == 0) return false;
            try
            {
                if (String.Equals(args[0], "--translations", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 3) return true;
                    JavaScriptSerializer serializer = NewSerializer();
                    string json = serializer.Serialize(InstallerText.ExportTranslations(args[1]));
                    WriteAtomic(args[2], json);
                    return true;
                }
                if (String.Equals(args[0], "--inspect", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 3) return true;
                    if (args.Length > 3) InstallerText.SetLanguage(args[3]);
                    WriteAtomic(args[2], NewSerializer().Serialize(Inspect(args[1])));
                    return true;
                }
                if (String.Equals(args[0], "--migration-data", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 5) return true;
                    if (args.Length > 4) InstallerText.SetLanguage(args[4]);
                    int mask;
                    if (!Int32.TryParse(args[2], out mask)) mask = 0;
                    SetupRequest request = new SetupRequest();
                    request.SourceDataDirectory = args[1];
                    request.CopyProfiles = (mask & 1) != 0;
                    request.CopyMods = (mask & 2) != 0;
                    request.CopyDownloads = (mask & 4) != 0;
                    request.CopyOverwrite = (mask & 8) != 0;
                    request.CopySettings = (mask & 16) != 0;
                    request.CopyCategories = (mask & 32) != 0;
                    Dictionary<string, object> data = new Dictionary<string, object>();
                    data["hasData"] = SetupEngine.HasSelectedMigrationData(request);
                    WriteAtomic(args[3], NewSerializer().Serialize(data));
                    return true;
                }
                if (String.Equals(args[0], "--installations", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 2) return true;
                    Dictionary<string, object> data = new Dictionary<string, object>();
                    data["paths"] = SetupEngine.RegisteredInstallLocations().ToArray();
                    WriteAtomic(args[1], NewSerializer().Serialize(data));
                    return true;
                }
                if (String.Equals(args[0], "--backend", StringComparison.OrdinalIgnoreCase))
                {
                    RunBackend(args);
                    return true;
                }
                if (String.Equals(args[0], "--open-url", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 2) return true;
                    Environment.ExitCode = UnelevatedLauncher.OpenProjectPage(args[1]) ? 0 : 1;
                    return true;
                }
                if (String.Equals(args[0], "--launch", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 2) return true;
                    Environment.ExitCode = UnelevatedLauncher.Start(args[1]) ? 0 : 1;
                    return true;
                }
            }
            catch (Exception ex)
            {
                if (args.Length > 0 && args[0].StartsWith("--", StringComparison.Ordinal) && args.Length > 1)
                {
                    try
                    {
                        Dictionary<string, object> failure = new Dictionary<string, object>();
                        failure["success"] = false;
                        failure["error"] = ex.GetBaseException().Message;
                        int outputIndex = String.Equals(args[0], "--backend", StringComparison.OrdinalIgnoreCase) ? 3 : 2;
                        if (String.Equals(args[0], "--migration-data", StringComparison.OrdinalIgnoreCase)) outputIndex = 3;
                        if (String.Equals(args[0], "--installations", StringComparison.OrdinalIgnoreCase)) outputIndex = 1;
                        if (args.Length > outputIndex) WriteAtomic(args[outputIndex], NewSerializer().Serialize(failure));
                    }
                    catch { }
                }
                return true;
            }
            return false;
        }

        public static bool TryLaunchQtInstaller(string[] args)
        {
            Assembly assembly = Assembly.GetExecutingAssembly();
            using (Stream resource = assembly.GetManifestResourceStream(QtRuntimeResource))
            {
                if (resource == null) return false;

                string extractionRoot = Path.Combine(Path.GetTempPath(), "MO2 Revamped Setup", Guid.NewGuid().ToString("N"));
                bool cleanupScheduled = false;
                Directory.CreateDirectory(extractionRoot);
                string archivePath = Path.Combine(extractionRoot, "qt-runtime.zip");
                try
                {
                    using (FileStream archiveFile = new FileStream(archivePath, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                        resource.CopyTo(archiveFile);
                    ZipFile.ExtractToDirectory(archivePath, extractionRoot);
                    File.Delete(archivePath);

                    string executable = Path.Combine(extractionRoot, "MO2RevampedSetupQt.exe");
                    if (!File.Exists(executable)) throw new FileNotFoundException("The Qt setup interface is missing from the embedded runtime.", executable);
                    string iconPath = Path.Combine(extractionRoot, "MO2RevampedSetup.ico");
                    try
                    {
                        System.Drawing.Icon hostIcon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                        if (hostIcon != null) using (FileStream iconFile = new FileStream(iconPath, FileMode.Create, FileAccess.Write, FileShare.Read)) hostIcon.Save(iconFile);
                    }
                    catch { }

                    List<string> arguments = new List<string>();
                    arguments.Add("--host");
                    arguments.Add(Application.ExecutablePath);
                    arguments.Add("--language");
                    arguments.Add(InstallerText.DetectLanguage());
                    if (File.Exists(iconPath)) { arguments.Add("--icon"); arguments.Add(iconPath); }
#if ELDENRING_ONLY_INSTALLER
                    arguments.Add("--elden-ring-only");
#endif
#if UNINSTALLER_STUB
                    arguments.Add("--fixed-mode");
                    arguments.Add(IsRestoreRequest(args) ? "restore" : "uninstall");
                    arguments.Add("--target");
                    arguments.Add(ResolveTarget(args, Application.ExecutablePath));
#else
                    if (IsUninstallRequest(args))
                    {
                        arguments.Add("--fixed-mode");
                        arguments.Add(IsRestoreRequest(args) ? "restore" : "uninstall");
                        arguments.Add("--target");
                        arguments.Add(ResolveTarget(args, Application.ExecutablePath));
                    }
#endif
                    ProcessStartInfo start = new ProcessStartInfo();
                    start.FileName = executable;
                    start.Arguments = JoinArguments(arguments);
                    start.WorkingDirectory = extractionRoot;
                    start.UseShellExecute = false;
                    Process process = Process.Start(start);
                    if (process == null) throw new InvalidOperationException("Windows could not start the Qt setup interface.");
                    int processId = process.Id;
                    cleanupScheduled = ScheduleExtractionCleanup(extractionRoot, processId);
                    if (!cleanupScheduled) process.WaitForExit();
                    process.Dispose();
                    return true;
                }
                finally
                {
                    try { if (!cleanupScheduled && Directory.Exists(extractionRoot)) Directory.Delete(extractionRoot, true); }
                    catch { }
                }
            }
        }

        private static bool IsUninstallRequest(string[] args)
        {
            return args != null && args.Length >= 2 &&
                (String.Equals(args[0], "--uninstall", StringComparison.OrdinalIgnoreCase) ||
                 String.Equals(args[0], "--restore", StringComparison.OrdinalIgnoreCase));
        }

        private static bool IsRestoreRequest(string[] args)
        {
            return args != null && args.Length >= 2 &&
                String.Equals(args[0], "--restore", StringComparison.OrdinalIgnoreCase);
        }

        private static string ResolveTarget(string[] args, string hostPath)
        {
            return IsUninstallRequest(args) ? args[1] : Path.GetDirectoryName(hostPath);
        }

        private static string JoinArguments(IList<string> arguments)
        {
            List<string> quoted = new List<string>();
            foreach (string argument in arguments) quoted.Add(QuoteArgument(argument));
            return String.Join(" ", quoted.ToArray());
        }

        private static string QuoteArgument(string value)
        {
            if (value == null) value = "";
            if (value.Length > 0 && value.IndexOfAny(new char[] { ' ', '\t', '\n', '\v', '"' }) < 0) return value;
            StringBuilder result = new StringBuilder("\"");
            int slashes = 0;
            foreach (char character in value)
            {
                if (character == '\\') { slashes++; continue; }
                if (character == '"')
                {
                    result.Append('\\', slashes * 2 + 1);
                    result.Append('"');
                    slashes = 0;
                    continue;
                }
                result.Append('\\', slashes);
                slashes = 0;
                result.Append(character);
            }
            result.Append('\\', slashes * 2);
            result.Append('"');
            return result.ToString();
        }

        private static bool ScheduleExtractionCleanup(string directory, int processId)
        {
            try
            {
                string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
                if (!File.Exists(powershell)) return false;
                string escaped = (directory ?? "").Replace("'", "''");
                string script = "$p = " + processId.ToString() + "; try { $owner = [System.Diagnostics.Process]::GetProcessById($p); $owner.WaitForExit(); $owner.Dispose() } catch {}; " +
                    "$dir = '" + escaped + "'; try { if ([System.IO.Directory]::Exists($dir)) { Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue } } catch {};";
                string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
                ProcessStartInfo cleanup = new ProcessStartInfo();
                cleanup.FileName = powershell;
                cleanup.Arguments = "-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + encoded;
                cleanup.UseShellExecute = false;
                cleanup.CreateNoWindow = true;
                cleanup.WindowStyle = ProcessWindowStyle.Hidden;
                Process helper = Process.Start(cleanup);
                if (helper == null) return false;
                helper.Dispose();
                return true;
            }
            catch { return false; }
        }

        private static void RunBackend(string[] args)
        {
            if (args.Length < 4) return;
            string requestPath = args[1];
            string eventsPath = args[2];
            string resultPath = args[3];
            if (args.Length > 4) InstallerText.SetLanguage(args[4]);
            SetupDiagnostics.Initialize(new string[0]);
            SetupRequest request = NewSerializer().Deserialize<SetupRequest>(File.ReadAllText(requestPath, Encoding.UTF8));
            SetupDiagnostics.BeginOperation(request);
            // Without a UI thread, BackgroundWorker posts progress to the thread
            // pool, so events could be written out of order or after the result.
            // Run its callbacks inline on the worker thread instead.
            SynchronizationContext.SetSynchronizationContext(new InlineSynchronizationContext());
            using (ManualResetEvent finished = new ManualResetEvent(false))
            {
                BackgroundWorker worker = new BackgroundWorker();
                worker.WorkerReportsProgress = true;
                worker.DoWork += delegate(object sender, DoWorkEventArgs eventArgs)
                {
                    try { eventArgs.Result = SetupEngine.Execute(request, worker); }
                    catch (Exception error) { eventArgs.Result = error; }
                };
                worker.ProgressChanged += delegate(object sender, ProgressChangedEventArgs eventArgs)
                {
                    Dictionary<string, object> progress = new Dictionary<string, object>();
                    progress["type"] = "progress";
                    progress["percent"] = eventArgs.ProgressPercentage;
                    progress["message"] = eventArgs.UserState == null ? "" : eventArgs.UserState.ToString();
                    AppendEvent(eventsPath, NewSerializer().Serialize(progress));
                };
                worker.RunWorkerCompleted += delegate(object sender, RunWorkerCompletedEventArgs eventArgs)
                {
                    Dictionary<string, object> result = new Dictionary<string, object>();
                    Exception error = eventArgs.Error ?? eventArgs.Result as Exception;
                    OperationResult operation = error == null ? eventArgs.Result as OperationResult : null;
                    result["success"] = operation != null && operation.Success;
                    result["message"] = operation == null ? "" : InstallerText.T(operation.Message ?? "");
                    result["details"] = operation == null ? "" : operation.Details ?? "";
                    result["launchPath"] = operation == null ? "" : operation.LaunchPath ?? "";
                    result["error"] = error == null ? "" : error.GetBaseException().Message;
                    if (error != null) SetupDiagnostics.Error("Operation failed; the Qt installer backend returned an exception.", error);
                    try { WriteAtomic(resultPath, NewSerializer().Serialize(result)); }
                    catch (Exception writeError) { SetupDiagnostics.Error("Could not write the Qt installer result file.", writeError); }
                    finished.Set();
                };
                worker.RunWorkerAsync();
                finished.WaitOne();
                worker.Dispose();
            }
        }

        private sealed class InlineSynchronizationContext : SynchronizationContext
        {
            private readonly object gate = new object();

            public override void Post(SendOrPostCallback callback, object state)
            {
                lock (gate) callback(state);
            }

            public override void Send(SendOrPostCallback callback, object state)
            {
                lock (gate) callback(state);
            }

            public override SynchronizationContext CreateCopy()
            {
                return this;
            }
        }

        private static Dictionary<string, object> Inspect(string target)
        {
            Dictionary<string, object> result = new Dictionary<string, object>();
            string full = "";
            try { if (!String.IsNullOrWhiteSpace(target)) full = Path.GetFullPath(target); }
            catch { }
            bool exists = full.Length > 0 && Directory.Exists(full);
            bool recognized = exists && SetupEngine.IsRevampedInstallation(full);
            InstallState state = recognized ? SetupEngine.LoadState(full) : null;
            bool hasBackupDirectory = state != null && !String.IsNullOrWhiteSpace(state.BackupDirectory);
            bool pending = state != null && !String.Equals(state.Status, "Restored", StringComparison.OrdinalIgnoreCase) &&
                !String.Equals(state.Status, "Uninstalled", StringComparison.OrdinalIgnoreCase);
            bool validBackup = false;
            if (pending && !String.IsNullOrWhiteSpace(state.BackupDirectory) && Directory.Exists(state.BackupDirectory))
            {
                foreach (InstallFileState file in state.Files ?? new List<InstallFileState>())
                {
                    if (!file.ExistedBefore || file.IsUserData) continue;
                    try
                    {
                        string root = Path.GetFullPath(state.BackupDirectory).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
                        string backup = Path.GetFullPath(Path.Combine(state.BackupDirectory, file.Path));
                        if (backup.StartsWith(root, StringComparison.OrdinalIgnoreCase) && File.Exists(backup)) { validBackup = true; break; }
                    }
                    catch { }
                }
            }
            string version = "";
            try
            {
                string exe = Path.Combine(full, "ModOrganizer.exe");
                if (File.Exists(exe)) version = FileVersionInfo.GetVersionInfo(exe).FileVersion ?? "";
            }
            catch { }
            Version parsed;
            string normalized = version;
            while (normalized.Length > 0 && normalized.Split('.').Length < 4) normalized += ".0";
            bool compatible = Version.TryParse(normalized, out parsed) && parsed >= new Version(2, 5, 0, 0) && parsed <= new Version(2, 5, 2, 0);
            result["exists"] = exists;
            result["recognized"] = recognized;
            result["eldenRingOnly"] = recognized && SetupEngine.IsEldenRingOnlyInstallation(full);
            result["pendingRestore"] = pending;
            result["validBackup"] = validBackup;
            bool globalData = recognized && SetupEngine.UsesRevampedGlobalData(full);
            result["globalData"] = globalData;
            result["globalDataPath"] = globalData ? SetupEngine.RevampedAppDataDirectory() : "";
            result["hasBackupDirectory"] = hasBackupDirectory;
            result["version"] = version;
            result["compatibleVersion"] = compatible;
            result["stateStatus"] = state == null ? "" : state.Status ?? "";
            return result;
        }

        private static JavaScriptSerializer NewSerializer()
        {
            JavaScriptSerializer serializer = new JavaScriptSerializer();
            serializer.MaxJsonLength = Int32.MaxValue;
            serializer.RecursionLimit = 100;
            return serializer;
        }

        private static void AppendEvent(string path, string line)
        {
            try { lock (EventFileLock) File.AppendAllText(path, line + Environment.NewLine, new UTF8Encoding(false)); }
            catch { }
        }

        private static void WriteAtomic(string path, string content)
        {
            string directory = Path.GetDirectoryName(Path.GetFullPath(path));
            Directory.CreateDirectory(directory);
            string temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
            File.WriteAllText(temporary, content, new UTF8Encoding(false));
            if (File.Exists(path)) File.Delete(path);
            File.Move(temporary, path);
        }
    }

    // The setup runs elevated. Starting MO2 directly would give it the
    // administrator token, which breaks drag and drop from Explorer and leaves
    // files owned by Administrators. Passing the executable to explorer.exe
    // avoids that, but Explorer may open a folder window instead of running
    // the program. Asking the desktop shell, which runs with the user's normal
    // token, to execute the program starts MO2 itself, unelevated.
    internal static class UnelevatedLauncher
    {
        private const int SWC_DESKTOP = 8;
        private const int SWFO_NEEDDISPATCH = 1;
        private const uint SVGIO_BACKGROUND = 0;
        private static readonly Guid SID_STopLevelBrowser = new Guid("4C96BE40-915C-11CF-99D3-00AA004AE837");
        private static readonly Guid IID_IShellBrowser = new Guid("000214E2-0000-0000-C000-000000000046");
        private static readonly Guid IID_IDispatch = new Guid("00020400-0000-0000-C000-000000000046");
        private static readonly Guid CLSID_ShellWindows = new Guid("9BA05972-F6A8-11CF-A442-00A0C90A8F39");

        [ComImport, Guid("6D5140C1-7436-11CE-8034-00AA006009FA"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IServiceProvider
        {
            [PreserveSig]
            int QueryService(ref Guid service, ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object result);
        }

        // Only the vtable order matters for the members that are never called.
        [ComImport, Guid("000214E2-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IShellBrowser
        {
            void GetWindow();
            void ContextSensitiveHelp();
            void InsertMenusSB();
            void SetMenuSB();
            void RemoveMenusSB();
            void SetStatusTextSB();
            void EnableModelessSB();
            void TranslateAcceleratorSB();
            void BrowseObject();
            void GetViewStateStream();
            void GetControlWindow();
            void SendControlMsg();
            [PreserveSig]
            int QueryActiveShellView(out IShellView view);
        }

        [ComImport, Guid("000214E3-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IShellView
        {
            void GetWindow();
            void ContextSensitiveHelp();
            void TranslateAccelerator();
            void EnableModeless();
            void UIActivate();
            void Refresh();
            void CreateViewWindow();
            void DestroyViewWindow();
            void GetCurrentInfo();
            void AddPropertySheetPages();
            void SaveViewState();
            void SelectItem();
            [PreserveSig]
            int GetItemObject(uint item, ref Guid riid, [MarshalAs(UnmanagedType.IUnknown)] out object result);
        }

        public static bool Start(string executable)
        {
            string path;
            try { path = Path.GetFullPath(executable); }
            catch { return false; }
            if (!File.Exists(path)) return false;
            string directory = Path.GetDirectoryName(path);

            Exception shellError;
            if (TryShellExecute(path, directory, out shellError)) return true;
            if (shellError != null) SetupDiagnostics.Error("The desktop shell could not start MO2 unelevated.", shellError);
            // no fallback: started from this elevated setup, MO2 and every game
            // and mod it runs would run as administrator too
            return false;
        }

        // the project's pages only (release notes), opened by the desktop shell
        // so the browser does not run as administrator
        public static bool OpenProjectPage(string url)
        {
            Uri uri;
            if (!Uri.TryCreate(url, UriKind.Absolute, out uri) || uri.Scheme != Uri.UriSchemeHttps ||
                !String.Equals(uri.Host, "github.com", StringComparison.OrdinalIgnoreCase) ||
                !uri.AbsolutePath.StartsWith("/ArialSenki/MO2-Revamped", StringComparison.OrdinalIgnoreCase))
                return false;
            Exception shellError;
            if (TryShellExecute(uri.AbsoluteUri, "", out shellError)) return true;
            if (shellError != null) SetupDiagnostics.Error("The desktop shell could not open the release notes.", shellError);
            return false;
        }

        private static bool TryShellExecute(string path, string directory, out Exception error)
        {
            error = null;
            object shellWindows = null, desktop = null, browser = null, folderView = null, shell = null;
            IShellView view = null;
            try
            {
                Type shellWindowsType = Type.GetTypeFromCLSID(CLSID_ShellWindows);
                if (shellWindowsType == null) return false;
                shellWindows = Activator.CreateInstance(shellWindowsType);
                object location = 0;
                object root = null;
                object[] arguments = { location, root, SWC_DESKTOP, 0, SWFO_NEEDDISPATCH };
                ParameterModifier byRef = new ParameterModifier(5);
                byRef[0] = true; byRef[1] = true; byRef[3] = true;
                desktop = shellWindowsType.InvokeMember("FindWindowSW", BindingFlags.InvokeMethod, null, shellWindows,
                    arguments, new[] { byRef }, null, null);
                IServiceProvider provider = desktop as IServiceProvider;
                if (provider == null) return false;

                Guid service = SID_STopLevelBrowser;
                Guid browserId = IID_IShellBrowser;
                if (provider.QueryService(ref service, ref browserId, out browser) != 0 || browser == null) return false;
                if (((IShellBrowser)browser).QueryActiveShellView(out view) != 0 || view == null) return false;
                Guid dispatchId = IID_IDispatch;
                if (view.GetItemObject(SVGIO_BACKGROUND, ref dispatchId, out folderView) != 0 || folderView == null) return false;

                shell = folderView.GetType().InvokeMember("Application", BindingFlags.GetProperty, null, folderView, null);
                if (shell == null) return false;
                shell.GetType().InvokeMember("ShellExecute", BindingFlags.InvokeMethod, null, shell,
                    new object[] { path, "", directory, "open", 1 });
                return true;
            }
            catch (Exception ex)
            {
                error = ex;
                return false;
            }
            finally
            {
                foreach (object item in new object[] { shell, folderView, view, browser, desktop, shellWindows })
                    if (item != null && Marshal.IsComObject(item)) Marshal.ReleaseComObject(item);
            }
        }
    }
}
