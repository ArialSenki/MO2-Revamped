using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.AccessControl;
using System.Security.Principal;
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
        // where the process was started from, for relative paths on its
        // command line (/dir=.\MO2); the process itself moves away from it
        private static string startDirectory = "";

        public static void LeaveStartDirectory()
        {
            try
            {
                startDirectory = Environment.CurrentDirectory;
                Environment.CurrentDirectory = Environment.SystemDirectory;
            }
            catch { }
        }

        private static string FullPathFromStart(string path)
        {
            if (String.IsNullOrWhiteSpace(path)) return "";
            string value = path.Trim().Trim('"');
            if (!Path.IsPathRooted(value) && !String.IsNullOrEmpty(startDirectory)) value = Path.Combine(startDirectory, value);
            return Path.GetFullPath(value);
        }

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
                if (String.Equals(args[0], "--open-path", StringComparison.OrdinalIgnoreCase))
                {
                    if (args.Length < 2) return true;
                    Environment.ExitCode = UnelevatedLauncher.OpenPath(args[1]) ? 0 : 1;
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
                        failure["logPath"] = SetupDiagnostics.CurrentPath;
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

                string extractionRoot = CreateWorkFolder();
                bool cleanupScheduled = false;
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
                        using (Stream embeddedIcon = assembly.GetManifestResourceStream("Mo2RevampedSetup.icon.ico"))
                        {
                            if (embeddedIcon != null)
                            {
                                using (FileStream iconFile = new FileStream(iconPath, FileMode.Create, FileAccess.Write, FileShare.Read)) embeddedIcon.CopyTo(iconFile);
                            }
                            else
                            {
                                // Icon.Save keeps only 32 px in 16 colors: a last resort
                                System.Drawing.Icon hostIcon = System.Drawing.Icon.ExtractAssociatedIcon(Application.ExecutablePath);
                                if (hostIcon != null) using (FileStream iconFile = new FileStream(iconPath, FileMode.Create, FileAccess.Write, FileShare.Read)) hostIcon.Save(iconFile);
                            }
                        }
                    }
                    catch { }

#if UNINSTALLER_STUB
                    bool fixedTask = true;
#else
                    bool fixedTask = IsUninstallRequest(args);
#endif
                    // a relative --uninstall folder means one below where it was started
                    string fixedTarget = fixedTask ? FullPathFromStart(ResolveTarget(args, Application.ExecutablePath)) : null;
                    // an uninstaller inside the folder it removes runs from a
                    // copy in this work folder: the window and the engine then
                    // hold nothing in it, and the folder can go entirely. The
                    // copy goes with the work folder when the window closes.
                    string host = Application.ExecutablePath;
                    bool hostCopied = false;
                    if (fixedTarget != null && SetupEngine.IsPathWithinDirectory(host, fixedTarget))
                    {
                        try
                        {
                            string copy = Path.Combine(extractionRoot, "MO2RevampedUninstall.exe");
                            File.Copy(host, copy, true);
                            host = copy;
                            hostCopied = true;
                        }
                        catch (Exception copyError)
                        {
                            SetupDiagnostics.Error("Could not copy the uninstaller out of '" + fixedTarget + "'; it runs from there.", copyError);
                        }
                    }

                    List<string> arguments = new List<string>();
                    arguments.Add("--host");
                    arguments.Add(host);
                    arguments.Add("--language");
                    arguments.Add(InstallerText.DetectLanguage());
                    if (File.Exists(iconPath)) { arguments.Add("--icon"); arguments.Add(iconPath); }
#if ELDENRING_ONLY_INSTALLER
                    arguments.Add("--elden-ring-only");
