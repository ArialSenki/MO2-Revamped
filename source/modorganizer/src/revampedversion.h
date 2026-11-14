#ifndef REVAMPEDVERSION_H
#define REVAMPEDVERSION_H

// The MO2 Revamped release this build belongs to, as tagged on GitHub without the
// "v". ModOrganizer.exe keeps the version of the MO2 it is based on (2.5.2) in its
// file version, so the update check compares GitHub releases with this instead.
// Build-Release.ps1 refuses to package a build whose version differs from it.
#define MO2R_VERSION_STRING "1.1.0-beta.10"

// the MO2 Revamped listing on Nexus, under Elden Ring: the update notice links
// to it and the endorsement goes to it
#define MO2R_NEXUS_MOD_ID 11314

#endif  // REVAMPEDVERSION_H
