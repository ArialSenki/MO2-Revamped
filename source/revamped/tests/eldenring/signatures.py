"""Every code signature the Elden Ring bridge and the me3 compatibility DLL
look for, searched offline in a game executable the way they search the
running game.

The bridge (eldenring_mo2_bridge.cpp) and me3_compat (src/*.rs) search the
game's image as Windows maps it; here the executable's sections are laid out
at their virtual addresses the same way. Each check says what the runtime
search would find, so a game update that breaks one is seen before anyone
starts the game. Keep each function in step with the C++ or Rust it names.

    py -3.12 -I source/revamped/tests/eldenring/signatures.py [exe ...]

With no arguments it scans every eldenring.exe and nightreign.exe below
D:\\[Backup] (ELDEN_RING_EXE_BACKUPS to change the folder).
"""

from __future__ import annotations

import os
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

BACKUPS = Path(os.environ.get("ELDEN_RING_EXE_BACKUPS", r"D:\[Backup]"))

IMAGE_SCN_CNT_INITIALIZED_DATA = 0x00000040
IMAGE_SCN_MEM_EXECUTE = 0x20000000
IMAGE_SCN_MEM_WRITE = 0x80000000


@dataclass
class Section:
    name: str
    rva: int
    size: int
    characteristics: int

    @property
    def executable(self) -> bool:
        return bool(self.characteristics & IMAGE_SCN_MEM_EXECUTE)

    @property
    def writable(self) -> bool:
        return bool(self.characteristics & IMAGE_SCN_MEM_WRITE)


@dataclass
class Image:
    """The executable as Windows maps it: each section at its virtual
    address (only its raw bytes; the rest of the section stays zero)."""

    path: Path
    data: bytes
    image_base: int
    sections: list[Section]
    file_version: str = ""

    def section(self, name: str) -> Section | None:
        return next((s for s in self.sections if s.name == name), None)

    def code(self) -> list[Section]:
        return [s for s in self.sections if s.executable]

    def in_code(self, rva: int) -> bool:
        return any(s.rva <= rva < s.rva + s.size for s in self.code())

    def in_writable_data(self, rva: int) -> bool:
        return any(
            s.writable and not s.executable and s.rva <= rva and rva - s.rva + 8 <= s.size
            for s in self.sections
        )


