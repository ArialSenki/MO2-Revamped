from __future__ import annotations

import argparse
import hashlib
import json
import marshal
from pathlib import Path, PurePosixPath, PureWindowsPath
import py_compile
import shutil
import struct
import sys
import tempfile
import zipfile


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def normalize_pe_pdb_path(path: Path) -> int:
    """Keep CodeView PDB references while removing machine-specific directories."""
    data = bytearray(path.read_bytes())
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise RuntimeError(f"Expected a PE image: {path}")

    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if pe_offset + 24 > len(data) or data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise RuntimeError(f"Invalid PE header: {path}")

    coff_offset = pe_offset + 4
    section_count = struct.unpack_from("<H", data, coff_offset + 2)[0]
    optional_size = struct.unpack_from("<H", data, coff_offset + 16)[0]
    optional_offset = coff_offset + 20
    if optional_offset + optional_size > len(data):
        raise RuntimeError(f"Truncated PE optional header: {path}")

    magic = struct.unpack_from("<H", data, optional_offset)[0]
    if magic == 0x20B:
        directories_offset = optional_offset + 112
    elif magic == 0x10B:
        directories_offset = optional_offset + 96
    else:
        raise RuntimeError(f"Unsupported PE optional-header type in {path}")

    debug_entry = directories_offset + 6 * 8
    if debug_entry + 8 > optional_offset + optional_size:
        raise RuntimeError(f"PE image has no debug data directory: {path}")
    debug_rva, debug_size = struct.unpack_from("<II", data, debug_entry)
    if debug_rva == 0 or debug_size == 0:
        return 0

    section_offset = optional_offset + optional_size

    def rva_to_file_offset(rva: int) -> int:
        for index in range(section_count):
            header = section_offset + index * 40
            if header + 40 > len(data):
                break
            virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
                "<IIII", data, header + 8
            )
            span = max(virtual_size, raw_size)
            if virtual_address <= rva < virtual_address + span:
                result = raw_pointer + rva - virtual_address
                if result < len(data):
                    return result
        raise RuntimeError(f"Could not map PE RVA 0x{rva:X} in {path}")

    debug_offset = rva_to_file_offset(debug_rva)
    record_size = 28
    record_count = debug_size // record_size
    if record_count == 0 or debug_offset + record_count * record_size > len(data):
        raise RuntimeError(f"Invalid PE debug directory in {path}")

    changed = 0
    for index in range(record_count):
        record = debug_offset + index * record_size
        kind = struct.unpack_from("<I", data, record + 12)[0]
        size_of_data = struct.unpack_from("<I", data, record + 16)[0]
        data_pointer = struct.unpack_from("<I", data, record + 24)[0]
        if kind != 2 or size_of_data < 25:
            continue
        if data_pointer + size_of_data > len(data) or data[data_pointer : data_pointer + 4] != b"RSDS":
            continue

        name_start = data_pointer + 24
        name_limit = data_pointer + size_of_data
        try:
            name_end = data.index(0, name_start, name_limit)
        except ValueError as exc:
            raise RuntimeError(f"Unterminated CodeView PDB path in {path}") from exc

        old_name = bytes(data[name_start:name_end])
        try:
            decoded_name = old_name.decode("ascii")
        except UnicodeDecodeError as exc:
            raise RuntimeError(f"Non-ASCII CodeView PDB path in {path}") from exc
        new_name = PureWindowsPath(decoded_name.replace("/", "\\")).name.encode("ascii")
        if not new_name or new_name == old_name:
            continue

        available = name_end + 1 - name_start
        if len(new_name) + 1 > available:
            raise RuntimeError(f"PDB basename does not fit CodeView record in {path}")
        data[name_start : name_end + 1] = new_name + b"\0" * (available - len(new_name))
        changed += 1

    if changed:
        path.write_bytes(data)
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Prepare a Python runtime overlay for MO2 Revamped.")
    parser.add_argument("--python-source", required=True, type=Path)
    parser.add_argument("--python-build", required=True, type=Path)
    parser.add_argument("--base-plugin-python", required=True, type=Path)
    parser.add_argument("--openssl-extension-directory", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--openssl-version", required=True)
    args = parser.parse_args()

    if sys.version_info[:3] != (3, 12, 15):
        raise RuntimeError(f"Run this script with Python 3.12.15; found {sys.version.split()[0]}.")
    if args.openssl_version != "3.5.9":
        raise RuntimeError("This release overlay expects OpenSSL 3.5.9.")

    python_source = args.python_source.resolve()
    python_build = args.python_build.resolve()
    base_plugin = args.base_plugin_python.resolve()
    ssl_extensions = args.openssl_extension_directory.resolve()
    output = args.output.resolve()
    library_source = python_source / "Lib"
    runtime_library = base_plugin / "libs"
    runtime_zip = runtime_library / "pythoncore.zip"
    output_libs = output / "libs"
    output_dlls = output / "dlls"

    for path in (library_source, runtime_library, ssl_extensions):
        if not path.is_dir():
            raise FileNotFoundError(path)
    for path in (runtime_zip, python_build / "python.exe", python_build / "python312.dll"):
        if not path.is_file():
            raise FileNotFoundError(path)
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f"Output directory must be new or empty: {output}")
    output_libs.mkdir(parents=True, exist_ok=True)
    output_dlls.mkdir(parents=True, exist_ok=True)

    shutil.copy2(python_build / "python312.dll", output_dlls / "python312.dll")
    libffi = python_build / "libffi-8.dll"
    if libffi.is_file() and (base_plugin / "dlls" / libffi.name).is_file():
        shutil.copy2(libffi, output_dlls / libffi.name)

    copied_extensions = []
    for installed_extension in sorted(runtime_library.glob("*.pyd")):
        if installed_extension.name.lower().startswith("mobase"):
            continue
        source_extension = (
            ssl_extensions / installed_extension.name
            if installed_extension.name.lower() in {"_ssl.pyd", "_hashlib.pyd"}
            else python_build / installed_extension.name
        )
        if not source_extension.is_file():
            raise FileNotFoundError(f"Missing Python extension: {source_extension}")
        destination = output_libs / installed_extension.name
        shutil.copy2(source_extension, destination)
        copied_extensions.append(destination)

    compiled_modules = 0
    with tempfile.TemporaryDirectory(prefix="mo2-pythoncore-") as temp_name:
        temp_root = Path(temp_name)
        output_zip = output_libs / "pythoncore.zip"
        with zipfile.ZipFile(runtime_zip, "r") as original, zipfile.ZipFile(
            output_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as rebuilt:
            if not original.infolist():
                raise RuntimeError("The embedded Python core archive is empty.")
            for old_info in original.infolist():
                name = old_info.filename
                relative = PurePosixPath(name)
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError(f"Unsafe path in embedded Python archive: {name}")
                payload = original.read(name)
                if name.endswith(".pyc"):
                    if len(payload) < 16 or payload[:4] != __import__("importlib.util").util.MAGIC_NUMBER:
                        raise RuntimeError(f"Unexpected Python bytecode format: {name}")
                    source_name = name[:-1]
                    source_path = library_source.joinpath(*PurePosixPath(source_name).parts)
                    if not source_path.is_file():
                        raise FileNotFoundError(source_path)
                    code_filename = marshal.loads(payload[16:]).co_filename
                    if Path(code_filename).is_absolute() or ".." in PurePosixPath(code_filename.replace("\\", "/")).parts:
                        raise RuntimeError(f"Unsafe source path in bytecode metadata: {code_filename}")
                    temp_path = temp_root.joinpath(*relative.parts)
                    temp_path.parent.mkdir(parents=True, exist_ok=True)
                    py_compile.compile(
                        str(source_path),
                        cfile=str(temp_path),
                        dfile=code_filename,
                        doraise=True,
                        optimize=2,
                    )
                    payload = temp_path.read_bytes()
                    compiled_modules += 1

                new_info = zipfile.ZipInfo(name, date_time=(2026, 10, 8, 0, 0, 0))
                new_info.compress_type = zipfile.ZIP_DEFLATED
                new_info.external_attr = old_info.external_attr
                new_info.create_system = old_info.create_system
                rebuilt.writestr(new_info, payload, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)

    if compiled_modules == 0:
        raise RuntimeError("No Python bytecode modules were rebuilt.")

    normalized_pdb_paths = 0
    for binary in sorted((*output_dlls.glob("*.dll"), *output_libs.glob("*.pyd"))):
        normalized_pdb_paths += normalize_pe_pdb_path(binary)

    included = [output_dlls / "python312.dll"]
    if (output_dlls / "libffi-8.dll").is_file():
        included.append(output_dlls / "libffi-8.dll")
    included.extend(copied_extensions)
    included.append(output_libs / "pythoncore.zip")
    manifest = {
        "format": 1,
        "python_version": "3.12.15",
        "openssl_version": args.openssl_version,
        "compiled_bytecode_modules": compiled_modules,
        "normalized_pdb_paths": normalized_pdb_paths,
        "files": {
            path.relative_to(output).as_posix(): sha256(path)
            for path in sorted(included, key=lambda item: item.relative_to(output).as_posix().lower())
        },
    }
    (output / "runtime-manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"Prepared Python {manifest['python_version']} overlay with {compiled_modules} rebuilt modules.")
    print(f"OpenSSL extension build: {manifest['openssl_version']}")
    print(f"Runtime manifest: {output / 'runtime-manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
