#pragma once

// Startup phase names and event names must be string literals so they remain
// valid if an exception is raised while startup diagnostics are being written.
void setStartupDiagnosticPhase(const char* phase) noexcept;
void writeStartupDiagnosticEvent(const char* event) noexcept;
