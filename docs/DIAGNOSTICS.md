# Startup diagnostics

MO2 Revamped keeps the detailed startup and memory-exception diagnostics off by
default. The normal launch path therefore does not install the diagnostic
exception handlers or write and flush a diagnostic file at each startup step.

To collect these logs for a support session, set this environment variable for
the MO2 process and start it again:

```text
MO2_REVAMPED_DIAGNOSTICS=1
```

The logs are written beside `ModOrganizer.exe` under
`logs\memory_diagnostics.log` and `logs\startup_diagnostics.log`. The memory
log records exception codes, addresses, registers, process and thread IDs, and
the current startup phase. Review both files before sharing them because they
can include local paths and machine-specific details. Remove the environment
variable or set it to another value to return to the default behavior.

Under Wine and Proton (Linux, Steam Deck) these logs are on by default, because
reports from there often had no file at all: MO2 had stopped before its normal
log starts. Set `MO2_REVAMPED_DIAGNOSTICS=0` to turn them off. If the folder
beside `ModOrganizer.exe` can't be written, the files go to `%TEMP%` as
`ModOrganizer-startup_diagnostics.log` and `ModOrganizer-memory_diagnostics.log`.
Once a file passes 1 MiB, it is renamed to `<name>.old`, replacing any older
one, and a new file starts.

If not even `startup_diagnostics.log` appears, MO2 stopped before its first
line of code, almost always because Windows (or Wine) could not load a DLL it
needs, such as the Visual C++ runtime. Under Proton, `PROTON_LOG=1` writes
`steam-<appid>.log` to the home folder; look for `err:module` lines there.

## Testing the update notice

The update check reads a local file instead of GitHub when this variable names
one:

```text
MO2_REVAMPED_UPDATE_TEST=C:\path\to\releases.json
```

The file has the format of GitHub's releases list
(`https://api.github.com/repos/ArialSenki/MO2-Revamped/releases`): an array of
objects with `tag_name`, `name`, `body`, `html_url`, `published_at`,
`prerelease` and `draft`. Add a release with a higher tag to see the notice
without publishing anything. The log says when the file is used.
