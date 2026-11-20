# MO2 Revamped: the Elden Ring save isolation tool and its save checks. The
# code is eldenring_mo2_tools/save_isolation.py, imported only in Elden Ring
# and Nightreign instances (see eldenring_mo2_tools/stub.py): MO2 evaluates
# this file in every instance, and the ~6,000 lines cost ~50 ms each start.
import os
import sys

_PLUGINS = os.path.dirname(os.path.abspath(__file__))
if _PLUGINS not in sys.path:
    sys.path.append(_PLUGINS)

from eldenring_mo2_tools import stub as _stub  # noqa: E402

_CONTEXT = "EldenRingSaveIsolation"


def createPlugins() -> list:
    if _stub.instance_needs_tools():
        return _stub.tool_module("save_isolation").createPlugins()
    return [
        _stub.ToolStub(
            "Elden Ring MO2 Save Isolation",
            _CONTEXT,
            "Save isolation",
            "Choose Elden Ring save routes and named game save profiles, inspect "
            "the active save path, and import or transfer .sl2 saves.",
            "Select the Elden Ring save route and named game save for this profile.",
            _stub.file_version("save_isolation.py", (0, 5, 0, 85)),
        ),
        _stub.DiagnoseStub(
            "Elden Ring MO2 Save Diagnose",
            _CONTEXT,
            "Elden Ring save checks",
            "Warns about Elden Ring save routes that need attention.",
            _stub.mobase.VersionInfo(1, 0, 0, _stub.mobase.ReleaseType.FINAL),
        ),
    ]
