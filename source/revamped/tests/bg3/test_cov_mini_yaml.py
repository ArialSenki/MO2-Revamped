"""The plugin's own YAML writer and reader (mini_yaml), and the two places
that use it: the YAML-to-JSON conversion at start and "Convert JSONS to YAML"."""

from __future__ import annotations

import io
import json
import math
import os
import random
import shutil
import tempfile
import time
import unittest
from pathlib import Path

from basic_games.games.baldursgate3 import mapper, mini_yaml
from basic_games.games.baldursgate3.plugins import tools


def same(a, b) -> bool:
    """Equal, with the types kept (1 is not True, 1.0 is not 1) and NaN equal to NaN."""
    if isinstance(a, float) and isinstance(b, float) and math.isnan(a):
        return math.isnan(b)
    if type(a) is not type(b):
        return False
    if isinstance(a, dict):
        return list(a) == list(b) and all(same(a[k], b[k]) for k in a)
    if isinstance(a, list):
        return len(a) == len(b) and all(same(x, y) for x, y in zip(a, b))
    return a == b


class RoundTrip(unittest.TestCase):
    def check(self, data) -> None:
        text = mini_yaml.dumps(data)
        self.assertTrue(same(mini_yaml.loads(text), data), f"{data!r} written as {text!r}")

    def test_empty_containers_and_scalars(self):
        for data in ({}, [], None, True, False, 0, -7, 2**70, "", {"a": {}}, {"a": []}, [[]], [{}], [None]):
            self.check(data)

    def test_strings_that_look_like_other_things(self):
        # YAML 1.1 reads these as booleans, nulls or numbers when plain
        for text in ("yes", "No", "ON", "off", "y", "n", "true", "False", "null", "~", "1", "-1",
                     "1.0", "0x1F", "010", "0b11", "1:30", "1e5", "1_000", ".nan", "-.inf", "+1"):
            self.check(text)
            self.check({text: text})

    def test_keys_needing_quotes(self):
        self.check({"key: colon": 1, "#hash": 2, "a #b": 3, "-dash": 4, "[x": 5, "{y": 6, "": 7,
                    "'q": 8, '"dq': 9, "multi\nline": 10, "tab\tkey": 11, "trailing ": 12, " lead": 13,
                    "? q": 14, "&a": 15, "*b": 16, "!t": 17, "%d": 18, "|": 19, ">": 20, "@": 21})

    def test_unicode_and_controls(self):
        self.check({"accents": "Ánimo ñandú", "cjk": "日本語", "emoji": "\U0001F600",
                    "controls": "\x00\x07\x1b\x7f\x85\x9f", "breaks": "a\u2028b\u2029c",
                    "bom": "\ufeffx", "nonchar": "\ufffe\uffff", "backslash": "a\\b\\"})

    def test_multi_line_strings(self):
        for text in ("one\ntwo", "ends with a break\n", "\n", "two breaks\n\n", "crlf\r\nline", "  spaces  "):
            self.check(text)
            self.check({"k": text, "l": [text]})

    def test_numbers_stay_numbers(self):
        self.check([1, "1", 1.5, "1.5", 1e300, 1e-7, -0.0, 3.0, 1e16, math.inf, -math.inf, 0.1 + 0.2])
        self.assertTrue(math.isnan(mini_yaml.loads(mini_yaml.dumps(math.nan))))
        # the mantissa gets a "." so PyYAML reads a float too
        self.assertEqual(mini_yaml.dumps(1e300), "1.0e+300\n")

    def test_nested_lists_and_maps(self):
        self.check({"nested": [[[1]], [[{"a": [[]]}]], [{"b": {"c": [1, {"d": None}]}}]]})
        self.check([{"a": 1, "b": [1, 2]}, {"c": {}}, [], [[1, 2], [3]]])

    def test_windows_and_proton_paths(self):
        self.check({"win": "C:\\Users\\José\\Documents", "proton": "Z:\\home\\deck\\.steam",
                    "slashes": "Z:/home/deck/x.pak", "linux": "/home/deck/.local/share/Steam"})

    def test_random_data(self):
        rng = random.Random(1234)
        alphabet = list("ab -:#'\",[]{}|>&*!%@`?\n\t\\/.0123+_eE~") + ["\u2028", "\x85", "é", "\ufeff", "\r"]

        def text():
            return "".join(rng.choice(alphabet) for _ in range(rng.randint(0, 8)))

        def value(depth=0):
            r = rng.random()
            if depth > 3 or r < 0.5:
                return rng.choice([None, True, False, rng.randint(-1000, 1000), rng.uniform(-1e5, 1e5), text(), text()])
            if r < 0.75:
                return [value(depth + 1) for _ in range(rng.randint(0, 4))]
            return {text(): value(depth + 1) for _ in range(rng.randint(0, 4))}

        for _ in range(2000):
            self.check(value())

    def test_dump_to_a_stream(self):
        out = io.StringIO()
        mini_yaml.dump({"a": [1, 2]}, out)
        self.assertEqual(out.getvalue(), "a:\n  - 1\n  - 2\n")
        # a dict in a list starts on the dash's line
        self.assertEqual(mini_yaml.dumps([{"a": 1, "b": 2}]), "- a: 1\n  b: 2\n")
        self.assertEqual(mini_yaml.dumps([[1]]), "-\n  - 1\n")


