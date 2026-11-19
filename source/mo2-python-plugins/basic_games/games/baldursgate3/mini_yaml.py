"""MO2 Revamped: the YAML this plugin writes and reads, without PyYAML.

MO2's Python has no PyYAML, so "Convert JSONS to YAML" stopped with
"No module named 'yaml'" and the YAML-to-JSON setting did nothing. dump()
writes any JSON data as block YAML; load() reads that back, plus what people
write by hand in such files: comments, flow lists and maps, quoted and plain
scalars, | and > blocks, lists at their key's indentation. Anchors, tags,
multi-document files and multi-line plain or quoted scalars are not read
(ValueError); load_text() uses PyYAML instead when it's there.
"""

import json
import math
import re
from typing import Any, TextIO

# ---------------------------------------------------------------- writing

_plain_re = re.compile(r"[A-Za-z_][A-Za-z0-9_ .\-/()]*")
_special = {
    "y", "n", "yes", "no", "on", "off", "true", "false", "null", "~",
}  # fmt: skip


def _scalar(value: Any) -> str:
    if value is None:
        return "null"
    if value is True:
        return "true"
    if value is False:
        return "false"
    if isinstance(value, int):
        return str(value)
    if isinstance(value, float):
        if math.isnan(value):
            return ".nan"
        if math.isinf(value):
            return ".inf" if value > 0 else "-.inf"
        # MO2 Revamped: YAML 1.1 reads a float only with a "." ("1e+300"
        # stays text for PyYAML), so the mantissa always gets one
        text = repr(value)
        mantissa, e, exponent = text.partition("e")
        if "." not in mantissa:
            mantissa += ".0"
        return mantissa + e + exponent
    text = str(value)
    if (
        _plain_re.fullmatch(text)
        and not text.endswith(" ")
        and text.lower() not in _special
    ):
        return text
    # a JSON string is a YAML double-quoted scalar, once the characters YAML
    # takes for line breaks (U+0085, U+2028, U+2029) or doesn't allow in a
    # file (C1 controls, lone surrogates, U+FFFE/U+FFFF) are escaped too
    return _yaml_escape_re.sub(_yaml_escape, json.dumps(text, ensure_ascii=False))


_yaml_escape_re = re.compile("[\x7f-\x9f\u2028\u2029\ud800-\udfff\ufffe\uffff]")
_named_escapes = {"\x85": "\\N", "\u2028": "\\L", "\u2029": "\\P"}


def _yaml_escape(m: re.Match[str]) -> str:
    ch = m.group(0)
    if ch in _named_escapes:
        return _named_escapes[ch]
    code = ord(ch)
    return f"\\x{code:02x}" if code <= 0xFF else f"\\u{code:04x}"


def _emit(value: Any, indent: int, out: list[str]) -> None:
    pad = " " * indent
    if isinstance(value, dict):
        for key, item in value.items():
            head = f"{pad}{_scalar(key)}:"
            if isinstance(item, (dict, list)) and item:
                out.append(head)
                _emit(item, indent + 2, out)
            else:
                out.append(f"{head} {_inline(item)}")
    elif isinstance(value, list):
        for item in value:
            if isinstance(item, dict) and item:
                sub: list[str] = []
                _emit(item, indent + 2, sub)
                sub[0] = f"{pad}- " + sub[0][indent + 2 :]
                out.extend(sub)
            elif isinstance(item, list) and item:
                out.append(f"{pad}-")
                _emit(item, indent + 2, out)
            else:
                out.append(f"{pad}- {_inline(item)}")
    else:
        out.append(pad + _inline(value))


def _inline(value: Any) -> str:
    if isinstance(value, dict):
        return "{}"
    if isinstance(value, list):
        return "[]"
    return _scalar(value)


def dumps(data: Any) -> str:
    out: list[str] = []
    if isinstance(data, (dict, list)) and data:
        _emit(data, 0, out)
    else:
        out.append(_inline(data))
    return "\n".join(out) + "\n"


def dump(data: Any, stream: TextIO) -> None:
    stream.write(dumps(data))


# ---------------------------------------------------------------- reading

