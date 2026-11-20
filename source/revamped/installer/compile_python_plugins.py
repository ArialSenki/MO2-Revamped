"""Compiles the Python plugins of a release stage to the bytecode MO2 loads.

plugin_python starts its interpreter with optimization_level = 2, so Python
looks for __pycache__/<name>.cpython-312.opt-2.pyc beside each module. Without
them the first start after installing or updating compiles every plugin
(~2 s), and an installation folder Python can't write to pays it on every
start. The files are checked-hash .pyc: Python validates them against the
source's hash, not its timestamp, so the installer changing the files' dates
doesn't invalidate them and a user editing a plugin gets it recompiled.

Usage: python -I compile_python_plugins.py <stage folder> <expected magic, hex>

The magic number is the one of the bundled interpreter (read by
Build-Release.ps1 from pythoncore.zip): bytecode from another Python version
would be ignored, or worse. The file name each .pyc records is its path below
the stage, so no build folder ends up in the payload.
"""

import importlib.util
import os
import py_compile
import sys


def main() -> int:
    stage, expected_magic = sys.argv[1], sys.argv[2].lower()
    if importlib.util.MAGIC_NUMBER.hex() != expected_magic:
        print(
            f"this Python's bytecode ({importlib.util.MAGIC_NUMBER.hex()}) is not the bundled "
            f"interpreter's ({expected_magic})",
            file=sys.stderr,
        )
        return 2
    plugins = os.path.join(stage, "plugins")
    compiled, failed = 0, []
    for root, dirs, files in os.walk(plugins):
        dirs[:] = sorted(d for d in dirs if d != "__pycache__")
        for name in sorted(files):
            if not name.endswith(".py"):
                continue
            source = os.path.join(root, name)
            relative = os.path.relpath(source, stage).replace(os.sep, "/")
            try:
                py_compile.compile(
                    source,
                    cfile=importlib.util.cache_from_source(source, optimization=2),
                    dfile=relative,
                    doraise=True,
                    optimize=2,
                    invalidation_mode=py_compile.PycInvalidationMode.CHECKED_HASH,
                )
                compiled += 1
            except py_compile.PyCompileError as error:
                failed.append(f"{relative}: {error.exc_type_name}")
    for line in failed:
        print("not compiled:", line, file=sys.stderr)
    print(f"compiled {compiled} files")
    # a plugin file Python can't compile would fail when MO2 loads it too
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
