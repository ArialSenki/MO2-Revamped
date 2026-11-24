# MO2 Revamped: Tools > Reforged UI images and its checks (ELDEN RING
# Reforged's tutorial and item pictures, made by its tool Gideon). The code is
# eldenring_mo2_tools/reforged_ui_tool.py, imported only in Elden Ring and
# Nightreign instances (see eldenring_mo2_tools/stub.py).
import os
import sys

_PLUGINS = os.path.dirname(os.path.abspath(__file__))
if _PLUGINS not in sys.path:
    sys.path.append(_PLUGINS)

from eldenring_mo2_tools import stub as _stub  # noqa: E402

# its texts are the Elden Ring game plugin's (revamped_<language>.ts)
_CONTEXT = "EldenRingGame"


def createPlugins() -> list:
    if _stub.instance_needs_tools():
        return _stub.tool_module("reforged_ui_tool").createPlugins()
    return [
        _stub.ToolStub(
            "Elden Ring MO2 Reforged UI Images",
            _CONTEXT,
            "Reforged UI images...",
            "Makes ELDEN RING Reforged's UI images (tutorials, item pictures) with its tool, Gideon.",
            "Make the tutorial and item pictures of ELDEN RING Reforged from your game files.",
            _stub.mobase.VersionInfo(1, 0, 0, _stub.mobase.ReleaseType.FINAL),
        ),
        _stub.DiagnoseStub(
            "Elden Ring MO2 Reforged UI Diagnose",
            _CONTEXT,
            "Reforged UI image checks",
            "Warns when ELDEN RING Reforged's UI images are missing or hidden by another mod.",
            _stub.mobase.VersionInfo(1, 0, 0, _stub.mobase.ReleaseType.FINAL),
        ),
    ]