# MO2 Revamped: PyYAML's YAML 1.1 resolver: octal with a leading 0, binary,
# sexagesimal ("1:30"), and floats only with a "." and a signed exponent
# ("1e5" and "1.5e3" stay text there)
_int_re = re.compile(
    r"[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)"
    r"|[-+]?0x[0-9a-fA-F_]+|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+"
)
_float_re = re.compile(
    r"[-+]?(?:[0-9][0-9_]*)\.[0-9_]*(?:[eE][-+][0-9]+)?"
    r"|\.[0-9_]+(?:[eE][-+][0-9]+)?"
    r"|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*"
)
_escapes = {
    "0": "\0", "a": "\a", "b": "\b", "t": "\t", "\t": "\t", "n": "\n",
    "v": "\v", "f": "\f", "r": "\r", "e": "\x1b", " ": " ", '"': '"',
    "/": "/", "\\": "\\", "N": "\x85", "_": "\xa0", "L": " ",
    "P": " ",
}  # fmt: skip
_hex_len = {"x": 2, "u": 4, "U": 8}


def _sexagesimal(digits: str) -> float:
    value = 0.0
    for part in digits.split(":"):
        value = value * 60 + float(part)
    return value


def _resolve(text: str) -> Any:
    """A plain scalar as PyYAML's safe_load reads it (YAML 1.1 core); dates
    and times stay text (PyYAML makes date objects of them)."""
    if text in ("", "~", "null", "Null", "NULL"):
        return None
    if text in ("true", "True", "TRUE", "yes", "Yes", "YES", "on", "On", "ON"):
        return True
    if text in ("false", "False", "FALSE", "no", "No", "NO", "off", "Off", "OFF"):
        return False
    if _int_re.fullmatch(text):
        value = text.replace("_", "")
        sign = -1 if value[0] == "-" else 1
        value = value.lstrip("+-")
        if value == "0":
            return 0
        if value.startswith("0b"):
            return sign * int(value[2:], 2)
        if value.startswith("0x"):
            return sign * int(value[2:], 16)
        if ":" in value:
            return sign * int(_sexagesimal(value))
        if value[0] == "0":
            return sign * int(value, 8)
        return sign * int(value)
    if text in (".inf", ".Inf", ".INF", "+.inf", "+.Inf", "+.INF"):
        return math.inf
    if text in ("-.inf", "-.Inf", "-.INF"):
        return -math.inf
    if text in (".nan", ".NaN", ".NAN"):
        return math.nan
    if _float_re.fullmatch(text):
        value = text.replace("_", "").lower()
        sign = -1.0 if value[0] == "-" else 1.0
        value = value.lstrip("+-")
        if ":" in value:
            return sign * _sexagesimal(value)
        return sign * float(value)
    return text


def _double_quoted(text: str, i: int) -> tuple[str, int]:
    """The string starting at the quote at text[i], and the index after it."""
    out: list[str] = []
    i += 1
    while i < len(text):
        c = text[i]
        if c == '"':
            return "".join(out), i + 1
        if c == "\\":
            i += 1
            if i >= len(text):
                break
            e = text[i]
            if e in _hex_len:
                n = _hex_len[e]
                code = text[i + 1 : i + 1 + n]
                if len(code) != n:
                    raise ValueError("bad escape in a quoted string")
                out.append(chr(int(code, 16)))
                i += n
            elif e in _escapes:
                out.append(_escapes[e])
            else:
                raise ValueError(f"unknown escape \\{e}")
        else:
            out.append(c)
        i += 1
    raise ValueError("quoted string not closed on its line")


def _single_quoted(text: str, i: int) -> tuple[str, int]:
    out: list[str] = []
    i += 1
    while i < len(text):
        c = text[i]
        if c == "'":
            if text[i + 1 : i + 2] == "'":
                out.append("'")
                i += 2
                continue
            return "".join(out), i + 1
        out.append(c)
        i += 1
    raise ValueError("quoted string not closed on its line")


def _strip_comment(line: str) -> str:
    """The line without its # comment (one outside quotes, at the start or
    after a space)."""
    i = 0
    quote = ""
    while i < len(line):
        c = line[i]
        if quote == '"':
            if c == "\\":
                i += 2
                continue
            if c == '"':
                quote = ""
        elif quote == "'":
            if c == "'":
                quote = ""
        elif c in "\"'" and (i == 0 or line[i - 1] in " \t[{,:-"):
            quote = c
        elif c == "#" and (i == 0 or line[i - 1] in " \t"):
            return line[:i].rstrip()
        i += 1
    return line.rstrip()