def load_image(path: Path) -> Image:
    raw = path.read_bytes()
    pe = struct.unpack_from("<I", raw, 0x3C)[0]
    if raw[:2] != b"MZ" or raw[pe:pe + 4] != b"PE\0\0":
        raise ValueError(f"{path} is not a PE file")
    count = struct.unpack_from("<H", raw, pe + 6)[0]
    optional_size = struct.unpack_from("<H", raw, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from("<H", raw, optional)[0] != 0x20B:
        raise ValueError(f"{path} is not a 64-bit image")
    image_base = struct.unpack_from("<Q", raw, optional + 24)[0]
    image_size = struct.unpack_from("<I", raw, optional + 56)[0]
    data = bytearray(image_size)
    headers = struct.unpack_from("<I", raw, optional + 60)[0]
    data[:headers] = raw[:headers]
    sections = []
    for index in range(count):
        entry = optional + optional_size + index * 40
        name = raw[entry:entry + 8].rstrip(b"\0").decode("latin-1")
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<IIII", raw, entry + 8)
        characteristics = struct.unpack_from("<I", raw, entry + 36)[0]
        if rva >= image_size:
            continue
        size = min(virtual_size or raw_size, image_size - rva)
        copied = min(size, raw_size)
        data[rva:rva + copied] = raw[raw_offset:raw_offset + copied]
        sections.append(Section(name, rva, size, characteristics))
    version = ""
    fixed = raw.find(struct.pack("<I", 0xFEEF04BD))
    if fixed >= 0:
        high, low = struct.unpack_from("<II", raw, fixed + 8)
        version = f"{high >> 16}.{high & 0xFFFF}.{low >> 16}.{low & 0xFFFF}"
    return Image(path, bytes(data), image_base, sections, version)


@dataclass
class Result:
    name: str
    ok: bool
    detail: str
    # a search that depends on the running game (decrypted code, tables
    # filled at start): offline it can only be partly checked
    runtime_only: bool = False


def _rel32(data: bytes, at: int) -> int:
    return struct.unpack_from("<i", data, at)[0]


def _follow_jumps(image: Image, rva: int) -> int | None:
    """install_loose_asset_hook: up to eight `jmp rel32` thunks."""
    for _ in range(9):
        if not image.in_code(rva):
            return None
        if image.data[rva] != 0xE9:
            return rva
        rva = rva + 5 + _rel32(image.data, rva + 1)
    return None


# --- the bridge --------------------------------------------------------------

# install_loose_asset_hook: the code around a call to the archive resolver
RESOLVER_CALL_SITES = (
    (b"", bytes.fromhex("48837B2008488D4B087203488B094C8B4B1841B8050000004D3BC8")),
    (bytes.fromhex("33D24889442420498BCD"), bytes.fromhex("48837F18087203488B3F48837B1805")),
)


def loose_asset_resolver(image: Image) -> Result:
    targets: set[int] = set()
    sites = 0
    data = image.data
    for section in image.code():
        start, end = section.rva, section.rva + section.size
        for before, after in RESOLVER_CALL_SITES:
            first = 5 + len(before)
            match = data.find(after, start + first, end)
            while match >= 0:
                if data[match - 5] == 0xE8 and data[match - first:match - 5] == before:
                    target = _follow_jumps(image, match + _rel32(data, match - 4))
                    if target is not None:
                        sites += 1
                        targets.add(target)
                match = data.find(after, match + 1, end)
    if len(targets) == 1:
        return Result("loose asset resolver", True,
                      f"{sites} call site(s) -> rva 0x{next(iter(targets)):X}")
    if not targets:
        return Result("loose asset resolver", False, "no call site matched")
    return Result("loose asset resolver", False, f"call sites disagree: {len(targets)} targets")


# find_resolver_by_argument_setup: the second way, when no call site matches
# the exact code (the instructions that set up the call's arguments)
RESOLVER_ARGUMENT_SETUPS = (
    re.compile(rb"(?s)\x4C\x8B[\xC0-\xC7]\x48\xC7\x44\x24.\x00\x00\x00\x00\x33\xD2\x49\x8B[\xC8-\xCF]\xE8"),
    re.compile(rb"(?s)\x4C\x8B[\xC0-\xC7]\x33\xD2\x48\x89\x44\x24.\x49\x8B[\xC8-\xCF]\xE8"),
)


def loose_asset_resolver_fallback(image: Image) -> Result:
    """The fallback on its own must reach the same function as the exact
    search, from at least two calls that agree."""
    exact = loose_asset_resolver(image)
    targets: dict[int, int] = {}
    data = image.data
    for section in image.code():
        end = section.rva + section.size
        for pattern in RESOLVER_ARGUMENT_SETUPS:
            for match in pattern.finditer(data, section.rva, end):
                call = match.end() - 1
                if call + 5 > end:
                    continue
                target = _follow_jumps(image, call + 5 + _rel32(data, call + 1))
                if target is not None:
                    targets[target] = targets.get(target, 0) + 1
    if len(targets) != 1:
        return Result("loose asset resolver (fallback)", False,
                      f"{len(targets)} different targets, the bridge needs 1")
    target, calls = next(iter(targets.items()))
    same = exact.ok and exact.detail.endswith(f"0x{target:X}")
    ok = calls >= 2 and same
    return Result("loose asset resolver (fallback)", ok,
                  f"{calls} call(s) -> rva 0x{target:X}"
                  + ("" if same else "; not the exact search's target"))


def _is_wwise_open_call_site(code: bytes, at: int) -> bool:
    """is_wwise_open_call_site, byte for byte."""
    if at + 26 > len(code) or code[at] != 0xE8 or code[at + 5] != 0x83 \
            or code[at + 6] != 0xF8 or code[at + 7] != 0x01:
        return False
    p = at + 8
    if code[p] == 0x74:
        p += 2
    elif code[p] == 0x0F and code[p + 1] == 0x84:
        p += 6
    else:
        return False
    if not (0x48 <= code[p] <= 0x4F and code[p + 1] == 0x83 and 0xC0 <= code[p + 2] <= 0xC7
            and code[p + 3] == 0x38):
        return False
    p += 4
    if not (0x48 <= code[p] <= 0x4F and code[p + 1] == 0x83):
        return False
    if code[p + 2] == 0x7D:
        p += 4
    elif code[p + 2] == 0xBD:
        p += 7
    else:
        return False
    return code[p] == 0x08


WWISE_CALL = re.compile(rb"(?s)\xE8.{4}\x83\xF8\x01")


def wwise_open(image: Image, nightreign: bool) -> Result:
    expected = 2 if nightreign else 1
    targets: set[int] = set()
    data = image.data
    for section in image.code():
        for match in WWISE_CALL.finditer(data, section.rva, section.rva + section.size):
            at = match.start()
            if not _is_wwise_open_call_site(data, at):
                continue
            target = at + 5 + _rel32(data, at + 1)
            if image.in_code(target):
                targets.add(target)
    ok = len(targets) == expected
    return Result("Wwise open function", ok,
                  f"{len(targets)} function(s), the bridge expects {expected}")


# apply_skip_intro_logos (Elden Ring): the masked signature and the jz 60
# bytes before it
SKIP_LOGOS = re.compile(
    rb"(?s)\xC6.{5}\x01.\x03\x00\x00\x00.\x8B.\xE8.{4}\xE9.{5}\x8D")


def skip_logos_jump(image: Image) -> Result:
    data = image.data
    matches = []
    for section in image.code():
        for match in SKIP_LOGOS.finditer(data, section.rva + 60, section.rva + section.size):
            matches.append(match.start())
    # overlapping matches count too in the bridge's byte-by-byte scan
    if len(matches) != 1:
        return Result("intro logos (jump)", False, f"{len(matches)} match(es), the bridge needs 1")
    jump = matches[0] - 60
    ok = data[jump] == 0x74
    return Result("intro logos (jump)", ok,
                  f"one match; byte at the jump 0x{data[jump]:02X} (needs 0x74)")


def skip_logos_step_table(image: Image) -> Result:
    """apply_skip_logo_step (Nightreign): the step name once in .rdata, and
    the .data entry that points to it."""
    rdata, data_section = image.section(".rdata"), image.section(".data")
    if rdata is None or data_section is None:
        return Result("intro logos (step table)", False, "no .rdata or .data section")
    name = "TitleStep::STEP_BeginLogo".encode("utf-16-le")
    region = image.data[rdata.rva:rdata.rva + rdata.size]
    found = [m.start() for m in re.finditer(re.escape(name), region) if m.start() % 2 == 0]
    if len(found) != 1:
        return Result("intro logos (step table)", False, f"step name found {len(found)} time(s)")
    pointer = struct.pack("<Q", image.image_base + rdata.rva + found[0])
    table = image.data[data_section.rva:data_section.rva + data_section.size]
    entries = [m.start() for m in re.finditer(re.escape(pointer), table) if m.start() % 8 == 0]
    if len(entries) == 1:
        return Result("intro logos (step table)", True, "step name and its table entry found once")
    # the table can be filled as the game starts (the bridge tries again)
    return Result("intro logos (step table)", True,
                  f"step name found once; {len(entries)} table entries on disk (filled at start)",
                  runtime_only=True)


def save_extension_literal(image: Image) -> Result:
    """apply_save_extension_setting: one ".sl2" in constant data, even offset."""
    pattern = ".sl2".encode("utf-16-le")
    matches = 0
    for section in image.sections:
        if section.characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_WRITE) \
                or not section.characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA:
            continue
        region = image.data[section.rva:section.rva + section.size]
        matches += sum(1 for m in re.finditer(re.escape(pattern), region) if m.start() % 2 == 0)
    return Result(".sl2 save extension", matches == 1, f"{matches} literal(s), the bridge needs 1")


