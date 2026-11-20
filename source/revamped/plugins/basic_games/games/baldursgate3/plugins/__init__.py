"""Baldur's Gate 3: the tools in MO2's Tools menu and the load order check
in MO2's problem list."""

from __future__ import annotations

import mobase

from .diagnose import BG3LoadOrderCheck
from .tools import (
    BG3ToolCheckForLsLibUpdates,
    BG3ToolConvertJsonsToYaml,
    BG3ToolLoadOrder,
    BG3ToolReparsePakMetadata,
)


def createPlugins() -> list[mobase.IPlugin]:
    return [
        BG3ToolCheckForLsLibUpdates(),
        BG3ToolReparsePakMetadata(),
        BG3ToolConvertJsonsToYaml(),
        BG3ToolLoadOrder(),
        BG3LoadOrderCheck(),
    ]