class _Flow:
    """A flow collection ([a, b], {k: v}) or scalar on one logical line."""

    def __init__(self, text: str):
        self.text = text
        self.i = 0

    def _ws(self) -> None:
        while self.i < len(self.text) and self.text[self.i] in " \t":
            self.i += 1

    def value(self) -> Any:
        self._ws()
        t = self.text
        if self.i >= len(t):
            return None
        c = t[self.i]
        if c == "[":
            self.i += 1
            items: list[Any] = []
            while True:
                self._ws()
                if t[self.i : self.i + 1] == "]":
                    self.i += 1
                    return items
                items.append(self.value())
                self._ws()
                if t[self.i : self.i + 1] == ",":
                    self.i += 1
                elif t[self.i : self.i + 1] != "]":
                    raise ValueError("expected , or ] in a flow list")
        if c == "{":
            self.i += 1
            mapping: dict[Any, Any] = {}
            while True:
                self._ws()
                if t[self.i : self.i + 1] == "}":
                    self.i += 1
                    return mapping
                key = self.value()
                self._ws()
                if t[self.i : self.i + 1] == ":":
                    self.i += 1
                    mapping[key] = self.value()
                else:
                    mapping[key] = None
                self._ws()
                if t[self.i : self.i + 1] == ",":
                    self.i += 1
                elif t[self.i : self.i + 1] != "}":
                    raise ValueError("expected , or } in a flow map")
        if c == '"':
            s, self.i = _double_quoted(t, self.i)
            return s
        if c == "'":
            s, self.i = _single_quoted(t, self.i)
            return s
        start = self.i
        while self.i < len(t):
            ch = t[self.i]
            if ch in ",]}":
                break
            if ch == ":" and t[self.i + 1 : self.i + 2] in (" ", "", ",", "]", "}"):
                break
            self.i += 1
        return _resolve(t[start : self.i].strip())


def _scalar_value(text: str) -> Any:
    flow = _Flow(text)
    value = flow.value()
    flow._ws()
    if flow.i != len(text):
        if text[:1] in "[{\"'":
            raise ValueError(f"unexpected text after a value: {text!r}")
        return _resolve(text.strip())
    return value


def _split_key(content: str) -> tuple[Any, str] | None:
    """(key, rest) of a "key: value" line, or None if it isn't one."""
    if content[:1] in "\"'":
        reader = _double_quoted if content[0] == '"' else _single_quoted
        key, i = reader(content, 0)
        rest = content[i:].lstrip(" \t")
        if rest == ":" or rest.startswith(": ") or rest.startswith(":\t"):
            return key, rest[1:].strip()
        return None
    if content[:1] in "[{":
        return None
    m = re.search(r":(\s|$)", content)
    if not m:
        return None
    return _resolve(content[: m.start()].strip()), content[m.end() :].strip()


class _Block:
    def __init__(self, lines: list[tuple[int, str, str]]):
        # (indent, content without comment, raw line for | and > blocks)
        self.lines = lines
        self.pos = 0

    def peek(self) -> tuple[int, str, str] | None:
        return self.lines[self.pos] if self.pos < len(self.lines) else None

    def node(self, indent: int) -> Any:
        line = self.peek()
        if line is None or line[0] < indent:
            return None
        ind, content, _ = line
        if content == "-" or content.startswith("- "):
            return self.sequence(ind)
        if _split_key(content) is not None:
            return self.mapping(ind)
        self.pos += 1
        return self.inline(content, ind)

    def inline(self, text: str, indent: int) -> Any:
        if text[:1] in "|>":
            return self.block_scalar(text, indent)
        if text[:1] in "&*!":
            raise ValueError("anchors, aliases and tags aren't read")
        # a flow collection spread over several lines
        if text[:1] in "[{":
            while True:
                try:
                    return _scalar_value(text)
                except ValueError:
                    nxt = self.peek()
                    if nxt is None or nxt[0] <= indent:
                        raise
                    text += " " + nxt[1]
                    self.pos += 1
        nxt = self.peek()
        if nxt is not None and nxt[0] > indent:
            raise ValueError("multi-line scalars aren't read")
        return _scalar_value(text)

    def block_scalar(self, header: str, indent: int) -> str:
        folded = header[0] == ">"
        chomp = "-" if "-" in header else "+" if "+" in header else ""
        body: list[str] = []
        block_indent = None
        while self.pos < len(self.lines):
            ind, content, raw = self.lines[self.pos]
            if content and ind <= indent:
                break
            if block_indent is None and content:
                block_indent = ind
            body.append(raw)
            self.pos += 1
        cut = block_indent or 0
        text_lines = [line[cut:] if line.strip() else "" for line in body]
        if folded:
            text = ""
            for j, line in enumerate(text_lines):
                if j == 0:
                    text = line
                elif line == "" or text_lines[j - 1] == "":
                    text += "\n" + line if line else "\n"
                else:
                    text += " " + line
        else:
            text = "\n".join(text_lines)
        text = text.rstrip("\n")
        if chomp == "-":
            return text
        return text + "\n"

    def sequence(self, indent: int) -> list[Any]:
        items: list[Any] = []
        while True:
            line = self.peek()
            if line is None or line[0] != indent:
                break
            ind, content, raw = line
            if not (content == "-" or content.startswith("- ")):
                break
            rest = content[1:].lstrip(" ")
            if not rest:
                self.pos += 1
                nxt = self.peek()
                items.append(
                    self.node(nxt[0]) if nxt is not None and nxt[0] > indent else None
                )
                continue
            # "- key: value" or "- - x": the rest is a node at its own column
            column = ind + (len(content) - len(rest))
            if rest.startswith("- ") or rest == "-" or (
                rest[:1] not in "[{" and _split_key(rest) is not None
            ):
                self.lines[self.pos] = (column, rest, raw)
                items.append(self.node(column))
            else:
                self.pos += 1
                items.append(self.inline(rest, indent))
        return items

    def mapping(self, indent: int) -> dict[Any, Any]:
        result: dict[Any, Any] = {}
        while True:
            line = self.peek()
            if line is None or line[0] != indent:
                break
            split = _split_key(line[1])
            if split is None:
                if line[1] == "-" or line[1].startswith("- "):
                    break
                raise ValueError(f"expected 'key: value': {line[1]!r}")
            key, rest = split
            self.pos += 1
            if rest:
                result[key] = self.inline(rest, indent)
                continue
            nxt = self.peek()
            if nxt is not None and nxt[0] > indent:
                result[key] = self.node(nxt[0])
            elif (
                nxt is not None
                and nxt[0] == indent
                and (nxt[1] == "-" or nxt[1].startswith("- "))
            ):
                # a list at its key's indentation (PyYAML writes them so)
                result[key] = self.sequence(indent)
            else:
                result[key] = None
        return result


