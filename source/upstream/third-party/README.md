# Third-party sources Mod Organizer 2 compiles

Mod Organizer 2.5.2 did not take `7z.dll` and PyQt6 from their projects'
binaries: its build tool, `mob`, compiled them from these official sources. The
release compiles them the same way, with `Build-UpstreamPlugins.ps1`.

| File | Source | Built as |
| --- | --- | --- |
| `7z2405-src.7z` | 7-Zip 24.05 by Igor Pavlov, from the project's release on GitHub (`ip7z/7zip`); identical to the SourceForge copy. GNU LGPL 2.1 or later with the unRAR restriction, see `licenses/7zip.txt` | `CPP/7zip/Bundles/Format7zF` with `nmake CPU=x64 NEW_COMPILER=1 MY_STATIC_LINK=1 NO_BUFFEROVERFLOWU=1`, `mob`'s options, plus `PLATFORM=x64`, which 24.05's makefiles need to pick the x64 assembler |
| `PyQt6-6.7.1.tar.gz` | PyQt6 6.7.1 by Riverbank Computing, the source distribution on PyPI; GNU GPL 3 | `sip-install --confirm-license --pep484-pyi --link-full-dll`, as `mob` does, against Qt 6.7.1 |
| `PyQt6_sip-13.8.0.tar.gz` | PyQt6-sip 13.8.0, the source distribution on PyPI; BSD 2-Clause, see `licenses/PyQt6-sip-BSD-2-Clause.txt` | a wheel built with `pip`, from which `PyQt6/sip.cp312-win_amd64.pyd` is taken (`mob` generates the same source distribution with `sip-module --sdist`; this is the one Riverbank published) |

`SHA256SUMS.txt` holds their hashes; the PyPI ones match the hashes PyPI
publishes. `pyqt-build-requirements.txt` pins sip and PyQt-builder at the
versions `mob` uses for 2.5.2, and what they need, with the hashes PyPI
publishes. `pyqt-builder-link-full-dll.patch` is the linker part of `mob`'s
patch to PyQt-builder (`patches/pyqt/builder.py.manual_patch`): without it the
modules link `python3.dll`, which MO2 does not ship, instead of
`python312.dll`. The Designer and qmlscene plugins are not built, as MO2 does
not ship them.
