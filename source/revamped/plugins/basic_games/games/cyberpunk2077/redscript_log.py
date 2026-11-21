"""redscript's compile log: which scripts failed, so MO2 can name their mods.

redscript writes r6/logs/redscript_rCURRENT.log (through the VFS, in
Overwrite) each time the game starts. An error is a line starting
"[ERROR - <date>]", followed by its indented details; the script and line
it is about appear as "<path>.reds:<line>:<column>".
"""

from __future__ import annotations

import re
from dataclasses import dataclass

LOG = "r6/logs/redscript_rCURRENT.log"
_HEADER = re.compile(r"^\[(ERROR|WARN|INFO|DEBUG)[^\]]*\]\s?(.*)$")
# redscript names the scripts in r6/scripts relative to it ("Mod\main.reds")
_RELATIVE_SCRIPT = re.compile(r"(?:^|\s)([^\s:\\/][^\r\n:]{0,260}?\.reds):(\d+)", re.IGNORECASE)
_SCRIPT = re.compile(r"([A-Za-z]:[\\/][^\r\n:]{0,260}?\.reds|[\\/]{2}[^\r\n:]{0,260}?\.reds)(?::(\d+))?", re.IGNORECASE)
# MO2 Revamped: the paths above are bounded (260 characters is the most a
# Windows path has), so a long line of repeated starts can't make the lazy
# scans quadratic. A real log is a few KB: only its last 1 MB is read, each
# line cut to 1000 characters
_LOG_LIMIT = 1024 * 1024
_LINE_LIMIT = 1000
# text kept of one error
_MESSAGE_LIMIT = 300


@dataclass(frozen=True)
class ScriptError:
    script: str  # relative to the game folder, with forward slashes ("" if none named)
    line: int
    message: str


def _relative(path: str, game_dir: str) -> str:
    normalized = path.replace("\\", "/")
    root = game_dir.replace("\\", "/").rstrip("/") + "/"
    if normalized.casefold().startswith(root.casefold()):
        return normalized[len(root):]
    # a path outside the game folder: from its r6/ or red4ext/ on
    lowered = normalized.casefold()
    for marker in ("/r6/scripts/", "/red4ext/plugins/"):
        index = lowered.find(marker)
        if index >= 0:
            return normalized[index + 1:]
    return normalized


def _lines(text: str) -> list[str]:
    return [line[:_LINE_LIMIT] for line in text[-_LOG_LIMIT:].splitlines()]


def parse(text: str, game_dir: str) -> list[ScriptError]:
    """The errors of a compile log, one per script and line."""
    blocks: list[list[str]] = []
    current: list[str] | None = None
    for raw in _lines(text):
        header = _HEADER.match(raw)
        if header:
            current = [header.group(2)] if header.group(1) == "ERROR" else None
            if current is not None:
                blocks.append(current)
        elif current is not None:
            current.append(raw)
    errors: list[ScriptError] = []
    seen: set[tuple[str, int]] = set()
    for block in blocks:
        body = "\n".join(line.rstrip() for line in block).strip()
        match = _SCRIPT.search(body)
        if match:
            script = _relative(match.group(1), game_dir)
            line = int(match.group(2)) if match.group(2) else 0
        elif relative := _RELATIVE_SCRIPT.search(body):
            name = re.sub(r"^At\s+", "", relative.group(1).strip())
            script = "r6/scripts/" + name.replace("\\", "/")
            line = int(relative.group(2))
        else:
            script, line = "", 0
        if not script and "compilation" in body.casefold():
            # the summary at the end ("REDscript compilation has failed")
            continue
        key = (script.casefold(), line)
        if key in seen:
            continue
        seen.add(key)
        message = " ".join(part.strip() for part in body.splitlines() if part.strip() and set(part.strip()) != {"^"})
        errors.append(ScriptError(script, line, message[:_MESSAGE_LIMIT]))
    return errors


def failed(text: str) -> bool:
    """Whether the log ends with a failed compilation."""
    for raw in reversed(_lines(text)):
        header = _HEADER.match(raw)
        if header:
            if "compilation" in header.group(2).casefold() or header.group(1) == "ERROR":
                return header.group(1) == "ERROR" or "fail" in header.group(2).casefold()
    return False