def loads(text: str) -> Any:
    """YAML text as data; ValueError for anything this reader doesn't read
    (a key that can't be one, nesting too deep)."""
    try:
        return _loads(text)
    except (TypeError, RecursionError) as e:
        raise ValueError(f"can't read this YAML: {e}") from e


def _loads(text: str) -> Any:
    if text.startswith("﻿"):
        text = text[1:]
    lines: list[tuple[int, str, str]] = []
    started = False
    # MO2 Revamped: lines end at "\n" only; str.splitlines() also broke them
    # at U+0085, U+2028 and U+2029 inside quoted text
    for raw in text.split("\n"):
        raw = raw.removesuffix("\r")
        if "\t" in raw[: len(raw) - len(raw.lstrip())]:
            raise ValueError("tabs in indentation")
        stripped = raw.strip()
        if stripped.startswith("%") and not started:
            continue
        if stripped in ("---", "..."):
            if started:
                raise ValueError("more than one document")
            continue
        content = _strip_comment(raw).strip()
        indent = len(raw) - len(raw.lstrip(" "))
        if content:
            started = True
        lines.append((indent, content, raw))
    return _parse_document(_Block(lines))


def _parse_document(block: _Block) -> Any:
    _drop_blank_lines_outside_blocks(block)
    if not block.lines:
        return None
    value = block.node(block.lines[0][0])
    if block.pos < len(block.lines):
        raise ValueError(f"unexpected line: {block.lines[block.pos][2]!r}")
    return value


def _drop_blank_lines_outside_blocks(block: _Block) -> None:
    """Blank lines only matter inside | and > blocks."""
    kept: list[tuple[int, str, str]] = []
    in_block_at: int | None = None
    for ind, content, raw in block.lines:
        if in_block_at is not None:
            if not content or ind > in_block_at:
                kept.append((ind, content, raw))
                continue
            in_block_at = None
        if not content:
            continue
        kept.append((ind, content, raw))
        tail = content.rsplit(" ", 1)[-1] if " " in content else content
        if re.fullmatch(r"[|>][-+0-9]*", tail) and (
            content.endswith(": " + tail) or content == tail or content.endswith("- " + tail)
        ):
            in_block_at = ind
    block.lines = kept


def load_text(text: str) -> Any:
    """YAML text as data: PyYAML's safe_load when it's installed, this
    module's reader otherwise."""
    try:
        import yaml  # type: ignore[import-untyped]
    except ImportError:
        return loads(text)
    return yaml.safe_load(text)
