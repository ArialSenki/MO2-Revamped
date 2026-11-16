from typing import TypeGuard

import mobase


def is_directory(entry: mobase.FileTreeEntry) -> TypeGuard[mobase.IFileTree]:
    return entry.isDir()


# MO2 Revamped: Root Builder's plugin name is "RootBuilder" ("Root Builder" is
# its display name, kept in case a version names itself so)
ROOT_BUILDER_NAMES = ("RootBuilder", "Root Builder")


def root_builder_enabled(organizer: mobase.IOrganizer) -> bool:
    """Whether Root Builder deploys the mods' Root/ folders itself; without it,
    MO2 Revamped maps them to the game folder (OrganizerCore::fileMapping)."""
    return any(organizer.isPluginEnabled(name) for name in ROOT_BUILDER_NAMES)
