import configparser
import hashlib
import os
import re
import shutil
import subprocess
import traceback
from functools import cached_property
from pathlib import Path
from typing import Callable
from xml.etree import ElementTree
from xml.etree.ElementTree import Element

from PyQt6.QtCore import (
    qDebug,
    qInfo,
    qWarning,
)

import mobase

from . import bg3_utils


# MO2 Revamped: a mod's meta.ini as MO2 writes it. Python's parser lower-cased
# every key on writing it back ("gameName" became "gamename", and MO2 then
# added its own again), and stopped on the duplicate it had made itself, which
# left the mod out of the load order. Keys keep their case now, duplicates
# don't stop the reading, and a key written twice in two cases keeps MO2's.
def _new_config() -> configparser.ConfigParser:
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.optionxform = str  # type: ignore[assignment, method-assign]
    return config


def read_meta_ini(path: Path) -> configparser.ConfigParser:
    # MO2 Revamped: a file that isn't UTF-8 throughout, or with keys before its
    # first section (QSettings' [General] left out), is read anyway instead of
    # leaving the mod out of the load order: bytes that aren't UTF-8 are kept
    # as they are (surrogateescape, written back the same in write_meta_ini)
    try:
        text = path.read_bytes().decode("utf-8", errors="surrogateescape")
    except OSError:
        text = ""
    if text.startswith("\ufeff"):
        text = text[1:]
    config = _new_config()
    try:
        config.read_string(text, source=str(path))
    except configparser.MissingSectionHeaderError:
        config = _new_config()
        config.read_string("[General]\n" + text, source=str(path))
    _unescape_sections(config)
    for section in config.sections():
        seen: dict[str, str] = {}
        for key in list(config[section].keys()):
            other = seen.get(key.lower())
            if other is None:
                seen[key.lower()] = key
                continue
            # the all-lowercase one is the parser's old copy
            drop = key if key == key.lower() else other
            keep = other if drop == key else key
            config.remove_option(section, drop)
            seen[key.lower()] = keep
    return config


_qsettings_escape_re = re.compile(r"%U([0-9A-Fa-f]{4})|%([0-9A-Fa-f]{2})|\\")


def qsettings_unescape(name: str) -> str:
    """MO2 Revamped: a group name as QSettings reads it ("Auto%20Wares.pak" is
    "Auto Wares.pak", "\\" is "/")."""

    def repl(m: re.Match[str]) -> str:
        if m.group(0) == "\\":
            return "/"
        return chr(int(m.group(1) or m.group(2), 16))

    # QSettings works in UTF-16 units: a character past U+FFFF is two %U
    unescaped = _qsettings_escape_re.sub(repl, name)
    return unescaped.encode("utf-16-le", "surrogatepass").decode(
        "utf-16-le", "replace"
    )


def qsettings_escape(name: str) -> str:
    """MO2 Revamped: a group name as QSettings writes it: letters, digits, "_",
    "-" and "." as they are, "/" as "\\", the rest as %XX or %UXXXX."""
    out: list[str] = []
    for ch in name:
        code = ord(ch)
        if ch == "/":
            out.append("\\")
        elif (ch.isascii() and ch.isalnum()) or ch in "_-.":
            out.append(ch)
        elif code <= 0xFF:
            out.append(f"%{code:02X}")
        else:
            units = ch.encode("utf-16-le", "surrogatepass")
            for i in range(0, len(units), 2):
                out.append(f"%U{int.from_bytes(units[i : i + 2], 'little'):04X}")
    return "".join(out)


