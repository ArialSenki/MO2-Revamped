"""The load order: which modules the active mods bring, in MO2's order, what
they need, and writing modsettings.lsx without ever losing an order.

Nothing here shows a window; launch.py does that. Everything here can run in
a worker thread except where noted.
"""

from __future__ import annotations

import os
import threading
import time
from pathlib import Path
from typing import Any, Callable

from . import modsettings
from .paks import (
    BASE_MODULE_UUIDS,
    DamagedPak,
    NeedsLSLib,
    PakCache,
    forced_in_load_order,
    is_builtin,
    is_part,
    scan_mod_paks,
    version64_text,
)
from .util import (
    LOOSE_FILE_FOLDERS,
    PAK_FILES_FOLDER,
    SE_CONFIG_FOLDER,
    TOP_LEVEL_DATA_FOLDERS,
    log_debug,
    log_info,
    log_warning,
    subfolders,
    write_atomically,
)

BACKUPS_KEPT = 10

# a mod's top-level folders that are mapped as they are, never packed
_not_packable = {
    f.casefold()
    for f in TOP_LEVEL_DATA_FOLDERS
    | {"Data", "Script Extender", "bin", "Root", SE_CONFIG_FOLDER, PAK_FILES_FOLDER}
}


class Canceled(Exception):
    pass


class ModEntry:
    """One active mod and what it brings to the load order."""

    def __init__(self, name: str, path: Path):
        self.name = name
        self.path = path
        self.paks: list[Path] = []
        self.modules: list[dict[str, Any]] = []
        self.overrides: list[str] = []
        self.needs_se = False
        self.stale: list[str] = []  # paks read on an earlier start (unreadable now)
        self.failed: list[tuple[str, str]] = []  # (pak, why)
        self.needs_lslib: list[str] = []
        self.workspaces: list[Path] = []
        self.unread: list[Path] = []  # not in the cache yet (cached_only)


def is_workspace(folder: Path) -> bool:
    """An unpacked module: <folder>/Mods/<x>/meta.lsx."""
    return any((sub / "meta.lsx").is_file() for sub in subfolders(folder / "Mods"))


def packable_folders(modpath: Path) -> list[Path]:
    """A mod's folders that hold an unpacked module (a "mod workspace"),
    which the game only loads packed. A folder with only the game's folders
    (Public/, Generated/...) and no meta.lsx is loose files in a wrapper
    folder: the mapper sends it to Data, nothing is packed."""
    return [
        folder
        for folder in subfolders(modpath)
        if folder.name.casefold() not in _not_packable and is_workspace(folder)
    ]


def read_mods(
    mods: list[tuple[str, Path]],
    cache: PakCache,
    divine: Callable[[Path], tuple[list[str], bytes | None]] | None,
    autobuild: bool = False,
    canceled: Callable[[], bool] = lambda: False,
    progress: Callable[[int], None] = lambda done: None,
    cached_only: bool = False,
) -> list[ModEntry]:
    """Reads the active mods' paks (cached ones cost a stat each). Raises
    Canceled between paks if canceled() says so; the cache keeps what was
    read, nothing else changes."""
    entries: list[ModEntry] = []
    for index, (name, path) in enumerate(mods):
        if canceled():
            raise Canceled()
        entry = ModEntry(name, path)
        entry.paks = scan_mod_paks(path)
        for pak in entry.paks:
            if canceled():
                raise Canceled()
            if is_part(pak):
                continue
            if cached_only:
                known = cache.get(pak) or cache.stale(pak)
                if known is None:
                    entry.unread.append(pak)
                    continue
            try:
                info = known if cached_only else cache.info(pak, divine)
            except NeedsLSLib:
                entry.needs_lslib.append(pak.name)
                info = cache.stale(pak)
                if info is None:
                    continue
                entry.stale.append(pak.name)
            except (DamagedPak, OSError, ValueError) as e:
                info = cache.stale(pak)
                if info is None:
                    log_warning(f"couldn't read {pak} ({name}): {e}")
                    entry.failed.append((pak.name, str(e)))
                    continue
                log_warning(f"couldn't read {pak} ({name}), using what an earlier start read: {e}")
                entry.stale.append(pak.name)
            entry.needs_se |= bool(info.get("needs_se"))
            module = info.get("module")
            in_order = info.get("kind") == "module" or (
                module is not None and forced_in_load_order(path, pak)
            )
            if in_order and module:
                entry.modules.append(dict(module, mod=name, pak=pak.name))
            else:
                entry.overrides.append(pak.name)
        if autobuild:
            entry.workspaces = packable_folders(path)
        entries.append(entry)
        progress(index + 1)
    return entries


