using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using Microsoft.Win32;

namespace Mo2RevampedSetup
{
    // MO2, its plugins, LOOT and usvfs need the Microsoft Visual C++ 2015-2022
    // runtime installed in Windows. usvfs is loaded into the game, so a copy next
    // to MO2 doesn't help: 64-bit games need the x64 runtime and 32-bit ones
    // (Fallout 3, New Vegas, Oblivion...) the x86 one. When one is missing or
    // older than what the binaries were built with, Microsoft's own installer
    // is downloaded, its Microsoft signature checked, and run.
    internal static class VcRuntime
    {
        // the newest MSVC toolset the shipped binaries are built with: a runtime
        // older than the toolset may not run them (MSVC 14.44, VS 2022 17.14)
        internal static readonly Version Required = new Version(14, 44, 0);

        // Microsoft's permanent links to the latest supported redistributables
        internal const string LinkX64 = "https://aka.ms/vc14/vc_redist.x64.exe";
        internal const string LinkX86 = "https://aka.ms/vc14/vc_redist.x86.exe";

        private const int DownloadTimeoutMs = 120000;
        private const int InstallTimeoutMs = 600000;

        // the installed runtime of one architecture ("x64" or "x86"), or null
        public static Version InstalledVersion(string architecture)
        {
            Version best = null;
            foreach (RegistryView view in new RegistryView[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                try
                {
                    using (RegistryKey root = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    using (RegistryKey key = root.OpenSubKey(@"SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\" + architecture, false))
                    {
                        if (key == null) continue;
                        if (Convert.ToInt32(key.GetValue("Installed", 0)) != 1) continue;
                        Version version = new Version(
                            Convert.ToInt32(key.GetValue("Major", 0)),
                            Convert.ToInt32(key.GetValue("Minor", 0)),
                            Convert.ToInt32(key.GetValue("Bld", 0)));
                        if (best == null || version > best) best = version;
                    }
                }
                catch (Exception ex)
                {
                    SetupDiagnostics.Error("Could not read the Visual C++ " + architecture + " runtime version.", ex);
                }
            }
            return best;
        }

        public static bool IsCurrent(string architecture)
        {
            Version version = InstalledVersion(architecture);
            return version != null && version >= Required;
        }

        // Installs what is missing. Returns null when the runtime is ready (or
        // only a restart is left, then restartNeeded is set), otherwise the
        // text to show the user. Never throws: MO2 itself is already installed.
        public static string Ensure(bool needX86, Action<string> report, out bool restartNeeded)
        {
            restartNeeded = false;
            List<string> missing = new List<string>();
            foreach (string architecture in needX86 ? new string[] { "x64", "x86" } : new string[] { "x64" })
            {
                Version version = InstalledVersion(architecture);
                SetupDiagnostics.Info("Visual C++ " + architecture + " runtime: " +
                    (version == null ? "not installed" : version.ToString()) + ", needed " + Required + ".");
                if (version == null || version < Required) missing.Add(architecture);
            }
            if (missing.Count == 0) return null;

            // the setup runs elevated: the installer it runs must sit where only
            // administrators can write, not in the user's %TEMP%, where another
            // program of the user could swap it after the signature check
            string folder = null;
            List<string> failed = new List<string>();
            string reason = null;
            try
            {
                folder = InstallerBackend.CreateWorkFolder();
                // after a timeout (the installer still running) or a network
                // that doesn't answer, the other architecture is not tried: it
                // would only fail the same way, or wait minutes more
                bool stop = false;
                foreach (string architecture in missing)
                {
                    if (stop)
                    {
                        failed.Add(architecture);
                        continue;
                    }
                    try
                    {
                        if (report != null)
                            report(String.Format(InstallerText.T("Descargando Microsoft Visual C++ ({0})…"), architecture));
                        string installer;
                        try { installer = Download(architecture, folder); }
                        catch (WebException ex)
                        {
                            if (ex.Status == WebExceptionStatus.ConnectFailure ||
                                ex.Status == WebExceptionStatus.NameResolutionFailure ||
                                ex.Status == WebExceptionStatus.Timeout)
                                stop = true;
                            throw;
                        }
                        Verify(installer);
                        if (report != null)
                            report(String.Format(InstallerText.T("Instalando Microsoft Visual C++ ({0})…"), architecture));
                        int exitCode = Run(installer);
                        // 1618: another installation (often Windows Update) is
                        // running; one more try after it had time to finish
                        if (exitCode == 1618)
                        {
                            SetupDiagnostics.Info("vc_redist." + architecture + ".exe: another installation is running, retrying.");
                            System.Threading.Thread.Sleep(30000);
                            exitCode = Run(installer);
                        }
                        SetupDiagnostics.Info("vc_redist." + architecture + ".exe finished with code " + exitCode + ".");
                        // 3010: installed, Windows wants a restart; 1638: a newer
                        // version was already there
                        bool restartThis = exitCode == 3010;
                        if (restartThis) restartNeeded = true;
                        else if (exitCode != 0 && exitCode != 1638)
                            throw new InvalidOperationException(String.Format(InstallerText.T("el instalador de Microsoft terminó con el código {0}"), exitCode));
                        if (!restartThis && !IsCurrent(architecture))
                            throw new InvalidOperationException(InstallerText.T("Windows no lo registra después de instalarlo"));
                    }
                    catch (TimeoutException ex)
                    {
                        SetupDiagnostics.Error("The Visual C++ " + architecture + " installer did not finish in time.", ex);
                        failed.Add(architecture);
                        if (reason == null) reason = ex.Message;
                        stop = true;
                    }
                    catch (Exception ex)
                    {
                        SetupDiagnostics.Error("Could not install the Visual C++ " + architecture + " runtime.", ex);
                        failed.Add(architecture);
                        if (reason == null) reason = ex.Message;
                    }
                }
            }
            catch (Exception ex)
            {
                SetupDiagnostics.Error("Could not prepare the Visual C++ runtime download.", ex);
                failed = missing;
                reason = ex.Message;
            }
            finally
            {
                // a timed-out installer may still hold its file: the work folder
                // then stays, and the next setup removes it after a day
                if (folder != null)
                {
                    try { InstallerBackend.RemoveWorkFolder(folder); }
                    catch (Exception ex) { SetupDiagnostics.Error("Could not remove '" + folder + "'.", ex); }
                }
            }
            if (failed.Count == 0) return null;

            List<string> links = new List<string>();
            foreach (string architecture in failed)
                links.Add(architecture == "x86" ? LinkX86 : LinkX64);
            return String.Format(
                InstallerText.T("No se pudo instalar Microsoft Visual C++ ({0}): {1}. MO2 lo necesita para arrancar y para cargar los mods en el juego. Instálalo desde la página de Microsoft: {2}"),
                String.Join(", ", failed.ToArray()), reason, String.Join("  ", links.ToArray()));
        }

        internal static string Download(string architecture, string folder)
        {
            string url = architecture == "x86" ? LinkX86 : LinkX64;
            string destination = Path.Combine(folder, "vc_redist." + architecture + ".exe");
            ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
            HttpWebRequest request = (HttpWebRequest)WebRequest.Create(url);
            request.Timeout = DownloadTimeoutMs;
            request.ReadWriteTimeout = DownloadTimeoutMs;
            request.AllowAutoRedirect = true;
            request.UserAgent = "MO2RevampedSetup";
            using (HttpWebResponse response = (HttpWebResponse)request.GetResponse())
            {
                if (response.StatusCode != HttpStatusCode.OK)
                    throw new InvalidOperationException(String.Format(InstallerText.T("la descarga respondió {0}"), (int)response.StatusCode));
                // a redirect must end at Microsoft's own download servers
                string host = response.ResponseUri.Host.ToLowerInvariant();
                if (response.ResponseUri.Scheme != Uri.UriSchemeHttps ||
                    !(host == "microsoft.com" || host.EndsWith(".microsoft.com")))
                    throw new InvalidOperationException(InstallerText.T("la descarga no vino de los servidores de Microsoft"));
                using (Stream input = response.GetResponseStream())
                using (FileStream output = new FileStream(destination, FileMode.Create, FileAccess.Write))
                    input.CopyTo(output);
            }
            SetupDiagnostics.Info("Downloaded " + url + " (" + new FileInfo(destination).Length + " bytes).");
            return destination;
        }

        // a valid Authenticode signature whose signer is Microsoft Corporation
        internal static void Verify(string path)
        {
            int trust = CheckSignature(path);
            if (trust != 0)
                throw new InvalidOperationException(String.Format(InstallerText.T("el archivo descargado no tiene una firma válida (0x{0:X8})"), trust));
            X509Certificate2 signer = new X509Certificate2(X509Certificate.CreateFromSignedFile(path));
            string subject = signer.Subject ?? "";
            // the exact organization and common name, not a substring that a
            // name like "Microsoft Corporation Ltd" or a quoted field would match
            if (!HasExactName(signer.SubjectName, "O=Microsoft Corporation") ||
                !String.Equals(signer.GetNameInfo(X509NameType.SimpleName, false), "Microsoft Corporation", StringComparison.Ordinal))
                throw new InvalidOperationException(InstallerText.T("el archivo descargado no está firmado por Microsoft"));
            FileVersionInfo info = FileVersionInfo.GetVersionInfo(path);
            if (info.ProductName == null || info.ProductName.IndexOf("Visual C++", StringComparison.OrdinalIgnoreCase) < 0)
                throw new InvalidOperationException(InstallerText.T("el archivo descargado no es el instalador de Visual C++"));
            SetupDiagnostics.Info("Verified " + Path.GetFileName(path) + ": " + subject + ", " + info.ProductName + " " + info.ProductVersion + ".");
        }

        private static bool HasExactName(X500DistinguishedName name, string expected)
        {
            string decoded = name.Decode(X500DistinguishedNameFlags.UseNewLines | X500DistinguishedNameFlags.DoNotUseQuotes);
            foreach (string part in decoded.Split(new char[] { '\r', '\n' }, StringSplitOptions.RemoveEmptyEntries))
                if (String.Equals(part.Trim(), expected, StringComparison.Ordinal)) return true;
            return false;
        }

        // a timed-out installer is left running (killing it in the middle of
        // an MSI is worse) and reported as a TimeoutException
        private static int Run(string installer)
        {
            ProcessStartInfo start = new ProcessStartInfo(installer, "/install /quiet /norestart");
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            using (Process process = Process.Start(start))
            {
                if (!process.WaitForExit(InstallTimeoutMs))
                    throw new TimeoutException(InstallerText.T("el instalador de Microsoft no terminó a tiempo"));
                return process.ExitCode;
            }
        }

        private static int CheckSignature(string path)
        {
            IntPtr fileInfoPtr = IntPtr.Zero;
            IntPtr dataPtr = IntPtr.Zero;
            WinTrustFileInfo fileInfo = new WinTrustFileInfo();
            fileInfo.StructSize = (uint)Marshal.SizeOf(typeof(WinTrustFileInfo));
            fileInfo.FilePath = path;
            try
            {
                fileInfoPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WinTrustFileInfo)));
                Marshal.StructureToPtr(fileInfo, fileInfoPtr, false);
                WinTrustData data = new WinTrustData();
                data.StructSize = (uint)Marshal.SizeOf(typeof(WinTrustData));
                data.UIChoice = 2;           // WTD_UI_NONE
                data.RevocationChecks = 0;   // WTD_REVOKE_NONE
                data.UnionChoice = 1;        // WTD_CHOICE_FILE
                data.FileInfoPtr = fileInfoPtr;
                data.StateAction = 0;        // WTD_STATEACTION_IGNORE
                data.ProvFlags = 0x00000080; // WTD_REVOCATION_CHECK_NONE
                dataPtr = Marshal.AllocHGlobal(Marshal.SizeOf(typeof(WinTrustData)));
                Marshal.StructureToPtr(data, dataPtr, false);
                Guid action = new Guid("00AAC56B-CD44-11d0-8CC2-00C04FC295EE"); // WINTRUST_ACTION_GENERIC_VERIFY_V2
                return WinVerifyTrust(new IntPtr(-1), ref action, dataPtr);
            }
            finally
            {
                if (dataPtr != IntPtr.Zero)
                {
                    Marshal.DestroyStructure(dataPtr, typeof(WinTrustData));
                    Marshal.FreeHGlobal(dataPtr);
                }
                if (fileInfoPtr != IntPtr.Zero)
                {
                    Marshal.DestroyStructure(fileInfoPtr, typeof(WinTrustFileInfo));
                    Marshal.FreeHGlobal(fileInfoPtr);
                }
            }
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustFileInfo
        {
            public uint StructSize;
            [MarshalAs(UnmanagedType.LPWStr)] public string FilePath;
            public IntPtr FileHandle;
            public IntPtr KnownSubject;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct WinTrustData
        {
            public uint StructSize;
            public IntPtr PolicyCallbackData;
            public IntPtr SIPClientData;
            public uint UIChoice;
            public uint RevocationChecks;
            public uint UnionChoice;
            public IntPtr FileInfoPtr;
            public uint StateAction;
            public IntPtr StateData;
            [MarshalAs(UnmanagedType.LPWStr)] public string URLReference;
            public uint ProvFlags;
            public uint UIContext;
            public IntPtr SignatureSettings;
        }

        [DllImport("wintrust.dll", ExactSpelling = true, SetLastError = false, CharSet = CharSet.Unicode)]
        private static extern int WinVerifyTrust(IntPtr hwnd, ref Guid action, IntPtr data);
    }
}