def _unescape_sections(config: configparser.ConfigParser) -> None:
    """MO2 Revamped: MO2 saves meta.ini with QSettings, which writes a pak's
    section "[Auto Wares.pak]" as "[Auto%20Wares.pak]"; under the name the pak
    has, this plugin didn't find its own entry again (the pak was read again on
    every start) nor the entry a list author's tool left. Sections are kept by
    their real names here; two copies of one (the old plain one this plugin
    wrote next to MO2's) become one, the plain copy's keys winning."""
    for section in list(config.sections()):
        name = qsettings_unescape(section)
        if name == section:
            continue
        items = dict(config[section])
        config.remove_section(section)
        if config.has_section(name):
            items.update(dict(config[name]))
            config.remove_section(name)
        config[name] = items


def write_meta_ini(config: configparser.ConfigParser, path: Path) -> None:
    """MO2 Revamped: meta.ini with the section names as QSettings writes them,
    so MO2 and this plugin keep one section per pak."""
    out = configparser.ConfigParser(interpolation=None, strict=False)
    out.optionxform = str  # type: ignore[assignment, method-assign]
    for section in config.sections():
        out[qsettings_escape(section)] = dict(config[section])
    with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
        out.write(f)


def _changed_since(folder: Path, mtime: float) -> bool:
    """MO2 Revamped: whether a file in the folder is newer than the pak built
    from it. The old check's break only left the inner loop, so a pak, once
    built, was never built again after its files changed."""
    for root, _, files in folder.walk():
        # a file deleted or renamed changes its folder's time
        try:
            if os.path.getmtime(root) > mtime:
                return True
        except OSError:
            return True
        for f in files:
            try:
                if os.path.getmtime(root / f) > mtime:
                    return True
            except OSError as e:
                qDebug(f"Error accessing file {root / f}: {e}")
                return True
    return False


def ini_value(value: str) -> str:
    """MO2 Revamped: a value as QSettings reads it back unchanged: quoted when
    it has a "," (QSettings makes a list of it and writes it back as "a, b"),
    a ";" (a comment), a quote, a backslash or spaces at its ends."""
    if not any(c in value for c in ',;"\\') and value == value.strip():
        return value
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def _plain_value(value: str) -> str:
    """MO2 Revamped: a quoted value (ini_value, or QSettings' own) as text."""
    if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
        return re.sub(r"\\(.)", r"\1", value[1:-1])
    return value



def _subfolders(path: Path) -> list[Path]:
    try:
        with os.scandir(path) as it:
            return [Path(e.path) for e in it if e.is_dir()]
    except OSError:
        return []


def _text(value: str) -> str:
    # a byte that wasn't UTF-8 (read_meta_ini) as U+FFFD, not a lone surrogate
    # that can't be written into modsettings.lsx
    return value.encode("utf-8", "surrogateescape").decode("utf-8", "replace")


def _keys(section: configparser.SectionProxy) -> dict[str, str]:
    return {key.lower(): _text(_plain_value(value)) for key, value in section.items()}


def _is_override(keys: dict[str, str]) -> bool:
    # "Override = false" (newer versions of this plugin write it for every pak)
    # is a mod for the load order, not an override
    return keys.get("override", "").strip().lower() in ("true", "1", "yes")


def _wanted_in_load_order(config: configparser.ConfigParser, file: Path) -> bool:
    # another tool's cache (a Wabbajack list's author) that puts the pak in the
    # load order: "Override = true" there only means it also replaces files
    if not config.has_section(file.name):
        return False
    keys = _keys(config[file.name])
    return "cacheversion" in keys and keys.get("loadorder", "").strip().lower() == "true"


def _has_cached_entry(config: configparser.ConfigParser, file: Path) -> bool:
    # what this plugin wrote for the pak: an override, or the whole entry;
    # anything else (another version's cache) is read from the pak again
    if not config.has_section(file.name):
        return False
    keys = _keys(config[file.name])
    if "cacheversion" in keys:
        # another version's cache ("Override = true" with "LoadOrder = true"
        # is a mod for the load order there): read the pak again
        return False
    return _is_override(keys) or all(
        key.lower() in keys for key in BG3PakParser._types
    )


