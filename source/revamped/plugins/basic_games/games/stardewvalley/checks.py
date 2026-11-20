"""Stardew Valley checks behind the problem report: SMAPI missing or too old,
dependencies missing or too old, and mods SMAPI skips as duplicates."""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import mobase

from . import manifest as manifests
from .content import mod_folders


GAME_FOLDER = ""  # the owner name of the mods in the game's own Mods folder


@dataclass
class ModManifests:
    name: str  # the MO2 mod (GAME_FOLDER for the game's own Mods folder)
    manifests: list[manifests.Manifest]
    # each manifest's folder, relative to the game's Mods folder (as SMAPI
    # sees it through the VFS): two MO2 mods with the same folder are one
    folders: list[str] = field(default_factory=list)


@dataclass
class Report:
    # mods with manifests while SMAPI isn't in the game folder
    no_smapi: list[str] = field(default_factory=list)
    # missing dependency (casefolded UniqueID) -> the MO2 mods that need it
    missing: dict[str, list[str]] = field(default_factory=dict)
    # (MO2 mod, what it needs, the version there, the version it needs)
    too_old: list[tuple[str, str, str, str]] = field(default_factory=list)
    # (MO2 mod, the SMAPI version it needs, the SMAPI version there)
    smapi_too_old: list[tuple[str, str, str]] = field(default_factory=list)
    # UniqueID -> the MO2 mods that both have it (SMAPI loads neither)
    duplicates: dict[str, list[str]] = field(default_factory=dict)


class ManifestCache:
    """Each manifest read once while its file stays the same."""

    def __init__(self) -> None:
        self._cache: dict[str, tuple[tuple[int, int], manifests.Manifest | None]] = {}

    def read(self, path: Path) -> manifests.Manifest | None:
        try:
            info = path.stat()
        except OSError:
            return None
        stamp = (info.st_mtime_ns, info.st_size)
        key = str(path).casefold()
        cached = self._cache.get(key)
        if cached is not None and cached[0] == stamp:
            return cached[1]
        result = manifests.read(path)
        self._cache[key] = (stamp, result)
        return result


def mod_manifests(name: str, folder: Path, tree: mobase.IFileTree, cache: ManifestCache) -> ModManifests:
    found, folders = [], []
    for mod_folder in mod_folders(tree):
        relative = mod_folder.path("/")
        parsed = cache.read(folder / relative / "manifest.json")
        if parsed is not None:
            found.append(parsed)
            folders.append(relative.casefold())
    return ModManifests(name, found, folders)


def game_folder_manifests(game_mods: Path, cache: ManifestCache) -> ModManifests:
    """The mods in the game's own Mods folder (SMAPI's bundled ones, and any
    put there by hand)."""
    found, relative = [], []
    try:
        folders = [p for p in game_mods.iterdir() if p.is_dir() and not p.name.startswith(".")]
    except OSError:
        folders = []
    for folder in folders:
        parsed = cache.read(folder / "manifest.json")
        if parsed is not None:
            found.append(parsed)
            relative.append(folder.name.casefold())
    return ModManifests(GAME_FOLDER, found, relative)


def display_name(unique_id: str, known: dict[str, manifests.Manifest]) -> str:
    if unique_id in manifests.KNOWN:
        return manifests.KNOWN[unique_id][0]
    if unique_id in known:
        return known[unique_id].name
    return unique_id


def analyze(mods: list[ModManifests], game: ModManifests | None, smapi_version: str | None) -> Report:
    """mods: the active MO2 mods, lowest priority first. smapi_version: the
    installed SMAPI's version, None when SMAPI isn't installed."""
    report = Report()
    provided: dict[str, manifests.Manifest] = {}
    # UniqueID -> folder -> who has it there (the highest MO2 mod, which the
    # VFS shows for that folder, or the game's Mods folder)
    places: dict[str, dict[str, str]] = {}
    for mod in ([game] if game else []) + mods:
        folders = mod.folders or [""] * len(mod.manifests)
        for manifest, folder in zip(mod.manifests, folders):
            provided[manifest.unique_id] = manifest
            places.setdefault(manifest.unique_id, {})[folder] = mod.name
    # the same mod in two folders SMAPI sees: it loads neither
    report.duplicates = {
        uid: sorted(set(owners.values()), key=lambda n: (n != GAME_FOLDER, n))
        for uid, owners in places.items()
        if len(owners) > 1
    }

    for mod in mods:
        if mod.manifests and smapi_version is None:
            report.no_smapi.append(mod.name)
        for manifest in mod.manifests:
            if smapi_version and manifests.older(smapi_version, manifest.minimum_api):
                report.smapi_too_old.append((mod.name, manifest.minimum_api, smapi_version))
            for need in manifest.needs():
                if need.unique_id == "smapi" or need.unique_id in manifests.BUNDLED and smapi_version:
                    continue
                there = provided.get(need.unique_id)
                if there is None:
                    users = report.missing.setdefault(need.unique_id, [])
                    if mod.name not in users:
                        users.append(mod.name)
                elif manifests.older(there.version, need.minimum):
                    report.too_old.append((mod.name, there.name, there.version, need.minimum))
            for maybe in manifest.optional():
                there = provided.get(maybe.unique_id)
                if there is not None and manifests.older(there.version, maybe.minimum):
                    report.too_old.append((mod.name, there.name, there.version, maybe.minimum))
    return report