#endif
                    if (fixedTask)
                    {
                        arguments.Add("--fixed-mode");
                        arguments.Add(IsRestoreRequest(args) ? "restore" : "uninstall");
                        arguments.Add("--target");
                        arguments.Add(fixedTarget);
                    }
                    ProcessStartInfo start = new ProcessStartInfo();
                    start.FileName = executable;
                    start.Arguments = JoinArguments(arguments);
                    start.WorkingDirectory = extractionRoot;
                    start.UseShellExecute = false;
                    Process process = Process.Start(start);
                    if (process == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo abrir la ventana del instalador."));
                    int processId = process.Id;
                    cleanupScheduled = ScheduleExtractionCleanup(extractionRoot, processId);
                    // waiting here would keep this uninstaller, inside the
                    // folder being removed, running until the window closes
                    if (!cleanupScheduled && hostCopied)
                    {
                        cleanupScheduled = true;
                        SetupDiagnostics.Info("No cleanup helper started; the work folder '" + extractionRoot + "' is left for the next setup to remove.");
                    }
                    if (!cleanupScheduled) process.WaitForExit();
                    process.Dispose();
                    return true;
                }
                finally
                {
                    if (!cleanupScheduled) RemoveWorkFolder(extractionRoot);
                }
            }
        }

        // Silent runs, without the window:
        //   setup.exe /S [/dir=<folder>] [/isolated] [/nodesktop] [/nostartmenu]
        //             [/nonxm] [/launch] [/forceclose]
        //   setup.exe /S /uninstall /dir=<folder> [/keepdata | /removeall [/removeappdata]] [/forceclose]
        //   unins000.exe /S [/keepdata | /removeall [/removeappdata]]  (also --uninstall <folder> /S)
        // A folder MO2 Revamped is in gets updated, any other a new
        // installation. /quiet and /silent mean /S. An uninstall always names
        // its folder, except unins000.exe, whose folder is its own. On a
        // standard installation /removeall also needs /removeappdata, as the
        // instances in AppData are what it removes. The exit code is 0 when
        // the task worked, 1 when it failed and 2 for a wrong command line
        // (an unknown switch, a stray word, /restore); the log is in
        // %TEMP%\MO2 Revamped\Installer Logs.
        private static readonly HashSet<string> SilentSwitches = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "S", "quiet", "silent", "verysilent", "uninstall", "keepdata", "removeall", "removeappdata",
            "isolated", "nodesktop", "nostartmenu", "nonxm", "launch", "forceclose", "from-copy"
        };

        public static bool TryRunSilent(string[] args)
        {
            if (args == null) return false;
            bool silent = false;
            foreach (string argument in args)
                if (IsSwitch(argument, "S") || IsSwitch(argument, "quiet") || IsSwitch(argument, "silent") || IsSwitch(argument, "verysilent"))
                    silent = true;
            if (!silent) return false;

            Dictionary<string, string> options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            string folder = null;
            bool uninstall = false;
            int parentProcessId = 0;
            List<string> wrong = new List<string>();
            for (int i = 0; i < args.Length; i++)
            {
                string argument = args[i] ?? "";
                if (String.Equals(argument, "--uninstall", StringComparison.OrdinalIgnoreCase) ||
                    String.Equals(argument, "--target", StringComparison.OrdinalIgnoreCase))
                {
                    if (i + 1 >= args.Length) { wrong.Add(argument); continue; }
                    uninstall |= String.Equals(argument, "--uninstall", StringComparison.OrdinalIgnoreCase);
                    folder = args[++i];
                    continue;
                }
                if (argument.StartsWith("/dir=", StringComparison.OrdinalIgnoreCase) || argument.StartsWith("/D=", StringComparison.OrdinalIgnoreCase))
                {
                    folder = argument.Substring(argument.IndexOf('=') + 1);
                    continue;
                }
                if (argument.StartsWith("/parent-pid=", StringComparison.OrdinalIgnoreCase))
                {
                    if (!Int32.TryParse(argument.Substring(argument.IndexOf('=') + 1), out parentProcessId)) wrong.Add(argument);
                    continue;
                }
                // a restore goes through the window: a silent one would choose
                // for the user which files come back
                if (argument.StartsWith("/", StringComparison.Ordinal) || argument.StartsWith("-", StringComparison.Ordinal))
                {
                    string name = argument.TrimStart('/', '-');
                    if (SilentSwitches.Contains(name)) options[name] = "1";
                    else wrong.Add(argument);
                    continue;
                }
                // a stray word: a path without /dir=, a mistyped switch
                wrong.Add(argument);
            }
            if (options.ContainsKey("uninstall")) uninstall = true;
            bool removeAll = options.ContainsKey("removeall");
            bool removeAppData = options.ContainsKey("removeappdata");
#if UNINSTALLER_STUB
            uninstall = true;
            // unins000.exe without a folder removes the installation it is in
            if (String.IsNullOrWhiteSpace(folder))
            {
                string own = Path.GetDirectoryName(Application.ExecutablePath);
                if (SetupEngine.IsRevampedInstallation(own)) folder = own;
            }
#endif
            if (uninstall && String.IsNullOrWhiteSpace(folder)) wrong.Add(InstallerText.T("falta /dir=<carpeta>"));
            if (removeAll && options.ContainsKey("keepdata")) wrong.Add("/keepdata + /removeall");
            if (removeAppData && !removeAll) wrong.Add("/removeappdata " + InstallerText.T("sin") + " /removeall");
            if (!uninstall && (removeAll || options.ContainsKey("keepdata"))) wrong.Add("/removeall /keepdata " + InstallerText.T("sin") + " /uninstall");

            string target = null;
            if (wrong.Count == 0)
            {
                bool isolatedEdition = options.ContainsKey("isolated");
#if ELDENRING_ONLY_INSTALLER
                isolatedEdition = true;
#endif
                try
                {
                    target = FullPathFromStart(String.IsNullOrWhiteSpace(folder)
                        ? (isolatedEdition ? @"C:\MO2 Revamped\Isolated\Elden Ring" : @"C:\MO2 Revamped") : folder);
                }
                catch (Exception ex) { wrong.Add(folder + " (" + ex.Message + ")"); }
                // on a standard installation "everything" is the instances in
                // AppData, shared with every standard installation: it is
                // asked for by name
                if (target != null && uninstall && removeAll && SetupEngine.UsesRevampedGlobalData(target) && !removeAppData)
                    wrong.Add("/removeall " + InstallerText.T("en una instalación estándar necesita también") + " /removeappdata");
                if (target != null && uninstall && removeAppData && SetupEngine.IsRevampedInstallation(target) && !SetupEngine.UsesRevampedGlobalData(target))
                    wrong.Add("/removeappdata " + InstallerText.T("solo vale para una instalación estándar"));
            }
            if (wrong.Count > 0)
            {
                SetupDiagnostics.Info("Silent run refused, wrong command line: " + String.Join("; ", wrong.ToArray()) + ". Nothing was changed.");
                Environment.ExitCode = 2;
                return true;
            }

            int exitCode = 1;
            try
            {
                bool isolated = options.ContainsKey("isolated");
#if ELDENRING_ONLY_INSTALLER
                isolated = true;
#endif
                // an uninstaller inside the folder it removes runs again from
                // a copy outside it (see TryLaunchQtInstaller). It waits for
                // the copy and returns its exit code; the copy leaves this
                // file to a helper that deletes it once this process ends.
                if (uninstall && !options.ContainsKey("from-copy") && SetupEngine.IsPathWithinDirectory(Application.ExecutablePath, target))
                {
                    string work = CreateWorkFolder();
                    string copy = Path.Combine(work, "MO2RevampedUninstall.exe");
                    File.Copy(Application.ExecutablePath, copy, true);
                    List<string> again = new List<string>();
                    again.Add("/S");
                    again.Add("/uninstall");
                    again.Add("/dir=" + target);
                    again.Add("/from-copy");
                    again.Add("/parent-pid=" + Process.GetCurrentProcess().Id);
                    if (removeAll) again.Add("/removeall");
                    if (removeAppData) again.Add("/removeappdata");
                    if (options.ContainsKey("forceclose")) again.Add("/forceclose");
                    ProcessStartInfo start = new ProcessStartInfo(copy, JoinArguments(again));
                    start.WorkingDirectory = work;
                    start.UseShellExecute = false;
                    using (Process process = Process.Start(start))
                    {
                        if (process == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo abrir la ventana del instalador."));
                        if (!ScheduleExtractionCleanup(work, process.Id))
                            SetupDiagnostics.Info("No cleanup helper started for the uninstaller copy in '" + work + "'.");
                        SetupDiagnostics.Info("The silent uninstall goes on from '" + copy + "'; waiting for it.");
                        process.WaitForExit();
                        exitCode = process.ExitCode;
                    }
                    Environment.ExitCode = exitCode;
                    return true;
                }
                // the copy: the unins000.exe that started it is still running
                // inside the folder; its file goes after it exits
                if (options.ContainsKey("from-copy") && parentProcessId > 0)
                {
                    try
                    {
                        using (Process parent = Process.GetProcessById(parentProcessId))
                        {
                            string parentExecutable = parent.MainModule.FileName;
                            if (SetupEngine.IsPathWithinDirectory(parentExecutable, target) &&
                                Path.GetFileName(parentExecutable).StartsWith("unins", StringComparison.OrdinalIgnoreCase))
                                SetupEngine.HoldUninstaller(parentExecutable, parentProcessId);
                        }
                    }
                    catch (Exception ex) { SetupDiagnostics.Error("Could not identify the uninstaller that started this copy.", ex); }
                }

                SetupRequest request = new SetupRequest();
                request.TargetDirectory = target;
                request.ForceCloseMo2 = options.ContainsKey("forceclose");
                if (uninstall)
                {
                    request.Mode = SetupMode.Uninstall;
                    bool standard = SetupEngine.UsesRevampedGlobalData(target);
                    // a standard installation's folder holds no data of its
                    // own: its "everything" is the instances in AppData
                    request.RemoveAllRemainingContents = removeAll && !standard;
                    request.RemoveRevampedAppData = removeAll && removeAppData && standard;
                }
                else
                {
                    request.StartMenuShortcut = !options.ContainsKey("nostartmenu");
                    request.DesktopShortcut = !options.ContainsKey("nodesktop");
                    request.LaunchWhenDone = options.ContainsKey("launch");
                    // links keep going where they go unless nothing alive takes
                    // them (the engine checks again before writing)
                    request.RegisterNxmHandler = !options.ContainsKey("nonxm");
                    if (SetupEngine.IsRevampedInstallation(target))
                    {
                        request.Mode = SetupMode.Update;
                        string installed = SetupEngine.InstalledReleaseVersion(target);
                        string setupVersion = SetupDiagnostics.SetupVersion;
                        if (installed.Length > 0 && setupVersion.Length > 0 && SetupEngine.CompareReleaseVersions(setupVersion, installed) < 0)
                            throw new InvalidOperationException(String.Format(InstallerText.T("La instalación tiene una versión más reciente ({0}) que este instalador ({1}). No se modificó la carpeta."), installed, setupVersion));
                    }
                    else
                    {
                        bool keptIsolated = File.Exists(Path.Combine(target, "eldenring-only.portable"));
                        bool keptPortable = keptIsolated || File.Exists(Path.Combine(target, "portable.txt")) || File.Exists(Path.Combine(target, "ModOrganizer.ini"));
                        request.Mode = isolated || keptIsolated ? SetupMode.EldenRingPortable : SetupMode.Fresh;
                        request.InstanceKind = isolated || keptPortable ? InstanceKind.Portable : InstanceKind.Standard;
                        request.ReinstallOverKeptData = SetupEngine.HasKeptData(target);
                    }
                }
                SetupDiagnostics.BeginOperation(request);
                OperationResult result = SetupEngine.Execute(request, null);
                SetupDiagnostics.Info("Silent run finished: " + (result.Success ? "success" : "needs attention") + ". " +
                    InstallerText.T(result.Message ?? "") + " " + (result.Details ?? ""));
                if (result.Success && !String.IsNullOrEmpty(result.LaunchPath) && !UnelevatedLauncher.Start(result.LaunchPath))
                    SetupDiagnostics.Info("MO2 could not be opened after the silent run.");
                exitCode = result.Success ? 0 : 1;
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("The silent run failed.", ex);
                exitCode = 1;
            }
            Environment.ExitCode = exitCode;
            return true;
        }

        private static bool IsSwitch(string argument, string name)
        {
            if (String.IsNullOrEmpty(argument)) return false;
            string value = argument.TrimStart('/', '-');
            return value.Length < argument.Length && String.Equals(value, name, StringComparison.OrdinalIgnoreCase);
        }

        // The elevated setup runs and reads what it keeps here. A program of the
        // same user can write to %TEMP% and rename folders in it, so an elevated
        // run works below Program Files, in a folder only administrators can
        // write, and stops if Windows refuses it: falling back to %TEMP% would
        // undo that protection. Without elevation there is nothing to protect:
        // %TEMP% is used.
        internal static string CreateWorkFolder()
        {
            string name = Guid.NewGuid().ToString("N");
            if (new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator))
            {
                try
                {
                    string programFiles = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
                    string parent = Path.Combine(programFiles, "MO2 Revamped Setup");
                    Directory.CreateDirectory(parent);
                    // what a power cut or a killed setup left behind
                    foreach (string old in Directory.GetDirectories(parent))
                    {
                        try
                        {
                            if (DateTime.UtcNow - Directory.GetLastWriteTimeUtc(old) > TimeSpan.FromDays(1)) Directory.Delete(old, true);
                        }
                        catch (IOException) { }
                        catch (UnauthorizedAccessException) { }
                    }
                    DirectorySecurity security = new DirectorySecurity();
                    security.SetAccessRuleProtection(true, false);
                    foreach (WellKnownSidType account in new WellKnownSidType[] { WellKnownSidType.BuiltinAdministratorsSid, WellKnownSidType.LocalSystemSid })
                    {
                        security.AddAccessRule(new FileSystemAccessRule(new SecurityIdentifier(account, null), FileSystemRights.FullControl,
                            InheritanceFlags.ContainerInherit | InheritanceFlags.ObjectInherit, PropagationFlags.None, AccessControlType.Allow));
                    }
                    string secured = Path.Combine(programFiles, "MO2 Revamped Setup", name);
                    Directory.CreateDirectory(secured, security);
                    return secured;
                }
                catch (IOException error) { throw WorkFolderRefused(error); }
                catch (UnauthorizedAccessException error) { throw WorkFolderRefused(error); }
            }
            string folder = Path.Combine(Path.GetTempPath(), "MO2 Revamped Setup", name);
            Directory.CreateDirectory(folder);
            return folder;
        }

        private static Exception WorkFolderRefused(Exception error)
        {
            return new InvalidOperationException(String.Format(InstallerText.T("Windows no dejó crear la carpeta de trabajo protegida en Archivos de programa, así que el instalador no continuó. Comprueba el espacio libre y que ningún antivirus la esté bloqueando, y vuelve a intentarlo.\n\n{0}"), error.Message));
        }

        internal static void RemoveWorkFolder(string folder)
        {
            try
            {
                if (Directory.Exists(folder)) Directory.Delete(folder, true);
                Directory.Delete(Path.GetDirectoryName(folder));
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        private static bool IsElevated()
        {
#if INSTALLER_TEST
            return Environment.GetEnvironmentVariable("MO2R_TEST_ELEVATED") == "1";
#else
            return new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
#endif
        }

        // An elevated run reads its request and writes its answers only in the
        // folder CreateWorkFolder protects. The Qt window falls back to %TEMP%
        // when it can't use that folder, and %TEMP% belongs to any program of
        // the user: there the backend stops instead of obeying what it finds.
        private static void RequireProtectedPath(string path)
        {
            if (!IsElevated()) return;
            string protectedRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles), "MO2 Revamped Setup");
#if INSTALLER_TEST
            protectedRoot = Environment.GetEnvironmentVariable("MO2R_TEST_PROTECTED_ROOT") ?? protectedRoot;
#endif
            string prefix = Path.GetFullPath(protectedRoot).TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar;
            if (!Path.GetFullPath(path).StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException(InstallerText.T("Un instalador con permisos de administrador solo trabaja dentro de su carpeta protegida: ") + path);
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

        // A hidden helper waits for the process to end and deletes its work
        // folder (the window's runtime, an uninstaller's copy); what Windows
        // still refuses after that is queued for deletion at the next start,
        // files before the folders that hold them.
        internal static bool ScheduleExtractionCleanup(string directory, int processId)
        {
            try
            {
                string powershell = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
                if (!File.Exists(powershell)) return false;
                string escaped = (directory ?? "").Replace("'", "''");
                // Clear-Tree empties a folder bottom-up and never goes through a
                // junction or link: those are removed (or queued) as links only.
                // Remove-Item -Recurse and GetFiles(AllDirectories) follow them.
                string script = "function Clear-Tree([string]$path, [bool]$queue) { foreach ($entry in [System.IO.Directory]::GetFileSystemEntries($path)) { try { " +
                    "$a = [System.IO.File]::GetAttributes($entry); $isDir = ($a -band [System.IO.FileAttributes]::Directory) -ne 0; " +
                    "if ($isDir -and ($a -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) { Clear-Tree $entry $queue }; " +
                    "if ($queue) { [void][Mo2Revamped.Reboot]::MoveFileEx($entry, [NullString]::Value, 4) } " +
                    "elseif ($isDir) { [System.IO.Directory]::Delete($entry, $false) } " +
                    "else { [System.IO.File]::SetAttributes($entry, [System.IO.FileAttributes]::Normal); [System.IO.File]::Delete($entry) } } catch {} } }; " +
                    "$p = " + processId.ToString() + "; try { $owner = [System.Diagnostics.Process]::GetProcessById($p); $owner.WaitForExit(); $owner.Dispose() } catch {}; " +
                    "$dir = '" + escaped + "'; $linked = $false; try { $linked = ([System.IO.File]::GetAttributes($dir) -band [System.IO.FileAttributes]::ReparsePoint) -ne 0 } catch {}; " +
                    "for ($attempt = 0; $attempt -lt 20 -and [System.IO.Directory]::Exists($dir); $attempt++) { try { if (-not $linked) { Clear-Tree $dir $false }; [System.IO.Directory]::Delete($dir, $false) } catch {}; if ([System.IO.Directory]::Exists($dir)) { Start-Sleep -Milliseconds 500 } }; " +
                    "if ([System.IO.Directory]::Exists($dir)) { try { Add-Type -Namespace Mo2Revamped -Name Reboot -MemberDefinition '[System.Runtime.InteropServices.DllImport(\"kernel32.dll\", CharSet = System.Runtime.InteropServices.CharSet.Unicode)] public static extern bool MoveFileEx(string existingName, string newName, int flags);'; " +
                    "if (-not $linked) { Clear-Tree $dir $true }; [void][Mo2Revamped.Reboot]::MoveFileEx($dir, [NullString]::Value, 4) } catch {} } " +
                    "else { try { [System.IO.Directory]::Delete([System.IO.Path]::GetDirectoryName($dir), $false) } catch {} };";
                string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
                ProcessStartInfo cleanup = new ProcessStartInfo();
                cleanup.FileName = powershell;
                cleanup.Arguments = "-NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand " + encoded;
                // never inherited: a double-clicked unins000.exe starts inside
                // the folder it removes
                cleanup.WorkingDirectory = Environment.SystemDirectory;
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
            try
            {
                RequireProtectedPath(requestPath);
                RequireProtectedPath(eventsPath);
                RequireProtectedPath(resultPath);
            }
            catch (Exception refused)
            {
                SetupDiagnostics.Error("The elevated backend refused its working files.", refused);
                throw;
            }
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
                    result["logPath"] = SetupDiagnostics.CurrentPath;
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
            result["exists"] = exists;
            result["recognized"] = recognized;
            bool eldenRingOnly = recognized && SetupEngine.IsEldenRingOnlyInstallation(full);
            result["eldenRingOnly"] = eldenRingOnly;
            result["pendingRestore"] = pending;
            result["validBackup"] = validBackup;
            bool globalData = recognized && SetupEngine.UsesRevampedGlobalData(full);
            result["globalData"] = globalData;
            string globalDataPath = "";
            try { if (globalData) globalDataPath = SetupEngine.RevampedAppDataDirectory(); }
            catch { }
            result["globalDataPath"] = globalDataPath;
            // what an uninstall offers: a standard installation keeps its
            // data in AppData, a portable or isolated one beside the program
            result["kind"] = !recognized ? "" : eldenRingOnly ? "isolated" : globalData ? "standard" : "portable";
            result["instanceFoldersInside"] = recognized ? SetupEngine.InstanceFoldersInside(full).ToArray() : new string[0];
            result["externalFolders"] = recognized ? SetupEngine.ExternalDataFolders(full, globalData).ToArray() : new string[0];
            result["hasBackupDirectory"] = hasBackupDirectory;
            result["version"] = version;
            // any installation MO2 Revamped's marker or record identifies can
            // be updated, whatever MO2 version it is built on
            result["compatibleVersion"] = recognized;
            string installedVersion = recognized ? SetupEngine.InstalledReleaseVersion(full) : "";
            string setupVersion = SetupDiagnostics.SetupVersion;
            result["installedVersion"] = installedVersion;
            result["setupVersion"] = setupVersion;
            bool comparable = installedVersion.Length > 0 && setupVersion.Length > 0;
            result["downgrade"] = comparable && SetupEngine.CompareReleaseVersions(setupVersion, installedVersion) < 0;
            result["sameVersion"] = comparable && SetupEngine.CompareReleaseVersions(setupVersion, installedVersion) == 0;
            result["stateStatus"] = state == null ? "" : state.Status ?? "";
            // the program Nexus links open with now, and whether it is alive
            string nxmHandler = SetupEngine.RegisteredNxmHandler();
            bool nxmLive = false;
            try { nxmLive = nxmHandler.Length > 0 && File.Exists(nxmHandler); }
            catch { }
            result["nxmHandler"] = nxmHandler;
            result["nxmHandlerLive"] = nxmLive;
            result["nxmHandlerHere"] = full.Length > 0 && nxmHandler.Length > 0 && SetupEngine.IsPathWithinDirectory(nxmHandler, full);
            string otherAccount = SetupEngine.OtherAccountWarning();
            result["otherAccountWarning"] = otherAccount ?? "";
            // the engine's own decisions, so the window says what will happen:
            // whether a new installation goes back into kept data, what an
            // uninstall keeps because it isn't MO2's, and whether the shared
            // instances in AppData may be removed at all (another standard
            // installation, or AppData of another account than the user's)
            result["keptData"] = SetupEngine.HasKeptData(full);
            // "Remove everything" clears the whole folder only when the first
            // installation found it empty; otherwise only MO2's data goes
            result["targetWasEmpty"] = state != null && state.TargetWasEmpty &&
                !String.Equals(state.Status, "Uninstalled", StringComparison.OrdinalIgnoreCase) &&
                !String.Equals(state.Status, "Restored", StringComparison.OrdinalIgnoreCase);
            result["unknownEntries"] = recognized ? SetupEngine.UnknownEntries(full).ToArray() : new string[0];
            result["otherStandardInstalls"] = globalData ? SetupEngine.OtherStandardInstallations(full).ToArray() : new string[0];
            result["globalDataOtherAccount"] = !String.IsNullOrEmpty(otherAccount);
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
            RequireProtectedPath(path);
            string directory = Path.GetDirectoryName(Path.GetFullPath(path));
            Directory.CreateDirectory(directory);
            string temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
            File.WriteAllText(temporary, content, new UTF8Encoding(false));
            if (File.Exists(path)) File.Delete(path);
            File.Move(temporary, path);
        }
    }

    // Windows shortcuts (.lnk) through the shell's own IShellLink object.
    // WScript.Shell, which the setup used before, needs late-bound COM calls
    // (IDispatch) that the .NET of Wine and Proton doesn't implement: on a
    // Steam Deck the shortcut threw "The method or operation is not
    // implemented" and the whole install was rolled back. Wine implements
    // IShellLink itself; WScript.Shell stays as the fallback.
    internal static class ShellLink
    {
        [ComImport, Guid("00021401-0000-0000-C000-000000000046")]
        private class CShellLink { }

        // the vtable order of IShellLinkW
        [ComImport, Guid("000214F9-0000-0000-C000-000000000046"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
        private interface IShellLinkW
        {
            void GetPath([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder file, int size, IntPtr findData, uint flags);
            void GetIDList(out IntPtr idList);
            void SetIDList(IntPtr idList);
            void GetDescription([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder name, int size);
            void SetDescription([MarshalAs(UnmanagedType.LPWStr)] string name);
            void GetWorkingDirectory([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder directory, int size);
            void SetWorkingDirectory([MarshalAs(UnmanagedType.LPWStr)] string directory);
            void GetArguments([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder arguments, int size);
            void SetArguments([MarshalAs(UnmanagedType.LPWStr)] string arguments);
            void GetHotkey(out short hotkey);
            void SetHotkey(short hotkey);
            void GetShowCmd(out int showCmd);
            void SetShowCmd(int showCmd);
            void GetIconLocation([Out, MarshalAs(UnmanagedType.LPWStr)] StringBuilder iconPath, int size, out int icon);
            void SetIconLocation([MarshalAs(UnmanagedType.LPWStr)] string iconPath, int icon);
            void SetRelativePath([MarshalAs(UnmanagedType.LPWStr)] string relativePath, uint reserved);
            void Resolve(IntPtr window, uint flags);
            void SetPath([MarshalAs(UnmanagedType.LPWStr)] string file);
        }

        private const int STGM_READ = 0;

        // Wine registers the shell's shortcut object as apartment-threaded:
        // made from the setup's worker (MTA) it would come as a proxy Wine
        // can't marshal. Each call runs on a thread of its own in an STA,
        // which Windows takes as well.
        private static T OnSta<T>(Func<T> work)
        {
            T result = default(T);
            Exception error = null;
            Thread thread = new Thread(delegate()
            {
                try { result = work(); }
                catch (Exception ex) { error = ex; }
            });
            thread.SetApartmentState(ApartmentState.STA);
            thread.IsBackground = true;
            thread.Start();
            thread.Join();
            if (error != null) throw new InvalidOperationException(error.Message, error);
            return result;
        }

        private static bool loggedReadFailure;

        public static void Create(string link, string target, string workingDirectory, string description,
            string iconPath, int iconIndex)
        {
            OnSta<bool>(delegate()
            {
                CreateHere(link, target, workingDirectory, description, iconPath, iconIndex);
                return true;
            });
        }

        public static string Target(string link)
        {
            return OnSta<string>(delegate() { return TargetHere(link); });
        }

        private static void CreateHere(string link, string target, string workingDirectory, string description,
            string iconPath, int iconIndex)
        {
            Exception shellLinkError;
            if (TryCreateWithShellLink(link, target, workingDirectory, description, iconPath, iconIndex, out shellLinkError))
                return;
            SetupDiagnostics.Error("IShellLink could not create '" + link + "'; trying WScript.Shell.", shellLinkError);
            CreateWithScriptHost(link, target, workingDirectory, description, iconPath + "," + iconIndex);
        }

        private static string TargetHere(string link)
        {
            object instance = null;
            try
            {
                instance = new CShellLink();
                ((System.Runtime.InteropServices.ComTypes.IPersistFile)instance).Load(link, STGM_READ);
                StringBuilder path = new StringBuilder(32768);
                ((IShellLinkW)instance).GetPath(path, path.Capacity, IntPtr.Zero, 0);
                return path.ToString();
            }
            catch (Exception shellLinkError)
            {
                // once a run: uninstall reads every shortcut on the desktop
                if (!loggedReadFailure)
                {
                    loggedReadFailure = true;
                    SetupDiagnostics.Info("IShellLink could not read '" + link + "' (" + shellLinkError.Message + "); trying WScript.Shell.");
                }
                return TargetWithScriptHost(link);
            }
            finally
            {
                if (instance != null && Marshal.IsComObject(instance)) Marshal.FinalReleaseComObject(instance);
            }
        }

        private static bool TryCreateWithShellLink(string link, string target, string workingDirectory, string description,
            string iconPath, int iconIndex, out Exception error)
        {
            error = null;
            object instance = null;
            try
            {
                instance = new CShellLink();
                IShellLinkW shellLink = (IShellLinkW)instance;
                shellLink.SetPath(target);
                shellLink.SetWorkingDirectory(workingDirectory);
                shellLink.SetDescription(description);
                shellLink.SetIconLocation(iconPath, iconIndex);
                ((System.Runtime.InteropServices.ComTypes.IPersistFile)instance).Save(link, true);
                return true;
            }
            catch (Exception ex)
            {
                error = ex;
                return false;
            }
            finally
            {
                if (instance != null && Marshal.IsComObject(instance)) Marshal.FinalReleaseComObject(instance);
            }
        }

        private static void CreateWithScriptHost(string link, string target, string workingDirectory, string description,
            string iconLocation)
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType == null) throw new InvalidOperationException(InstallerText.T("Windows no pudo crear un acceso directo."));
            object shell = Activator.CreateInstance(shellType);
            object shortcut = null;
            try
            {
                shortcut = shellType.InvokeMember("CreateShortcut", BindingFlags.InvokeMethod, null, shell, new object[] { link });
                Type shortcutType = shortcut.GetType();
                shortcutType.InvokeMember("TargetPath", BindingFlags.SetProperty, null, shortcut, new object[] { target });
                shortcutType.InvokeMember("WorkingDirectory", BindingFlags.SetProperty, null, shortcut, new object[] { workingDirectory });
                shortcutType.InvokeMember("Description", BindingFlags.SetProperty, null, shortcut, new object[] { description });
                shortcutType.InvokeMember("IconLocation", BindingFlags.SetProperty, null, shortcut, new object[] { iconLocation });
                shortcutType.InvokeMember("Save", BindingFlags.InvokeMethod, null, shortcut, null);
            }
            finally
            {
                if (shortcut != null && Marshal.IsComObject(shortcut)) Marshal.FinalReleaseComObject(shortcut);
                if (Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
            }
        }

        private static string TargetWithScriptHost(string link)
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
                if (shortcut != null && Marshal.IsComObject(shortcut)) Marshal.FinalReleaseComObject(shortcut);
                if (Marshal.IsComObject(shell)) Marshal.FinalReleaseComObject(shell);
            }
        }
    }

    // Wine and Proton: a Windows program on Linux. They have no elevation
    // boundary to keep MO2 out of, and their shell doesn't take the calls
    // the setup makes to start MO2 unelevated on Windows.
    internal static class WineHost
    {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandle(string module);

        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);

        private static bool? running;

        public static bool IsRunning
        {
            get
            {
                if (!running.HasValue)
                {
                    try
                    {
                        IntPtr ntdll = GetModuleHandle("ntdll.dll");
                        running = ntdll != IntPtr.Zero && GetProcAddress(ntdll, "wine_get_version") != IntPtr.Zero;
                    }
                    catch { running = false; }
                }
                return running.Value;
            }
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
            // Wine has no administrator boundary to keep MO2 out of: it starts
            // like any program
            if (WineHost.IsRunning)
            {
                try
                {
                    ProcessStartInfo start = new ProcessStartInfo(path);
                    start.WorkingDirectory = directory;
                    start.UseShellExecute = false;
                    Process.Start(start);
                    SetupDiagnostics.Info("Started MO2 directly under Wine.");
                    return true;
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not start MO2 under Wine.", ex);
                    return false;
                }
            }
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

        // a folder in Explorer, or a setup log in its editor, opened by the
        // desktop shell with the user's normal token; nothing else
        public static bool OpenPath(string requested)
        {
            string path;
            try { path = Path.GetFullPath(requested); }
            catch { return false; }
            bool folder = Directory.Exists(path);
            bool log = !folder && File.Exists(path) && String.Equals(Path.GetExtension(path), ".log", StringComparison.OrdinalIgnoreCase);
            if (!folder && !log) return false;
            Exception shellError;
            if (TryShellExecute(path, folder ? path : Path.GetDirectoryName(path), out shellError)) return true;
            if (shellError != null) SetupDiagnostics.Error("The desktop shell could not open '" + path + "'.", shellError);
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
