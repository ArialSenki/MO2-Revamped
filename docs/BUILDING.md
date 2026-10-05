# Build guidance

This repository is a consolidated source snapshot of multiple components, not
a preconfigured, one-command build checkout. Build steps and prerequisites can
vary with the selected component and Windows toolchain.

## Mod Organizer 2

Use the official [Mod Organizer 2 build project (`mob`)](https://github.com/ModOrganizer2/mob)
as the starting point for preparing a Windows development environment. Point
that environment at the matching MO2 source in `source/modorganizer/` and the
component sources in this repository. Follow the upstream build instructions
for the required compiler, Qt, and dependency versions.

## USVFS

USVFS source is in `source/usvfs/`. Its upstream README and CMake files contain
the component-specific build options and dependency requirements. The source
snapshot corresponds to USVFS v0.5.7.2; do not assume another revision is
binary-compatible with this MO2 source.

## Revamped components

Revamped plugins, themes, the native bridge source, and installer source are in
`source/revamped/`. The Elden Ring native bridge was prepared with Zig 0.13.0;
the installer source targets C# 5 and .NET Framework. Inspect the project files
for the exact build entry points before building those components.

## Source-only repository

The repository intentionally does not publish the MO2 installer or treat a
checked-in runtime binary as a build result. Build outputs, personal MO2 data,
profiles, downloads, mods, saves, and logs should remain outside the source
tree. A successful clean-clone build has not yet been verified.
