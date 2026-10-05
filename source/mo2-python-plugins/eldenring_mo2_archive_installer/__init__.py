"""Entry point for the Elden Ring archive layout installer."""

from .installer import EldenRingArchiveInstaller


def createPlugin() -> EldenRingArchiveInstaller:
    return EldenRingArchiveInstaller()