COOKIE_CHECK = re.compile(rb"(?s)\x48\x8B\x05(.{4})\x48\x33\xC4")


def security_cookie(image: Image) -> Result:
    """find_game_security_cookie: an address read by 64 stack checks."""
    counts: dict[int, int] = {}
    data = image.data
    for section in image.code():
        for match in COOKIE_CHECK.finditer(data, section.rva, section.rva + section.size):
            cookie = match.start() + 7 + _rel32(data, match.start() + 3)
            counts[cookie] = counts.get(cookie, 0) + 1
    if not counts:
        return Result("security cookie", False, "no stack check found")
    cookie, references = max(counts.items(), key=lambda item: item[1])
    ok = references >= 64 and image.in_writable_data(cookie)
    return Result("security cookie", ok, f"rva 0x{cookie:X}, {references} stack checks")


def exit_check(image: Image) -> Result:
    """skip_game_exit_check: cmp byte [rdx+18h],0 / je +1 / int3 / ..."""
    pattern = bytes.fromhex("807A18007401CC488B4F0841B828")
    data = image.data
    found = sum(len(list(re.finditer(re.escape(pattern), data[s.rva:s.rva + s.size])))
                for s in image.code())
    return Result("exit check (int3)", found >= 1, f"{found} place(s)")


def long_sleep(image: Image) -> Result:
    """called_from_game_long_sleep: mov ecx,16666 / call [rip+disp32]."""
    pattern = bytes.fromhex("B91A410000FF15")
    data = image.data
    found = sum(len(list(re.finditer(re.escape(pattern), data[s.rva:s.rva + s.size])))
                for s in image.code())
    return Result("16.7 s sleep", found >= 1, f"{found} caller(s)")


