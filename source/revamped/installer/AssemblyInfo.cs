using System.Reflection;
using System.Runtime.InteropServices;

[assembly: AssemblyTitle("MO2 Revamped Setup")]
[assembly: AssemblyDescription("Instalador de Mod Organizer 2: Revamped 1.1.0 beta 15")]
[assembly: AssemblyCompany("ArialSenki")]
[assembly: AssemblyProduct("Mod Organizer 2: Revamped")]
[assembly: AssemblyCopyright("Copyright © 2026 ArialSenki")]
[assembly: AssemblyVersion("1.1.0.0")]
[assembly: AssemblyFileVersion("1.1.0.0")]
// the release name; Build-Release.ps1 checks it against its own version
[assembly: AssemblyInformationalVersion("1.1.0-beta.15")]
[assembly: ComVisible(false)]
// the setup runs as administrator from Downloads: a DLL dropped next to it
// (wtsapi32.dll, rstrtmgr.dll, wintrust.dll) must never be loaded instead
// of Windows' own
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
