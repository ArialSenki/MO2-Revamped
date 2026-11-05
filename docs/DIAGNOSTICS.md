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
