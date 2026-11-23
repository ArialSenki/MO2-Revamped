"""The plugin's mod data checker on the archives of recently updated Nexus
mods, without downloading them: Nexus publishes each file's content preview
(its list of paths).

    py -3.12 -I source/revamped/tests/cyberpunk2077/check_nexus_previews.py [mods] [cache.json]

Needs NEXUS_API_KEY in the environment (two API requests per mod; the
previews themselves need no key). Read only: nothing is downloaded or
installed. Prints the archives the checker fixes (before and after) and
those it leaves to the manual installer.
"""

from __future__ import annotations

import json
import os
import sys
import time
import urllib.error
import urllib.request
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import harness  # noqa: E402

API = "https://api.nexusmods.com/v1/games/cyberpunk2077"
AGENT = "mo2-plugin-test/1.0"
# Nexus' API policy: every request names the application and its version; a
# developer's test, not MO2 Revamped itself, so it says so
APP_HEADERS = {"Application-Name": "MO2 Revamped plugin tests", "Application-Version": "1.0"}
# the API's hourly allowance this test leaves for everything else
KEEP_HOURLY = 50


class RateLimited(Exception):
    pass


def get(url: str, key: str | None):
    headers = {"User-Agent": AGENT, "Accept": "application/json", **APP_HEADERS}
    if key:
        # the personal key only ever goes to the API itself
        if not url.startswith("https://api.nexusmods.com/"):
            raise ValueError(f"not the Nexus API: {url}")
        headers["apikey"] = key
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=30) as response:
            remaining = response.headers.get("X-RL-Hourly-Remaining")
            if key and remaining is not None and remaining.isdigit() and int(remaining) < KEEP_HOURLY:
                raise RateLimited(f"{remaining} API requests left this hour")
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        if error.code == 429:
            raise RateLimited("Nexus answered 429") from error
        raise


def fetch(count: int, cache: Path) -> dict:
    key = os.environ["NEXUS_API_KEY"]
    result = json.loads(cache.read_text(encoding="utf-8")) if cache.exists() else {}
    updated = get(f"{API}/mods/updated.json?period=1m", key)
    for entry in sorted(updated, key=lambda e: -e["latest_file_update"])[:count]:
        mod_id = str(entry["mod_id"])
        if mod_id in result:
            continue
        try:
            files = get(f"{API}/mods/{mod_id}/files.json?category=main", key).get("files", [])
            files.sort(key=lambda f: -f.get("uploaded_timestamp", 0))
            if not files or not files[0].get("content_preview_link"):
                result[mod_id] = {"file": files[0]["file_name"] if files else None, "paths": None}
                continue
            preview = get(files[0]["content_preview_link"], None)
            result[mod_id] = {
                "file": files[0]["file_name"],
                "paths": [f["file_path"].replace("\\", "/") for f in preview.get("files", []) if f.get("file_path")],
            }
        except RateLimited as error:
            # what was read so far is kept; the rest waits for another run
            print(f"stopped: {error}", file=sys.stderr)
            break
        except Exception as error:  # noqa: BLE001
            result[mod_id] = {"error": str(error)[:200]}
        time.sleep(0.3)
    cache.write_text(json.dumps(result), encoding="utf-8")
    return result


def check(previews: dict) -> None:
    harness.setup_mobase()
    sys.path.insert(0, str(harness.stage_plugins()))
    from PyQt6.QtCore import QCoreApplication

    app = QCoreApplication([])  # noqa: F841
    import mobase
    from basic_games.games.cyberpunk2077.layout import CyberpunkModDataChecker

    checker = CyberpunkModDataChecker(lambda: False)
    names = {checker.VALID: "valid", checker.FIXABLE: "fixed", checker.INVALID: "manual installer"}
    stats: Counter[str] = Counter()
    notes = []
    for mod_id, info in previews.items():
        paths = info.get("paths")
        if not paths:
            stats["no preview"] += 1
            continue
        if any(p.casefold().endswith("fomod/moduleconfig.xml") for p in paths):
            stats["fomod"] += 1
            continue
        tree = mobase.private.makeTree()  # type: ignore[attr-defined]
        for path in paths:
            try:
                tree.addFile(path)
            except RuntimeError:
                pass
        status = names[checker.dataLooksValid(tree)]
        stats[status] += 1
        if status == "fixed":
            after = harness.tree_paths(checker.fix(tree))
            notes.append(f"fixed {mod_id} {info['file']}:\n    {sorted(paths)[:4]}\n -> {after[:4]}")
        elif status == "manual installer":
            notes.append(f"manual {mod_id} {info['file']}:\n    {sorted(paths)[:6]}")
    print(dict(stats))
    for note in notes:
        print(note)


if __name__ == "__main__":
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 150
    cache = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("nexus_previews_cache.json")
    sys.stdout.reconfigure(encoding="utf-8")
    check(fetch(count, cache))