class Plan:
    """What the next modsettings.lsx holds and what's wrong with it."""

    def __init__(self) -> None:
        self.base: list[dict[str, Any]] = []
        self.modules: list[dict[str, Any]] = []
        self.external: list[dict[str, Any]] = []
        self.version = modsettings.DEFAULT_VERSION
        self.duplicates: list[tuple[str, str, str]] = []  # (module, dropped mod, kept mod)
        self.missing: list[tuple[str, str]] = []  # (mod, dependency)
        self.late: list[tuple[str, str, str]] = []  # (mod, dependency, its mod)
        # the same as MO2 mod names: (mod, the mod with what it needs)
        self.late_mods: list[tuple[str, str]] = []
        self.older: list[tuple[str, str, str, str]] = []  # (mod, dep, needed, found)
        self.conflicts: list[tuple[str, str]] = []
        self.left_out: list[tuple[str, str]] = []  # (mod, why)
        self.stale: list[str] = []
        self.needs_lslib: list[tuple[str, str]] = []
        self.needs_se: list[str] = []

    def all_modules(self) -> list[dict[str, Any]]:
        return self.base + self.modules + self.external

    def text(self) -> str:
        return modsettings.render(self.all_modules(), self.version)

    def problems(self) -> bool:
        return bool(self.missing or self.late or self.left_out or self.needs_lslib)


def make_plan(
    entries: list[ModEntry],
    previous: modsettings.ModSettings | None,
    game_written: modsettings.ModSettings | None = None,
    external_uuids: set[str] | None = None,
    keep_external: bool = True,
    has_script_extender: bool = True,
) -> Plan:
    """The load order from the mods in MO2's order (lowest priority first,
    so the highest loads last and wins), keeping:
    - the base module the game last wrote (GustavX; GustavDev before Patch 7);
    - the file's version node as the game last wrote it;
    - mods enabled outside MO2 (the in-game mod manager's mod.io mods, paks put
      in the documents' Mods folder by hand) when their pak is there, after
      MO2's, in their earlier order."""
    plan = Plan()
    plan.version = modsettings.newest_version(
        previous.version if previous else None,
        game_written.version if game_written else None,
    )
    plan.base = (previous.base if previous else []) or [dict(modsettings.GUSTAVX)]
    plan.base = plan.base[:1]

    by_uuid: dict[str, dict[str, Any]] = {}
    for entry in entries:
        for module in entry.modules:
            uuid = module["uuid"].casefold()
            if uuid in BASE_MODULE_UUIDS:
                continue
            earlier = by_uuid.get(uuid)
            if earlier is not None:
                # two mods with one module (two versions installed): the
                # higher priority one wins, in its place
                plan.duplicates.append((module["name"], earlier["mod"], entry.name))
                plan.modules.remove(earlier)
            by_uuid[uuid] = module
            plan.modules.append(module)
        for pak, why in entry.failed:
            plan.left_out.append((entry.name, f"{pak}: {why}"))
        for pak in entry.needs_lslib:
            if pak not in entry.stale:
                plan.needs_lslib.append((entry.name, pak))
        plan.stale.extend(f"{entry.name}: {p}" for p in entry.stale)
        if entry.needs_se:
            plan.needs_se.append(entry.name)
    if has_script_extender:
        plan.needs_se = []

    if keep_external and previous is not None:
        external_uuids = {u.casefold() for u in (external_uuids or set())}
        for module in previous.mods:
            uuid = module["uuid"].casefold()
            if uuid in by_uuid or is_builtin(module):
                continue
            # only with its pak there: an entry the game can't find can make
            # it reset the whole list (what Vortex reports as "reset")
            if uuid in external_uuids:
                plan.external.append(dict(module, mod=""))
                by_uuid[uuid] = plan.external[-1]

    index = {m["uuid"].casefold(): i for i, m in enumerate(plan.modules)}
    names = {m["uuid"].casefold(): m for m in plan.modules + plan.external}
    for i, module in enumerate(plan.modules):
        for dep in module.get("dependencies") or ():
            uuid = dep["uuid"].casefold()
            if uuid == module["uuid"].casefold() or is_builtin(dep):
                continue
            label = dep.get("name") or dep.get("folder") or dep["uuid"]
            found = names.get(uuid)
            if found is None:
                plan.missing.append((_mod_label(module), label))
                continue
            if uuid in index and index[uuid] > i:
                plan.late.append((_mod_label(module), label, found.get("mod") or ""))
                pair = (module.get("mod") or "", found.get("mod") or "")
                if all(pair) and pair[0] != pair[1] and pair not in plan.late_mods:
                    plan.late_mods.append(pair)
            need, have = int(dep.get("version64") or 0), int(found.get("version64") or 0)
            if need and have and have < need:
                plan.older.append(
                    (_mod_label(module), label, version64_text(need), version64_text(have))
                )
        for other in module.get("conflicts") or ():
            if other["uuid"].casefold() in names:
                plan.conflicts.append(
                    (_mod_label(module), other.get("name") or other["uuid"])
                )
    return plan