class Reading(unittest.TestCase):
    def test_comments(self):
        text = "# top\na: 1 # c\nb: 'x # not' # c\nc: \"y # no\"\nd: x#y\n# end\n"
        self.assertEqual(mini_yaml.loads(text), {"a": 1, "b": "x # not", "c": "y # no", "d": "x#y"})

    def test_doubled_quote_then_hash_in_a_single_quoted_string(self):
        # '' inside a single-quoted string was taken as its end, so " # x'"
        # was cut off as a comment and the string never closed
        self.assertEqual(mini_yaml.loads("a: 'it''s # x'\n"), {"a": "it's # x"})
        self.assertEqual(mini_yaml.loads("'it''s': 2\n"), {"it's": 2})

    def test_flow_collections(self):
        text = "a: [1, two, 'three', \"four\", [5], {k: v}]\nb: {x: 1, y: [2, 3], z}\nc: []\nd: {}\ne: [1, 2, ]\n"
        self.assertEqual(
            mini_yaml.loads(text),
            {"a": [1, "two", "three", "four", [5], {"k": "v"}], "b": {"x": 1, "y": [2, 3], "z": None},
             "c": [], "d": {}, "e": [1, 2]},
        )
        self.assertEqual(mini_yaml.loads("a: {u: http://x, v: 1}\n"), {"a": {"u": "http://x", "v": 1}})
        self.assertEqual(mini_yaml.loads("a: [x#y, 'z']\n"), {"a": ["x#y", "z"]})

    def test_flow_over_several_lines(self):
        self.assertEqual(mini_yaml.loads("a: [1,\n  2,\n  3]\nb: {x: 1,\n  y: 2}\n"), {"a": [1, 2, 3], "b": {"x": 1, "y": 2}})

    def test_list_at_its_keys_indentation(self):
        self.assertEqual(mini_yaml.loads("Features:\n- A\n- B\nOther: 1\n"), {"Features": ["A", "B"], "Other": 1})

    def test_block_scalars(self):
        text = "lit: |\n  line1\n  line2\n\n  line4\nfold: >\n  a\n  b\n\n  c\nkeep: |+\n  k\n\nstrip: |-\n  s\nend: 1\n"
        self.assertEqual(
            mini_yaml.loads(text),
            {"lit": "line1\nline2\n\nline4\n", "fold": "a b\n\nc\n", "keep": "k\n", "strip": "s", "end": 1},
        )
        self.assertEqual(mini_yaml.loads("- |\n  a\n  b\n- >\n  c\n  d\n"), ["a\nb\n", "c d\n"])
        self.assertEqual(mini_yaml.loads("|\n  top\n"), "top\n")
        self.assertEqual(mini_yaml.loads("a: |\n  # not a comment\n  b\n"), {"a": "# not a comment\nb\n"})

    def test_block_indentation_indicator(self):
        # the text starts N columns in from its parent: its own leading spaces stay
        self.assertEqual(mini_yaml.loads("a: |2\n    x\n  y\nb: 1\n"), {"a": "  x\ny\n", "b": 1})
        self.assertEqual(mini_yaml.loads("a: >2\n   x\nb: 1\n"), {"a": " x\n", "b": 1})
        self.assertEqual(mini_yaml.loads("a: |-2\n   x\n"), {"a": " x"})
        self.assertEqual(mini_yaml.loads("- |+2\n   x\n- |-1\n  y\n"), [" x\n", " y"])
        self.assertEqual(mini_yaml.loads("k:\n  n: |1\n     x\n   y\nm: 2\n"), {"k": {"n": "  x\ny\n"}, "m": 2})
        self.assertEqual(mini_yaml.loads("|2\n   top\n"), " top\n")

    def test_comment_after_a_block_scalar_ends_it(self):
        # the comment line was read as part of the block, cut at its column
        self.assertEqual(mini_yaml.loads("a: |\n  x\n# c\nb: 1\n"), {"a": "x\n", "b": 1})
        text = "a:\n  b: |\n    x\n  # comment\n  c: 1\n"
        self.assertEqual(mini_yaml.loads(text), {"a": {"b": "x\n", "c": 1}})

    def test_sequences_of_maps_and_lists(self):
        text = "- name: a\n  v: 1\n- name: b\n  sub:\n    - x\n    - y\n-\n  - nested\n- - inner\n  - inner2\n-\n"
        self.assertEqual(
            mini_yaml.loads(text),
            [{"name": "a", "v": 1}, {"name": "b", "sub": ["x", "y"]}, ["nested"], ["inner", "inner2"], None],
        )

    def test_bom_crlf_and_document_markers(self):
        self.assertEqual(mini_yaml.loads("\ufeffa: 1\r\nb:\r\n  - x\r\n"), {"a": 1, "b": ["x"]})
        self.assertEqual(mini_yaml.loads("%YAML 1.1\n---\na: 1\n"), {"a": 1})
        # one document between --- and ... was refused as two
        self.assertEqual(mini_yaml.loads("---\na: 1\n...\n"), {"a": 1})
        self.assertEqual(mini_yaml.loads("a: 1\n...\n# after the end\n"), {"a": 1})

    def test_empty_documents(self):
        for text in ("", "\n\n", "# nothing\n", "---\n", "   \n"):
            self.assertIsNone(mini_yaml.loads(text), repr(text))

    def test_scalars_as_pyyaml_reads_them(self):
        text = (
            "o: 010\nh: 0x1f\nb: 0b11\ns: 1:30\nsf: 1:30.5\nf: 1.5e+3\nt: 1e5\nn: .NaN\ni: -.inf\npi: +.inf\n"
            "u: 1_000\nd: 2024-01-01\nneg: -0x10\nz: 0\nnull1: ~\nnull2: Null\nyes: Yes\noff: OFF\ndot: .5\n"
        )
        data = mini_yaml.loads(text)
        self.assertEqual(
            {k: v for k, v in data.items() if k != "n"},
            {"o": 8, "h": 31, "b": 3, "s": 90, "sf": 90.5, "f": 1500.0, "t": "1e5", "i": -math.inf,
             "pi": math.inf, "u": 1000, "d": "2024-01-01", "neg": -16, "z": 0, "null1": None, "null2": None,
             True: True, False: False, "dot": 0.5},
        )
        self.assertTrue(math.isnan(data["n"]))

    def test_double_quoted_escapes(self):
        text = 'a: "\\x41\\u00e9\\U0001F600\\t\\N\\_\\L\\P\\0\\e\\/\\ \\"\\\\"\n'
        self.assertEqual(mini_yaml.loads(text), {"a": "A\u00e9\U0001F600\t\x85\xa0\u2028\u2029\0\x1b/ \"\\"})

    def test_paths(self):
        text = "p: C:\\Users\\x\nq: 'Z:\\home\\u'\nr: /home/u/.local\nurl: http://x.y/z\n"
        self.assertEqual(
            mini_yaml.loads(text),
            {"p": "C:\\Users\\x", "q": "Z:\\home\\u", "r": "/home/u/.local", "url": "http://x.y/z"},
        )

    def test_what_isnt_read_is_a_value_error(self):
        for text in (
            "a:\n\tb: 1\n",  # tabs
            "a: 1\n---\nb: 2\n",  # two documents
            "a: 1\n...\nb: 2\n",
            "a: &x 1\n", "a: *x\n", "a: !!str 1\n",
            "a: b\n  c\n",  # multi-line plain scalar
            "a: 1\n b: 2\n",
            'a: "x\n', "a: 'x\n", 'a: "x\\',  # not closed
            'a: "\\q"\n', 'a: "\\x4"\n',  # bad escapes
            "a: [1, 2\n", "a: {x: 1\n", "a: [1] x\n",
            "a: 1\njunk\n",
            "[" * 3000 + "]" * 3000,  # too deep
            "{[1]: 2}\n",  # a key that can't be one
        ):
            with self.assertRaises(ValueError, msg=repr(text)):
                mini_yaml.loads(text)

    def test_load_text(self):
        import sys
        import types

        saved = sys.modules.get("yaml")
        try:
            # without PyYAML: this module's reader
            sys.modules["yaml"] = None  # type: ignore[assignment]
            self.assertEqual(mini_yaml.load_text("a: [1, 2]\n"), {"a": [1, 2]})
            # with it: PyYAML's safe_load
            fake = types.ModuleType("yaml")
            fake.safe_load = lambda text: ("pyyaml", text)
            sys.modules["yaml"] = fake
            self.assertEqual(mini_yaml.load_text("a: 1\n"), ("pyyaml", "a: 1\n"))
        finally:
            if saved is None:
                sys.modules.pop("yaml", None)
            else:
                sys.modules["yaml"] = saved


