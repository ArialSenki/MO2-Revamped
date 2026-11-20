"""Writes small but real LSPK packages (.pak, .lsv) for the tests, the way
LSLib lays them out: files, then the LZ4 file list at the end."""

from __future__ import annotations

import struct
from pathlib import Path


def _lz4_literals(data: bytes) -> bytes:
    """An LZ4 block of one literal run (valid LZ4, no matches)."""
    n = len(data)
    out = bytearray()
    if n < 15:
        out.append(n << 4)
    else:
        out.append(0xF0)
        rest = n - 15
        while rest >= 255:
            out.append(255)
            rest -= 255
        out.append(rest)
    return bytes(out) + data


def write_pak(path: Path, files: dict[str, bytes], version: int = 18) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    header_size = 4 + (struct.calcsize("<IQIBB16sH") if version != 15 else struct.calcsize("<IQIBB16s"))
    body = bytearray()
    entries = bytearray()
    for name, data in files.items():
        offset = header_size + len(body)
        body += data
        raw_name = name.encode("utf-8").ljust(256, b"\0")
        if version == 18:
            entries += struct.pack(
                "<256sIHBBII", raw_name, offset & 0xFFFFFFFF, offset >> 32, 0, 0, len(data), len(data)
            )
        else:
            entries += struct.pack("<256sQQQIIII", raw_name, offset, len(data), len(data), 0, 0, 0, 0)
    compressed = _lz4_literals(bytes(entries))
    list_offset = header_size + len(body)
    file_list = struct.pack("<ii", len(files), len(compressed)) + compressed
    if version == 15:
        header = b"LSPK" + struct.pack("<IQIBB16s", version, list_offset, len(file_list), 0, 0, b"\0" * 16)
    else:
        header = b"LSPK" + struct.pack("<IQIBB16sH", version, list_offset, len(file_list), 0, 0, b"\0" * 16, 1)
    path.write_bytes(header + bytes(body) + file_list)
    return path


def meta_lsx(
    name: str,
    uuid: str,
    folder: str | None = None,
    version64: int = 36028797018963968,
    deps: list[tuple[str, str]] = (),
    conflicts: list[tuple[str, str]] = (),
    raw_description: str = "A test mod",
) -> bytes:
    folder = folder or name

    def short(n: str, u: str, v: int = 0) -> str:
        return (
            '<node id="ModuleShortDesc">'
            f'<attribute id="Folder" type="LSString" value="{n}"/>'
            f'<attribute id="Name" type="LSString" value="{n}"/>'
            f'<attribute id="UUID" type="guid" value="{u}"/>'
            f'<attribute id="Version64" type="int64" value="{v}"/>'
            "</node>"
        )

    dep_nodes = "".join(short(n, u) for n, u in deps)
    conflict_nodes = "".join(short(n, u) for n, u in conflicts)
    return (
        '\ufeff<?xml version="1.0" encoding="UTF-8"?>\n<save><version major="4" minor="0" revision="9" build="331"/>'
        '<region id="Config"><node id="root"><children>'
        f'<node id="Dependencies"><children>{dep_nodes}</children></node>'
        f'<node id="Conflicts"><children>{conflict_nodes}</children></node>'
        '<node id="ModuleInfo">'
        '<attribute id="Author" type="LSString" value="Tester"/>'
        f'<attribute id="Description" type="LSString" value="{raw_description}"/>'
        f'<attribute id="Folder" type="LSString" value="{folder}"/>'
        '<attribute id="MD5" type="LSString" value=""/>'
        f'<attribute id="Name" type="LSString" value="{name}"/>'
        '<attribute id="PublishHandle" type="uint64" value="0"/>'
        f'<attribute id="UUID" type="guid" value="{uuid}"/>'
        f'<attribute id="Version64" type="int64" value="{version64}"/>'
        "</node></children></node></region></save>"
    ).encode("utf-8")


def mod_pak(path: Path, name: str, uuid: str, se: bool = False, override: bool = False, **meta) -> Path:
    folder = meta.get("folder") or name
    files = {f"Mods/{folder}/meta.lsx": meta_lsx(name, uuid, **meta)}
    if override:
        files["Public/Game/GUI/Assets/x.dds"] = b"dds"
    else:
        files[f"Public/{folder}/Stats/Generated/Data/x.txt"] = b"new entry"
    if se:
        files[f"Mods/{folder}/ScriptExtender/Config.json"] = b'{"RequiredVersion": 1}'
    return write_pak(path, files)