# --- me3_compat (Elden Ring only: Reforged's mem_patch) ------------------------

SYSTEM_ALLOCATOR = re.compile(
    rb"(?s)\xe8.{4}\x48\x8b\x74\x24\x30\x48\x8b\x5c\x24\x38\x48\x83\xc4\x20\x5f\xe9(.{4})")
FIRST_ALLOCATOR = re.compile(rb"(?s)\x48\x89\x05(.{4})\x4c\x8b\xc0\xba\x08\x00\x00\x00\x8d\x4a\x08")
LAST_ALLOCATOR = re.compile(
    rb"(?s)\x48\x89\x3d(.{4})\xc7\x44\x24\x20\xff\xff\xff\xff\x45\x33\xc9\x4c\x8b\xc7\x48\x8d\x15.{4}")


def _captures(pattern: re.Pattern, data: bytes, start: int = 0, end: int | None = None):
    end = len(data) if end is None else end
    return [(m.start(1), m.end(1)) for m in pattern.finditer(data, start, end)]


def mem_patch_system_allocator(image: Image) -> Result:
    text = image.section(".text")
    if text is None:
        return Result("mem_patch: system allocator", False, "no .text")
    data = image.data
    found = _captures(SYSTEM_ALLOCATOR, data, text.rva, text.rva + text.size)
    return Result("mem_patch: system allocator", len(found) == 1,
                  f"{len(found)} match(es) in .text, me3_compat needs 1", runtime_only=not found)


def mem_patch_allocator_table(image: Image) -> Result:
    data = image.data
    first = _captures(FIRST_ALLOCATOR, data)
    if len(first) != 1:
        return Result("mem_patch: allocator table", False,
                      f"first allocator matched {len(first)} time(s)", runtime_only=not first)
    first_rva = first[0][1] + _rel32(data, first[0][0])
    lasts = sorted(end + _rel32(data, start) for start, end in _captures(LAST_ALLOCATOR, data))
    if not lasts:
        return Result("mem_patch: allocator table", False, "last allocator not found",
                      runtime_only=True)
    count = (lasts[-1] - first_rva) // 8 + 1
    ok = first_rva <= lasts[-1] and count <= 256
    return Result("mem_patch: allocator table", ok, f"{count} entries (limit 256)")