def fix_order(
    pairs: list[tuple[str, str]],
    priority: Callable[[str], int],
    set_priority: Callable[[str, int], Any],
    rounds: int = 200,
) -> list[str]:
    """Moves each needed mod just above the mod that needs it (MO2's
    setPriority to the needing mod's place pushes that one down by one),
    until every pair is in order. Only the needed mods move, and only up;
    a cycle (A needs B, B needs A) is left as it is. Returns the mods moved,
    in order."""
    moved: list[str] = []
    for _ in range(rounds):
        late = [(m, d) for m, d in pairs if priority(d) > priority(m)]
        if not late:
            break
        mod, dep = min(late, key=lambda pair: priority(pair[0]))
        if (dep, mod) in pairs:  # a cycle: nothing to do for this pair
            pairs = [p for p in pairs if p not in ((mod, dep), (dep, mod))]
            continue
        before = priority(dep)
        set_priority(dep, priority(mod))
        if priority(dep) == before:
            # MO2 didn't move it (a locked mod): leave this pair
            pairs = [p for p in pairs if p != (mod, dep)]
            continue
        moved.append(dep)
    return moved


def _mod_label(module: dict[str, Any]) -> str:
    mod = module.get("mod") or ""
    name = module.get("name") or module.get("folder") or module.get("uuid")
    return f"{mod} ({name})" if mod and mod != name else str(name)


# ----------------------------------------------------------------- writing


def write(profile_file: Path, text: str, backup_dir: Path) -> bool:
    """Writes the load order if it changed. The file it replaces, if it held
    another order (the game, BG3 Mod Manager or the user changed it), is kept
    in backup_dir first; the last BACKUPS_KEPT stay. Written whole under
    another name and swapped in, so a crash never leaves half a file.
    Returns whether the file was written."""
    data = text.encode("utf-8")
    try:
        old = profile_file.read_bytes()
    except OSError:
        old = None
    if old == data:
        return False
    if old and old.strip():
        try:
            backup_dir.mkdir(parents=True, exist_ok=True)
            stamp = time.strftime("%Y%m%d-%H%M%S")
            target = backup_dir / f"modsettings-{stamp}.lsx"
            n = 1
            while target.exists():
                n += 1
                target = backup_dir / f"modsettings-{stamp}-{n}.lsx"
            write_atomically(target, old)
            backups = sorted(backup_dir.glob("modsettings-*.lsx"), key=os.path.getmtime)
            for stale in backups[:-BACKUPS_KEPT]:
                stale.unlink(missing_ok=True)
        except OSError as e:
            log_warning(f"couldn't back up {profile_file}: {e}")
    write_atomically(profile_file, data)
    log_info(f"load order written to {profile_file}")
    return True


def dropped_by_game(written: list[str], now: modsettings.ModSettings | None) -> list[str]:
    """The UUIDs MO2 wrote that the game's own copy no longer has (it turns
    off mods with a missing dependency, or all of them after a crash it
    blames on mods)."""
    if now is None:
        return []
    still = set(now.uuids())
    return [u for u in written if u not in still]


# ---------------------------------------------------------- mod workspaces


def changed_since(folder: Path, mtime: float) -> bool:
    """Whether a file in the folder is newer than the pak built from it (a
    file deleted or renamed changes its folder's time)."""
    for root, _dirs, files in os.walk(folder):
        try:
            if os.path.getmtime(root) > mtime:
                return True
            for f in files:
                if os.path.getmtime(os.path.join(root, f)) > mtime:
                    return True
        except OSError:
            return True
    return False


def workspace_pak(overwrite: Path, mod_name: str, folder: Path) -> Path:
    return overwrite / "Mods" / f"{mod_name.replace(' ', '_')}_{folder.name}.pak"


class Worker(threading.Thread):
    """read_mods in a thread, so the progress window paints and its Cancel
    button works while a big pak is read."""

    def __init__(self, target: Callable[[], Any]):
        super().__init__(daemon=True)
        self._target_fn = target
        self.result: Any = None
        self.error: BaseException | None = None

    def run(self) -> None:
        try:
            self.result = self._target_fn()
        except BaseException as e:  # noqa: BLE001 - handed to the main thread
            self.error = e


def debug_summary(plan: Plan) -> None:
    log_debug(
        f"load order: {len(plan.modules)} modules, {len(plan.external)} kept from "
        f"outside MO2, {len(plan.missing)} missing dependencies"
    )
