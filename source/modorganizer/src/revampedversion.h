#ifndef REVAMPEDVERSION_H
#define REVAMPEDVERSION_H

// The MO2 Revamped release this build belongs to, as tagged on GitHub without the
// "v". ModOrganizer.exe keeps the version of the MO2 it is based on (2.5.2) in its
// file version, so the update check compares GitHub releases with this instead.
// Build-Release.ps1 refuses to package a build whose version differs from it.
#define MO2R_VERSION_STRING "1.1.0-beta.13"

// the MO2 Revamped listing on Nexus, in Modding Tools ("site"): the endorse
// button endorses it. The old Elden Ring page (11314) is under moderation; an
// endorsement made there before still counts as the user's decision
#define MO2R_NEXUS_DOMAIN "site"
#define MO2R_NEXUS_MOD_ID 2612
#define MO2R_NEXUS_OLD_DOMAIN "eldenring"
#define MO2R_NEXUS_OLD_MOD_ID 11314

// the application name Nexus registered for MO2 Revamped's sign-in (SSO). Empty
// until Nexus registers it: signing in through Nexus would ask for access in
// the original Mod Organizer 2's name, so Settings offers the personal API key
// instead. Set it to the slug Nexus gives to bring "Connect to Nexus" back
#define MO2R_NEXUS_SSO_SLUG ""

// the page in Nexus' Modding Tools section, where moderators asked multi-game
// tools to live: the update notice links to it
#define MO2R_NEXUS_PAGE_URL "https://www.nexusmods.com/site/mods/2612"

#endif  // REVAMPEDVERSION_H