# BootBoost (me3_compat boot_boost.rs, from me3's mod-host-assets ebl.rs at
# the revision Cargo.toml pins): MountEbl, from the call that mounts each
# archive, or else from the whole small function that calls it. The first
# match is used.
MOUNT_EBL = re.compile(
    rb"(?s)\x48\x8b\x45.\x48\x89\x44\x24\x28[\x48|\x4c]\x89[\x44\x4c\x54\x5c\x64\x6c\x74\x7c]\x24\x20"
    rb"\x4c\x8b\x0d.{4}[\x48|\x49]\x8b[\xd0-\xd7][\x48|\x49]\x8b[\xc8-\xcf]\xe8(.{4})\x0f\xb6\xd8"
    rb"(?:(?:\x48\x83\x7d.\x08)|(?:\x48\x83\x7c\x24.\x08))\x72.")
MOUNT_EBL_ALTERNATIVE = re.compile(
    rb"(?s)\x53\x48\x83\xec\x30\x48\x8b\x44\x24\x70\x4d\x8b\xd1\x4c\x8b\x4c\x24\x60\x4d\x8b\xd8"
    rb"\x48\x89\x44\x24\x28\x48\x8b\xca\x48\x8b\x44\x24\x68\x4d\x8b\xc2\x49\x8b\xd3\x48\x89\x44"
    rb"\x24\x20\xe8(.{4})\x48\x83\xc4\x30\x5b\xc3")


def boot_boost_mount_ebl(image: Image) -> Result:
    text = image.section(".text")
    if text is None:
        return Result("BootBoost: MountEbl", False, "no .text")
    data = image.data
    found = []
    for name, pattern in (("call site", MOUNT_EBL), ("caller", MOUNT_EBL_ALTERNATIVE)):
        matches = list(pattern.finditer(data, text.rva, text.rva + text.size))
        if matches:
            first = matches[0]
            target = first.end(1) + _rel32(data, first.start(1))
            targets = {m.end(1) + _rel32(data, m.start(1)) for m in matches}
            found.append((name, target, len(matches), len(targets)))
    if not found:
        return Result("BootBoost: MountEbl", False, "neither of me3's searches matches")
    name, target, count, distinct = found[0]
    ok = image.in_code(target) and distinct == 1
    return Result("BootBoost: MountEbl", ok,
                  f"by its {name}: {count} match(es), {distinct} target(s) -> rva 0x{target:X}")


@dataclass
class Report:
    image: Image
    nightreign: bool
    results: list[Result] = field(default_factory=list)

    @property
    def failures(self) -> list[Result]:
        return [r for r in self.results if not r.ok]


def scan(path: Path) -> Report:
    image = load_image(path)
    nightreign = path.name.casefold() == "nightreign.exe"
    report = Report(image, nightreign)
    checks = [
        loose_asset_resolver(image),
        loose_asset_resolver_fallback(image),
        wwise_open(image, nightreign),
        save_extension_literal(image),
        security_cookie(image),
        exit_check(image),
        long_sleep(image),
        boot_boost_mount_ebl(image),
    ]
    if nightreign:
        checks.append(skip_logos_step_table(image))
    else:
        checks += [
            skip_logos_jump(image),
            mem_patch_system_allocator(image),
            mem_patch_allocator_table(image),
        ]
    report.results = checks
    return report


def backup_executables(root: Path = BACKUPS) -> list[Path]:
    if not root.is_dir():
        return []
    found = []
    for folder in sorted(root.iterdir()):
        for name in ("eldenring.exe", "nightreign.exe"):
            candidate = folder / name
            if candidate.is_file():
                found.append(candidate)
    return found


def main(arguments: list[str]) -> int:
    paths = [Path(a) for a in arguments] or backup_executables()
    if not paths:
        print(f"no game executables found below {BACKUPS}")
        return 2
    failed = 0
    for path in paths:
        report = scan(path)
        print(f"\n{path} ({report.image.file_version})")
        for result in report.results:
            mark = "ok  " if result.ok else "FAIL"
            note = " (needs the running game to confirm)" if result.runtime_only else ""
            print(f"  {mark} {result.name}: {result.detail}{note}")
        failed += len(report.failures)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
