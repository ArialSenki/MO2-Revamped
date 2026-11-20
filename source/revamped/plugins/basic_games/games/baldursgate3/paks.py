"""What each .pak holds: its module (meta.lsx), its dependencies, whether it
needs the Script Extender, or that it only overrides the game's files.

A pak is read once. The result goes into a cache file in the plugin's data
folder, keyed by the pak's path, size and modification time, so a start with
nothing new reads no pak at all, and an updated pak is read again even when
it keeps its name (the old plugin kept its metadata in each mod's meta.ini by
file name only, and never read an updated pak again).

The pak index and meta.lsx are read in Python (lspk). Divine.exe (LSLib) is
only asked for packages that reader doesn't know.
"""

from __future__ import annotations

import json
import os
import re
import threading
from pathlib import Path
from typing import Any, Callable

from . import lspk
from .util import log_debug, log_info, log_warning, write_atomically

CACHE_VERSION = 1

# The modules every game start has; a dependency on them is always met.
# GustavX/GustavDev are the story's base module (GustavDev before Patch 7).
BUILTIN_MODULES = {
    "991c9c7a-fb80-40cb-8f0d-b92d4e80e9b1": "Gustav",
    "28ac9ce2-2aba-8cda-b3b5-6e922f71b6b8": "GustavDev",
    "cb555efe-2d9e-131f-8195-a89329d218ea": "GustavX",
    "ed539163-bb70-431b-96a7-f5b2eda5376b": "Shared",
    "3d0c5ff8-c95d-c907-ff3e-34b204f1c630": "SharedDev",
    "b77b6210-ac50-4cb1-a3d5-5702fb9c744c": "Honour",
    "767d0062-d82c-279c-e16b-dfee7fe94cdd": "HonourX",
    "e842840a-2449-588c-b0c4-22122cfce31b": "DiceSet_01",
    "b176a0ac-d79f-ed9d-5a87-5c2c80874e10": "DiceSet_02",
    "e0a4d990-7b9b-8fa9-d7c6-04017c6cf5b1": "DiceSet_03",
    "77a2155f-4b35-4f0c-e7ff-4338f91426a4": "DiceSet_04",
    "ee4989eb-aab8-968f-8674-812ea2f4bfd7": "DiceSet_05",
    "bf19bab4-4908-ef39-9065-ced469c0f877": "DiceSet_06",
    "b0d7e5ab-27d7-d6fb-c55f-cb63b34c3cdd": "DiceSet_07",
    "630daa32-70f8-3da5-41b9-154fe8410236": "MainUI",
    "e1ce736b-52e6-e713-e9e7-e6abbb15a198": "ModBrowser",
    "9dff4c3b-fda7-43de-a763-ce1383039999": "CrossplayUI",
    "ee5a55ff-eb38-0b27-c5b0-f358dc306d34": "PhotoMode",
    "f8ff5ba6-b7f8-4cf4-9e10-b4d3e48d5a7f": "FW3",
    "e5c9077e-1fca-4f24-b55d-464f512c98a8": "Engine",
}
BASE_MODULE_UUIDS = {
    "cb555efe-2d9e-131f-8195-a89329d218ea",  # GustavX (Patch 7 and later)
    "28ac9ce2-2aba-8cda-b3b5-6e922f71b6b8",  # GustavDev
}


_builtin_names = {n.casefold() for n in BUILTIN_MODULES.values()}
_builtin_name_re = re.compile(r"(DiceSet_\d+|Gustav\w*|Shared\w*|Honour\w*)", re.I)


def is_builtin(dep: dict[str, Any] | str) -> bool:
    """A module the game itself has (by UUID, or by its folder name for the
    ones a patch adds)."""
    if isinstance(dep, str):
        return dep.casefold() in BUILTIN_MODULES
    if str(dep.get("uuid", "")).casefold() in BUILTIN_MODULES:
        return True
    folder = str(dep.get("folder") or dep.get("name") or "")
    return folder.casefold() in _builtin_names or bool(
        _builtin_name_re.fullmatch(folder)
    )


class DamagedPak(Exception):
    """A pak that neither this plugin nor Divine could read."""


