"""modsettings.lsx: the load order file the game reads from
<documents>/PlayerProfiles/Public.

Layout as the game (Patch 7 and later), BG3 Mod Manager and Vortex write it:
no ModOrder node, the order of the Mods children is the load order, the base
module (GustavX) first.
"""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any, Iterable

from .paks import BASE_MODULE_UUIDS

DEFAULT_VERSION = (4, 8, 0, 500)

GUSTAVX = {
    "folder": "GustavX",
    "md5": "",
    "name": "GustavX",
    "publish_handle": 0,
    "uuid": "cb555efe-2d9e-131f-8195-a89329d218ea",
    "version64": 36028797018963968,
}


# MO2 Revamped: characters XML 1.0 can't hold (a control code in a mod's name
# or a lone surrogate) make the game reject the whole file
_XML_BAD = re.compile("[^\t\n\r\u0020-\ud7ff\ue000-\ufffd\U00010000-\U0010ffff]")


def _xml(value: Any) -> str:
    # "Tattoo & Makeup" broke the file the game reads
    return (
        _XML_BAD.sub("", str(value))
        .replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def module_node(module: dict[str, Any]) -> str:
    return f"""
                        <node id="ModuleShortDesc">
                            <attribute id="Folder" type="LSString" value="{_xml(module.get("folder", ""))}"/>
                            <attribute id="MD5" type="LSString" value="{_xml(module.get("md5", ""))}"/>
                            <attribute id="Name" type="LSString" value="{_xml(module.get("name", ""))}"/>
                            <attribute id="PublishHandle" type="uint64" value="{int(module.get("publish_handle") or 0)}"/>
                            <attribute id="UUID" type="guid" value="{_xml(module.get("uuid", ""))}"/>
                            <attribute id="Version64" type="int64" value="{int(module.get("version64") or 0)}"/>
                        </node>"""


def render(modules: Iterable[dict[str, Any]], version: tuple[int, int, int, int]) -> str:
    major, minor, revision, build = version
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        "<save>\n"
        f'    <version major="{major}" minor="{minor}" revision="{revision}" build="{build}"/>\n'
        '    <region id="ModuleSettings">\n'
        '        <node id="root">\n'
        "            <children>\n"
        '                <node id="Mods">\n'
        "                    <children>"
        + "".join(module_node(m) for m in modules)
        + "\n                    </children>\n"
        "                </node>\n"
        "            </children>\n"
        "        </node>\n"
        "    </region>\n"
        "</save>\n"
    )


class ModSettings:
    def __init__(self) -> None:
        self.version: tuple[int, int, int, int] | None = None
        self.modules: list[dict[str, Any]] = []

    @property
    def base(self) -> list[dict[str, Any]]:
        return [m for m in self.modules if m["uuid"].casefold() in BASE_MODULE_UUIDS]

    @property
    def mods(self) -> list[dict[str, Any]]:
        return [m for m in self.modules if m["uuid"].casefold() not in BASE_MODULE_UUIDS]

    def uuids(self) -> list[str]:
        return [m["uuid"].casefold() for m in self.mods]


_version_re = re.compile(
    r'<version\s+major="(\d+)"\s+minor="(\d+)"\s+revision="(\d+)"\s+build="(\d+)"'
)


def parse(text: str) -> ModSettings:
    """What a modsettings.lsx holds; a file the game left half-written or
    that isn't XML gives an empty list, never an exception."""
    from xml.etree import ElementTree

    result = ModSettings()
    match = _version_re.search(text)
    if match:
        result.version = tuple(int(g) for g in match.groups())  # type: ignore[assignment]
    try:
        root = ElementTree.fromstring(text.lstrip("﻿"))
    except ElementTree.ParseError:
        return result
    mods = root.find(".//node[@id='Mods']")
    if mods is None:
        return result
    from .paks import _attrs, _short_desc

    children = mods.find("children")
    for node in children if children is not None else []:
        if node.get("id") != "ModuleShortDesc":
            continue
        module = _short_desc(_attrs(node))
        if module["uuid"]:
            result.modules.append(module)
    return result


def read(path: Path) -> ModSettings | None:
    try:
        data = path.read_bytes()
    except OSError:
        return None
    return parse(data.decode("utf-8", "replace"))


def newest_version(*versions: tuple[int, int, int, int] | None) -> tuple[int, int, int, int]:
    """The version node to write: the newest the game itself wrote (it
    writes its own when it saves the file), never older than ours."""
    known = [v for v in versions if v and v[0] >= 4]
    return max(known + [DEFAULT_VERSION])
