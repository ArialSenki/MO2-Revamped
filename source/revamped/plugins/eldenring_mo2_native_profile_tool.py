# MO2 Revamped: the Elden Ring native DLL profile tool. The code is
# eldenring_mo2_tools/native_profile_tool.py, imported only in Elden Ring and
# Nightreign instances (see eldenring_mo2_tools/stub.py).
import os
import sys

_PLUGINS = os.path.dirname(os.path.abspath(__file__))
if _PLUGINS not in sys.path:
    sys.path.append(_PLUGINS)

from eldenring_mo2_tools import stub as _stub  # noqa: E402


def createPlugin():
    if _stub.instance_needs_tools():
        return _stub.tool_module("native_profile_tool").createPlugin()
    return _stub.ToolStub(
        "Elden Ring MO2 Native Profile",
        "EldenRingNativeProfile",
        "Native DLL profile",
        "Detect installed Elden Ring native DLLs and manage their load "
        "order and optional initializer exports for each MO2 profile.",
        "Detect installed native DLLs and set their order and optional "
        "initializer for the active MO2 profile.",
        _stub.file_version("native_profile_tool.py", (0, 5, 0, 81)),
    )