class NeedsLSLib(Exception):
    """A pak of a kind only Divine (LSLib) reads, and LSLib isn't there."""


_READ_ERRORS = (OSError, ValueError, lspk.Unsupported, OverflowError, MemoryError)

_se_config_re = re.compile(r"(^|/)Mods/[^/]+/ScriptExtender/Config\.json$", re.I)


def version64_text(value: int) -> str:
    """Version64 as major.minor.revision.build (BG3 Mod Manager's layout)."""
    return (
        f"{value >> 55}.{(value >> 47) & 0xFF}."
        f"{(value >> 31) & 0xFFFF}.{value & 0x7FFFFFFF}"
    )


def version32_to_64(value: int) -> int:
    """An old meta.lsx's 32-bit Version as the Version64 the game uses."""
    major, minor = value >> 28, (value >> 24) & 0xF
    revision, build = (value >> 16) & 0xFF, value & 0xFFFF
    return (major << 55) | (minor << 47) | (revision << 31) | build


def _int(text: str | None, default: int = 0) -> int:
    try:
        return int(str(text).strip())
    except (TypeError, ValueError):
        return default


# ------------------------------------------------------------------ meta.lsx


def _attrs(node) -> dict[str, str]:
    return {
        a.get("id", ""): a.get("value", "")
        for a in node.findall("attribute")
        if a.get("id")
    }


def _short_desc(attrs: dict[str, str]) -> dict[str, Any]:
    if "Version64" in attrs:
        version = _int(attrs.get("Version64"))
    elif "Version" in attrs:
        version = version32_to_64(_int(attrs.get("Version")))
    else:
        version = 0
    return {
        "uuid": attrs.get("UUID", "").strip(),
        "folder": attrs.get("Folder", ""),
        "name": attrs.get("Name", ""),
        "version64": version,
        "md5": attrs.get("MD5", ""),
        "publish_handle": _int(attrs.get("PublishHandle")),
    }


_attr_re = re.compile(
    r'<attribute\s+id="(?P<id>[^"]+)"[^>]*?\svalue="(?P<value>[^"]*)"', re.I
)


def parse_meta_lsx(data: bytes) -> dict[str, Any] | None:
    """The ModuleInfo and Dependencies of a meta.lsx, or None if it has no
    module. A file that isn't well-formed XML (an "&" left unescaped in a
    description happens) is read with a looser scan instead of dropping the
    mod from the load order."""
    from xml.etree import ElementTree

    text = data.decode("utf-8-sig", "replace")
    try:
        root = ElementTree.fromstring(text)
    except ElementTree.ParseError:
        return _parse_meta_loosely(text)
    info_node = root.find(".//node[@id='ModuleInfo']")
    if info_node is None:
        return None
    attrs = _attrs(info_node)
    if not attrs.get("UUID"):
        return None
    module = _short_desc(attrs)
    if not attrs.get("Version64") and not attrs.get("Version"):
        # some put the version only in PublishVersion
        publish = info_node.find(".//node[@id='PublishVersion']")
        if publish is not None:
            module["version64"] = _short_desc(_attrs(publish))["version64"]
    module["author"] = attrs.get("Author", "")
    for key, node_id in (("dependencies", "Dependencies"), ("conflicts", "Conflicts")):
        found: list[dict[str, Any]] = []
        list_node = root.find(f".//node[@id='{node_id}']")
        if list_node is not None:
            for node in list_node.iter("node"):
                if node.get("id") == "ModuleShortDesc":
                    dep = _short_desc(_attrs(node))
                    if dep["uuid"]:
                        found.append(dep)
        module[key] = found
    return module


def _parse_meta_loosely(text: str) -> dict[str, Any] | None:
    start = text.find('id="ModuleInfo"')
    if start < 0:
        return None
    end = text.find("<children>", start)
    block = text[start : end if end > 0 else len(text)]
    attrs = {m["id"]: m["value"] for m in _attr_re.finditer(block)}
    if not attrs.get("UUID"):
        return None
    module = _short_desc(attrs)
    module["author"] = attrs.get("Author", "")
    deps: list[dict[str, Any]] = []
    dstart = text.find('id="Dependencies"')
    if dstart >= 0:
        dend = text.find('id="ModuleInfo"', dstart)
        for chunk in text[dstart : dend if dend > 0 else start].split(
            'id="ModuleShortDesc"'
        )[1:]:
            dep = _short_desc({m["id"]: m["value"] for m in _attr_re.finditer(chunk)})
            if dep["uuid"]:
                deps.append(dep)
    module["dependencies"] = deps
    module["conflicts"] = []
    return module


