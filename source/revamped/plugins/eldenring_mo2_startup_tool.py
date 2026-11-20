# MO2 Revamped: the Elden Ring startup options tool. The code is
# eldenring_mo2_tools/startup_tool.py, imported only in Elden Ring and
# Nightreign instances (see eldenring_mo2_tools/stub.py).
import os
import sys

_PLUGINS = os.path.dirname(os.path.abspath(__file__))
if _PLUGINS not in sys.path:
    sys.path.append(_PLUGINS)

from eldenring_mo2_tools import stub as _stub  # noqa: E402


def createPlugin():
    if _stub.instance_needs_tools():
        return _stub.tool_module("startup_tool").createPlugin()
    return _stub.ToolStub(
        "Elden Ring MO2 Startup Options",
        "EldenRingStartupOptions",
        "Startup options",
        "Per-profile Elden Ring startup, cleanup, and performance options.",
        "Startup, cleanup, and performance options for the active Elden Ring profile.",
        _stub.file_version("startup_tool.py", (0, 5, 0, 65)),
    )
