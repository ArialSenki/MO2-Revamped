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