# ------------------------------------------------------------- reading a pak


def judge(names: list[str], module: dict[str, Any] | None) -> dict[str, Any]:
    """A pak's info from its file list and its meta.lsx.

    A pak with a module is a mod for the load order when it has content of
    its own: files in a <Folder>/ other than meta.lsx (Mods/<Folder>/...,
    Public/<Folder>/...) or material groups. One with only a meta.lsx next to
    files that replace the game's is an override; it loads without being in
    the load order (the rule of the original plugin and of BG3 Mod Manager)."""
    needs_se = any(_se_config_re.search(n) for n in names)
    if module is None:
        return {"kind": "override", "needs_se": needs_se}
    folder = module.get("folder") or ""
    pattern = re.compile(
        f"(/{re.escape(folder)}/(?!meta\\.lsx$))|(Public/Engine/Timeline/MaterialGroups)",
        re.I,
    )
    own = bool(folder) and any(pattern.search("/" + n) for n in names)
    return {
        "kind": "module" if own else "override",
        "module": module,
        "needs_se": needs_se,
    }


def read_pak(path: Path, divine: Callable[[Path], tuple[list[str], bytes | None]] | None) -> dict[str, Any]:
    """Reads one pak. divine(path) -> (file names, meta.lsx) is asked for a
    package lspk doesn't know; None means LSLib isn't there (NeedsLSLib)."""
    try:
        package = lspk.Package(path)
        names = package.names
        entry = package.find("meta.lsx")
        meta = package.read(entry) if entry is not None else None
    except lspk.Damaged as e:
        raise DamagedPak(f"{path.name}: {e}") from e
    except lspk.Unsupported as e:
        if divine is None:
            raise NeedsLSLib(f"{path.name}: {e}") from e
        log_debug(f"{path.name} read by Divine: {e}")
        names, meta = divine(path)
    except (OSError, ValueError, OverflowError, MemoryError) as e:
        raise DamagedPak(f"{path.name}: {e}") from e
    module = parse_meta_lsx(meta) if meta else None
    return judge(names, module)


# ------------------------------------------------------------------- cache


def _stamp(path: Path) -> tuple[int, int] | None:
    try:
        st = path.stat()
    except OSError:
        return None
    return st.st_size, st.st_mtime_ns


