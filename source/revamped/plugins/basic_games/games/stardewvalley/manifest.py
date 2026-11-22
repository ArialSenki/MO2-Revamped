"""SMAPI's manifest.json: what a Stardew Valley mod is and what it needs.

SMAPI reads it with Json.NET, which accepts comments and trailing commas, so
mods ship them; json.loads doesn't. UniqueIDs compare without case.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

# SMAPI and the frameworks content packs and mods most often need: the Nexus
# page to send the user to when one is missing
KNOWN = {
    "smapi": ("SMAPI", 2400),
    "pathoschild.contentpatcher": ("Content Patcher", 1915),
    "spacechase0.jsonassets": ("Json Assets", 1720),
    "esca.farmtypemanager": ("Farm Type Manager", 3231),
    "spacechase0.genericmodconfigmenu": ("Generic Mod Config Menu", 5098),
    "platonymous.toolkit": ("PyTK", 1726),
    "digus.expandedpreconditionsutility": ("Expanded Preconditions Utility", 6529),
    "cherry.shoptileframework": ("Shop Tile Framework", 5005),
    "spacechase0.spacecore": ("SpaceCore", 1348),
    "aedenthorn.mailframeworkmod": ("Mail Framework Mod", 1536),
}

# the mods SMAPI's installer puts in the game's Mods folder
BUNDLED = {"smapi.consolecommands", "smapi.savebackup"}

# a real manifest is a few KB; a bigger file is not read
MAX_SIZE = 256 * 1024

# MO2 Revamped: one linear scan. A string (an open one runs to the end), a //
# comment, the start of a /* comment, a comma, a closing bracket, a run of
# anything else, or one stray character
_TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"?|//[^\n]*|/\*|[,\]}]|[^"/,\]}]+|.', re.DOTALL)


def strip_json(text: str) -> str:
    """The text without // and /* */ comments and trailing commas (strings
    left as they are). An unclosed /* is an error, as it is for Json.NET."""
    out: list[str] = []
    comma = -1  # where in `out` a comma waits for what follows it
    skip_to = 0
    for match in _TOKEN.finditer(text):
        if match.start() < skip_to:
            continue
        token = match.group(0)
        if token == "/*":
            end = text.find("*/", match.end())
            if end < 0:
                raise ValueError("unclosed comment")
            skip_to = end + 2
            continue
        if token.startswith("//"):
            continue
        if token in ("}", "]") and comma >= 0:
            out[comma] = ""
        if token == ",":
            out.append(token)
            comma = len(out) - 1
            continue
        if not token.isspace():
            comma = -1
        out.append(token)
    return "".join(out)


def loads(text: str):
    """json.loads that also takes what Json.NET takes: a BOM, // and /* */
    comments, trailing commas."""
    if len(text) > MAX_SIZE:
        raise ValueError("manifest too big")
    return json.loads(strip_json(text.lstrip("﻿")))


@dataclass(frozen=True)
class Dependency:
    unique_id: str  # casefolded
    minimum: str = ""
    required: bool = True


@dataclass
class Manifest:
    unique_id: str  # casefolded
    name: str
    version: str = ""
    minimum_api: str = ""
    entry_dll: str = ""
    content_pack_for: Dependency | None = None
    dependencies: list[Dependency] = field(default_factory=list)

    def needs(self) -> list[Dependency]:
        """What the mod can't load without."""
        needed = [d for d in self.dependencies if d.required]
        if self.content_pack_for is not None:
            needed.append(self.content_pack_for)
        return needed

    def optional(self) -> list[Dependency]:
        """What it uses when there (SMAPI still checks its version then)."""
        return [d for d in self.dependencies if not d.required]


def _get(data: dict, key: str, default=None):
    """A key of a SMAPI manifest, whatever its case (SMAPI ignores case)."""
    for name, value in data.items():
        if isinstance(name, str) and name.casefold() == key.casefold():
            return value
    return default


def _dependency(data) -> Dependency | None:
    if not isinstance(data, dict):
        return None
    unique_id = _get(data, "UniqueID")
    if not isinstance(unique_id, str) or not unique_id.strip():
        return None
    required = _get(data, "IsRequired", True)
    minimum = _get(data, "MinimumVersion", "")
    return Dependency(unique_id.strip().casefold(), minimum if isinstance(minimum, str) else "", required is not False)


def parse(text: str) -> Manifest | None:
    """A manifest, or None for one SMAPI couldn't use either."""
    try:
        data = loads(text)
    except (ValueError, RecursionError):
        return None
    if not isinstance(data, dict):
        return None
    unique_id = _get(data, "UniqueID")
    if not isinstance(unique_id, str) or not unique_id.strip():
        return None
    # a field of the wrong type is read as missing, never fails the manifest
    listed = _get(data, "Dependencies", [])
    dependencies = [d for d in map(_dependency, listed if isinstance(listed, list) else []) if d is not None]

    def text(key: str) -> str:
        value = _get(data, key, "")
        return value.strip() if isinstance(value, str) else ""

    return Manifest(
        unique_id=unique_id.strip().casefold(),
        name=text("Name") or unique_id.strip(),
        version=text("Version"),
        minimum_api=text("MinimumApiVersion"),
        entry_dll=text("EntryDll"),
        content_pack_for=_dependency(_get(data, "ContentPackFor")),
        dependencies=dependencies,
    )


def read(path: Path) -> Manifest | None:
    try:
        if path.stat().st_size > MAX_SIZE:
            return None
        data = path.read_bytes()
        # SMAPI reads it with .NET's File.ReadAllText, which follows a UTF-16
        # BOM (Notepad's "Unicode"); anything else is UTF-8
        encoding = "utf-16" if data[:2] in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"
        return parse(data.decode(encoding, errors="replace"))
    except OSError:
        return None


_VERSION_PART = re.compile(r"\d+")


def version_tuple(version: str) -> tuple[int, ...] | None:
    """The numbers of a semantic version ("1.2.3-beta.4" -> (1, 2, 3)), or
    None when it has none; the prerelease tag doesn't count, which only errs
    towards no warning."""
    core = re.split(r"[-+]", version.strip(), maxsplit=1)[0]
    numbers = [int(part) for part in _VERSION_PART.findall(core)][:4]
    if not numbers:
        return None
    while len(numbers) < 3:
        numbers.append(0)
    return tuple(numbers)


def older(version: str, minimum: str) -> bool:
    """Whether a known version is older than a minimum (False when either is
    missing or unreadable)."""
    have, need = version_tuple(version or ""), version_tuple(minimum or "")
    if have is None or need is None:
        return False
    return have < need