class Tmp(unittest.TestCase):
    def setUp(self) -> None:
        self.tmp = Path(tempfile.mkdtemp(prefix="bg3-yaml-"))

    def tearDown(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


class YamlToJson(Tmp):
    def test_converted_once_and_again_when_newer(self):
        yaml_file = self.tmp / "Config.yaml"
        yaml_file.write_text("\ufeffEnabled: yes\nList: [1, 2]\n", encoding="utf-8")
        out = mapper.convert_yaml(yaml_file)
        self.assertEqual(out, self.tmp / "Config.json")
        self.assertEqual(json.loads(out.read_text(encoding="utf-8")), {"Enabled": True, "List": [1, 2]})
        # the JSON is newer: kept as it is
        out.write_text("{}", encoding="utf-8")
        self.assertEqual(mapper.convert_yaml(yaml_file), out)
        self.assertEqual(out.read_text(encoding="utf-8"), "{}")
        future = time.time() + 10
        yaml_file.write_text("Enabled: no\n", encoding="utf-8")
        os.utime(yaml_file, (future, future))
        mapper.convert_yaml(yaml_file)
        self.assertEqual(json.loads(out.read_text(encoding="utf-8")), {"Enabled": False})

    def test_bad_yaml_leaves_no_json(self):
        for name, data in (("bad.yaml", "a: &x 1\n".encode()), ("nan.yml", b"a: .nan\n"), ("latin.yaml", "a: \xe9\n".encode("latin-1"))):
            file = self.tmp / name
            file.write_bytes(data)
            self.assertIsNone(mapper.convert_yaml(file), name)
            self.assertFalse(file.with_suffix(".json").exists(), name)
            self.assertFalse(list(self.tmp.glob("*.part")))

    def test_missing_file(self):
        self.assertIsNone(mapper.convert_yaml(self.tmp / "gone.yaml"))


class JsonToYaml(Tmp):
    def test_converts_bom_json_and_skips_bad_files(self):
        (self.tmp / "sub").mkdir()
        (self.tmp / "sub" / "a.json").write_text('\ufeff{"Name": "Ánimo", "On": true, "n": [1, 2.5]}', encoding="utf-8")
        (self.tmp / "broken.json").write_text("{not json", encoding="utf-8")
        (self.tmp / "latin.json").write_bytes('{"a": "\xe9"}'.encode("latin-1"))
        tools.convert_jsons_in_dir_to_yaml(self.tmp)
        converted = self.tmp / "sub" / "a.yaml"
        self.assertEqual(mini_yaml.loads(converted.read_text(encoding="utf-8")), {"Name": "Ánimo", "On": True, "n": [1, 2.5]})
        self.assertFalse((self.tmp / "broken.yaml").exists())
        self.assertFalse((self.tmp / "latin.yaml").exists())

    def test_newer_yaml_is_kept(self):
        (self.tmp / "a.json").write_text('{"a": 1}', encoding="utf-8")
        yaml_file = self.tmp / "a.yaml"
        yaml_file.write_text("mine: 1\n", encoding="utf-8")
        future = time.time() + 10
        os.utime(yaml_file, (future, future))
        tools.convert_jsons_in_dir_to_yaml(self.tmp)
        self.assertEqual(yaml_file.read_text(encoding="utf-8"), "mine: 1\n")

    def test_yaml_that_cant_be_written(self):
        (self.tmp / "a.json").write_text('{"a": 1}', encoding="utf-8")
        # a folder where the YAML goes: the OSError is logged, nothing raised
        (self.tmp / "a.yaml").mkdir()
        future = time.time() + 10
        os.utime(self.tmp / "a.json", (future, future))
        tools.convert_jsons_in_dir_to_yaml(self.tmp)
        self.assertTrue((self.tmp / "a.yaml").is_dir())


if __name__ == "__main__":
    unittest.main()