class BG3PakParser:
    def __init__(self, utils: bg3_utils.BG3Utils):
        self._utils = utils

    _mod_cache: dict[Path, bool] = {}
    _types = {
        "Folder": "",
        "MD5": "",
        "Name": "",
        "PublishHandle": "0",
        "UUID": "",
        "Version64": "0",
    }

    @cached_property
    def _divine_command(self):
        return f"{self._utils.tools_dir / 'Divine.exe'} -g bg3 -l info"

    @cached_property
    def _mapped_folder_names(self) -> set[str]:
        return {
            f.casefold()
            for f in bg3_utils.top_level_data_folders
            | {"Root", bg3_utils.se_config_folder, bg3_utils.pak_files_folder}
        }

    # MO2 Revamped: the whole name, in any case ("Combined Hairs" or
    # "ModDatabase" are packable folders, they only have "bin"/"Data" in them)
    @cached_property
    def _folder_pattern(self):
        return re.compile("Data|Script Extender|bin|Mods", re.IGNORECASE)

    def prepare(
        self, mod: mobase.IModInterface
    ) -> tuple[list[Path], configparser.ConfigParser | None]:
        """MO2 Revamped: the mod's paks and packable folders (one walk of its
        folder) and its meta.ini (read once; it was read again for every pak and
        every top-level folder), or no config when there's nothing to read."""
        modpath = Path(mod.absolutePath())
        files = bg3_utils.scan_mod_paks(modpath)
        if self._utils.autobuild_paks:
            files = files + [
                f
                for f in _subfolders(modpath)
                if not self._folder_pattern.fullmatch(f.name)
                and f.name.casefold() not in self._mapped_folder_names
            ]
        if not files:
            return [], None
        return sorted(files), read_meta_ini(modpath / "meta.ini")

    @staticmethod
    def needs_divine(
        files: list[Path],
        config: configparser.ConfigParser | None,
        force_reparse_metadata: bool,
    ) -> bool:
        """MO2 Revamped: whether reading the mod may run Divine (a pak with no
        entry of this plugin's in meta.ini, a folder to pack): such mods go to
        the worker threads, the others are read right away."""
        if config is None:
            return False
        if force_reparse_metadata:
            return True
        return any(
            not f.name.endswith("pak") or not _has_cached_entry(config, f)
            for f in files
        )

    def metadata_from(
        self,
        mod: mobase.IModInterface,
        files: list[Path],
        config: configparser.ConfigParser | None,
        force_reparse_metadata: bool,
    ) -> str:
        if config is None:
            return ""
        try:
            return "".join(
                self._get_metadata_for_file(mod, file, force_reparse_metadata, config)
                for file in files
            )
        finally:
            # MO2 Revamped: meta.ini written once for the mod, not once per pak
            if getattr(config, "_mo2r_changed", False):
                write_meta_ini(config, Path(mod.absolutePath()) / "meta.ini")
                config._mo2r_changed = False  # type: ignore[attr-defined]

    def get_metadata_for_files_in_mod(
        self, mod: mobase.IModInterface, force_reparse_metadata: bool
    ):
        files, config = self.prepare(mod)
        return {
            mod.name(): self.metadata_from(mod, files, config, force_reparse_metadata)
        }

    def _get_metadata_for_file(
        self,
        mod: mobase.IModInterface,
        file: Path,
        force_reparse_metadata: bool,
        config: configparser.ConfigParser,
    ) -> str:
        meta_ini = Path(mod.absolutePath()) / "meta.ini"
        try:
            if file.name.endswith("pak"):
                meta_file = (
                    self._utils.plugin_data_path
                    / "temp"
                    / "extracted_metadata"
                    / f"{file.name[: int(len(file.name) / 2)]}-{hashlib.md5(str(file).encode(), usedforsecurity=False).hexdigest()[:5]}.lsx"
                )
                try:
                    if not force_reparse_metadata and _has_cached_entry(config, file):
                        return get_module_short_desc(config, file)
                    meta_file.parent.mkdir(parents=True, exist_ok=True)
                    meta_file.unlink(missing_ok=True)
                    out_dir = (
                        str(meta_file)[:-4] if self._utils.extract_full_package else ""
                    )
                    can_continue = True
                    if self.run_divine(
                        f'{"extract-package" if self._utils.extract_full_package else "extract-single-file -f meta.lsx"} -d "{meta_file if not self._utils.extract_full_package else out_dir}"',
                        file,
                    ).returncode:
                        can_continue = False
                    if can_continue and self._utils.extract_full_package:
                        qDebug(f"archive {file} extracted to {out_dir}")
                        if self.run_divine(
                            f'convert-resources -d "{out_dir}" -i lsf -o lsx -x "*.lsf"',
                            out_dir,
                        ).returncode:
                            qDebug(
                                f"failed to convert lsf files in {out_dir} to readable lsx"
                            )
                        extracted_meta_files = list(Path(out_dir).rglob("meta.lsx"))
                        if len(extracted_meta_files) == 0:
                            qInfo(
                                f"No meta.lsx files found in {file.name}, {file.name} determined to be an override mod"
                            )
                            can_continue = False
                        else:
                            shutil.copyfile(
                                extracted_meta_files[0],
                                meta_file,
                            )
                    elif can_continue and not meta_file.exists():
                        qInfo(
                            f"No meta.lsx files found in {file.name}, {file.name} determined to be an override mod"
                        )
                        can_continue = False
                    return self.metadata_to_ini(
                        config,
                        file,
                        mod,
                        meta_ini,
                        can_continue,
                        lambda: meta_file,
                        _wanted_in_load_order(config, file),
                    )
                finally:
                    if self._utils.remove_extracted_metadata:
                        meta_file.unlink(missing_ok=True)
                        if self._utils.extract_full_package:
                            Path(str(meta_file)[:-4]).unlink(missing_ok=True)
            elif file.is_dir():
                # MO2 Revamped: prepare() leaves out the folders that aren't
                # packed (Data, bin, ..., and the game folders at the mod's top
                # level, which are mapped to Data/)
                for folder in bg3_utils.loose_file_folders:
                    if next(file.glob(f"{folder}/*"), False):
                        break
                else:
                    return ""
                qInfo(f"packable dir: {file}")
                if (file.parent / f"{file.name}.pak").exists() or (
                    file.parent / "Mods" / f"{file.name}.pak"
                ).exists():
                    qInfo(
                        f"pak with same name as packable dir exists in mod directory. not packing dir {file}"
                    )
                    return ""
                parent_mod_name = file.parent.name.replace(" ", "_")
                pak_path = (
                    self._utils.overwrite_path
                    / f"Mods/{parent_mod_name}_{file.name}.pak"
                )
                build_pak = True
                if pak_path.exists():
                    try:
                        build_pak = _changed_since(file, os.path.getmtime(pak_path))
                    except OSError as e:
                        qDebug(f"Error accessing file {pak_path}: {e}")
                        build_pak = False
                if build_pak:
                    pak_path.unlink(missing_ok=True)
                    if self.run_divine(
                        f'create-package -d "{pak_path}"', file
                    ).returncode:
                        return ""
                meta_files = list(file.glob("Mods/*/meta.lsx"))
                return self.metadata_to_ini(
                    config,
                    file,
                    mod,
                    meta_ini,
                    len(meta_files) > 0,
                    lambda: meta_files[0],
                )
            else:
                return ""
        except Exception:
            qWarning(traceback.format_exc())
            return ""

    def run_divine(
        self, action: str, source: Path | str
    ) -> subprocess.CompletedProcess[str]:
        command = f'{self._divine_command} -a {action} -s "{source}"'
        result = subprocess.run(
            command,
            creationflags=subprocess.CREATE_NO_WINDOW,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if result.returncode:
            qWarning(
                f"{command.replace(str(Path.home()), '~', 1).replace(str(Path.home()), '$HOME')}"
                f" returned stdout: {result.stdout}, stderr: {result.stderr}, code {result.returncode}"
            )
        return result

    def get_attr_value(self, root: Element, attr_id: str) -> str:
        default_val = self._types.get(attr_id) or ""
        attr = root.find(f".//attribute[@id='{attr_id}']")
        return default_val if attr is None else attr.get("value", default_val)

    def metadata_to_ini(
        self,
        config: configparser.ConfigParser,
        file: Path,
        mod: mobase.IModInterface,
        meta_ini: Path,
        condition: bool,
        to_parse: Callable[[], Path],
        in_load_order: bool = False,
    ):
        # MO2 Revamped: the config is the mod's for all its paks
        # (get_metadata_for_files_in_mod); if this pak can't be read, its old
        # entry (a list author's cache too) stays for the next pak's write
        previous = dict(config[file.name]) if config.has_section(file.name) else None
        try:
            return self._metadata_to_ini(
                config, file, mod, meta_ini, condition, to_parse, in_load_order
            )
        except Exception:
            config.remove_section(file.name)
            if previous is not None:
                config[file.name] = previous
            raise

    def _metadata_to_ini(
        self,
        config: configparser.ConfigParser,
        file: Path,
        mod: mobase.IModInterface,
        meta_ini: Path,
        condition: bool,
        to_parse: Callable[[], Path],
        in_load_order: bool,
    ):
        config[file.name] = {}
        if condition:
            root = (
                ElementTree.parse(to_parse())
                .getroot()
                .find(".//node[@id='ModuleInfo']")
            )
            if root is None:
                qInfo(f"No ModuleInfo node found in meta.lsx for {mod.name()} ")
            else:
                section = config[file.name]
                folder_name = self.get_attr_value(root, "Folder")
                if file.is_dir():
                    self._mod_cache[file] = (
                        len(list(file.glob(f"*/{folder_name}/**"))) > 1
                        or len(
                            list(file.glob("Public/Engine/Timeline/MaterialGroups/*"))
                        )
                        > 0
                    )
                elif in_load_order:
                    self._mod_cache[file] = True
                elif file not in self._mod_cache:
                    # a mod which has a meta.lsx and is not an override mod meets at least one of three conditions:
                    # 1. it has files in Public/Engine/Timeline/MaterialGroups, or
                    # 2. it has files in Mods/<folder_name>/ other than the meta.lsx file, or
                    # 3. it has files in Public/<folder_name>
                    result = self.run_divine(
                        f'list-package --use-regex -x "(/{re.escape(folder_name)}/(?!meta\\.lsx))|(Public/Engine/Timeline/MaterialGroups)"',
                        file,
                    )
                    self._mod_cache[file] = (
                        result.returncode == 0 and result.stdout.strip() != ""
                    )
                if self._mod_cache[file]:
                    for key in self._types:
                        section[key] = ini_value(self.get_attr_value(root, key))
                else:
                    qInfo(f"pak {file.name} determined to be an override mod")
                    section["override"] = "True"
                    section["Folder"] = ini_value(folder_name)
        else:
            config[file.name]["override"] = "True"
        # MO2 Revamped: written once for the mod (metadata_from)
        config._mo2r_changed = True  # type: ignore[attr-defined]
        return get_module_short_desc(config, file)


def get_module_short_desc(config: configparser.ConfigParser, file: Path) -> str:
    if not config.has_section(file.name):
        return ""
    keys = _keys(config[file.name])
    return (
        ""
        if _is_override(keys) or "name" not in keys
        else bg3_utils.get_node_string(
            folder=keys.get("folder", ""),
            md5=keys.get("md5", ""),
            name=keys["name"],
            publish_handle=keys.get("publishhandle", "0"),
            uuid=keys.get("uuid", ""),
            version64=keys.get("version64", "0"),
        )
    )