class PakCache:
    """pak path -> what read_pak found, while the pak keeps its size and time.

    A pak that changed is read again; its last known info stays as a
    fallback (stale) for a start on which it can't be read (locked by
    another program), so the mod keeps its place in the load order."""

    def __init__(self, file: Path):
        self.file = file
        self._lock = threading.Lock()
        self._save_lock = threading.Lock()
        self._entries: dict[str, dict[str, Any]] | None = None
        self._dirty = False

    def _load(self) -> dict[str, dict[str, Any]]:
        if self._entries is None:
            entries: dict[str, dict[str, Any]] = {}
            try:
                raw = json.loads(self.file.read_text(encoding="utf-8"))
                if raw.get("version") == CACHE_VERSION and isinstance(
                    raw.get("paks"), dict
                ):
                    entries = raw["paks"]
            except (OSError, ValueError, AttributeError):
                pass
            self._entries = entries
        return self._entries

    @staticmethod
    def key(path: Path) -> str:
        return os.path.normcase(os.path.abspath(path))

    def get(self, path: Path) -> dict[str, Any] | None:
        stamp = _stamp(path)
        with self._lock:
            entry = self._load().get(self.key(path))
        if entry is None or stamp is None:
            return None
        if (entry.get("size"), entry.get("mtime")) != stamp:
            return None
        return entry.get("info")

    def stale(self, path: Path) -> dict[str, Any] | None:
        with self._lock:
            entry = self._load().get(self.key(path))
        return None if entry is None else entry.get("info")

    def put(self, path: Path, info: dict[str, Any]) -> None:
        stamp = _stamp(path)
        if stamp is None:
            return
        with self._lock:
            self._load()[self.key(path)] = {
                "size": stamp[0],
                "mtime": stamp[1],
                "info": info,
            }
            self._dirty = True

    def clear(self) -> None:
        with self._lock:
            self._entries = {}
            self._dirty = True

    def save(self) -> None:
        with self._save_lock:
            self._save()

    def _save(self) -> None:
        with self._lock:
            if not self._dirty or self._entries is None:
                return
            # paks that are gone (a mod removed or updated) leave the cache
            entries = {
                k: v for k, v in self._entries.items() if os.path.exists(k)
            }
            self._entries = entries
            data = json.dumps(
                {"version": CACHE_VERSION, "paks": entries},
                ensure_ascii=False,
                separators=(",", ":"),
            ).encode("utf-8")
            self._dirty = False
        try:
            write_atomically(self.file, data)
        except OSError as e:
            log_warning(f"couldn't save the pak cache {self.file}: {e}")

    def info(
        self,
        path: Path,
        divine: Callable[[Path], tuple[list[str], bytes | None]] | None,
    ) -> dict[str, Any]:
        cached = self.get(path)
        if cached is not None:
            return cached
        info = read_pak(path, divine)
        self.put(path, info)
        if info.get("kind") == "override":
            log_info(f"{path.name}: no module of its own, loads as an override")
        return info


# ----------------------------------------------------------- a mod's paks


def scan_mod_paks(modpath: Path) -> list[Path]:
    """A mod's paks, wherever they are in it. They all go to the documents'
    Mods folder by name, so of two with one name (two FOMOD options'
    folders) only the last in path order is the mod's, for the file mapper
    and the load order alike."""
    found: list[Path] = []
    for root, _dirs, files in os.walk(modpath):
        found.extend(Path(root, f) for f in files if f.casefold().endswith(".pak"))
    by_name: dict[str, Path] = {}
    for pak in sorted(found):
        by_name[pak.name.casefold()] = pak
    return sorted(by_name.values())


_part_re = re.compile(r"(.+)_(\d+)\.pak", re.I)


def is_part(pak: Path) -> bool:
    """A later part of a split package (Big_1.pak next to Big.pak): it goes
    to the documents' Mods folder with its first part, and only the first
    part has the index."""
    match = _part_re.fullmatch(pak.name)
    return bool(match) and pak.with_name(match[1] + pak.suffix).exists()


def forced_in_load_order(modpath: Path, pak: Path) -> bool:
    """A Wabbajack list author's tool (made with the unofficial BG3 plugin)
    leaves "LoadOrder = true" in the mod's meta.ini for a pak it wants in the
    load order although it looks like an override. Only read for overrides
    with a module, which are rare."""
    import configparser

    config = configparser.ConfigParser(interpolation=None, strict=False)
    try:
        config.read_string(
            (modpath / "meta.ini").read_bytes().decode("utf-8", "replace")
        )
    except (OSError, configparser.Error):
        return False
    for section in config.sections():
        if qsettings_unescape(section).casefold() != pak.name.casefold():
            continue
        keys = {k.lower(): v.strip().strip('"').lower() for k, v in config[section].items()}
        if "cacheversion" in keys and keys.get("loadorder") == "true":
            return True
    return False


_qsettings_escape_re = re.compile(r"%U([0-9A-Fa-f]{4})|%([0-9A-Fa-f]{2})|\\")


def qsettings_unescape(name: str) -> str:
    """A meta.ini group name as QSettings reads it ("Auto%20Wares.pak" is
    "Auto Wares.pak", "\\" is "/")."""

    def repl(m: re.Match[str]) -> str:
        if m.group(0) == "\\":
            return "/"
        return chr(int(m.group(1) or m.group(2), 16))

    # QSettings works in UTF-16 units: a character past U+FFFF is two %U
    unescaped = _qsettings_escape_re.sub(repl, name)
    return unescaped.encode("utf-16-le", "surrogatepass").decode("utf-16-le", "replace")
