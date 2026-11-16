from pathlib import Path

import mobase
from PyQt6.QtCore import QDir


class BasicLocalSavegames(mobase.LocalSavegames):
    # MO2 Revamped: from MO2 cc5c3f2 (read savesDirectory() lazily, after init())
    _game: mobase.IPluginGame

    def __init__(self, game: mobase.IPluginGame):
        super().__init__()
        self._game = game

    def game_save_dir(self) -> str:
        return self._game.savesDirectory().absolutePath()

    def mappings(self, profile_save_dir: QDir):
        # MO2 Revamped: usvfs only maps the saves into a folder that exists
        # ("...\Savegames doesn't exist" on a first start of Baldur's Gate 3,
        # whose saves then went to the real folder, not the profile)
        # (nothing is made inside the game's own folder)
        parent = Path(self.game_save_dir()).parent
        game_dir = Path(self._game.gameDirectory().absolutePath())
        if not parent.is_relative_to(game_dir):
            try:
                parent.mkdir(parents=True, exist_ok=True)
            except OSError:
                pass
        return [
            mobase.Mapping(
                source=profile_save_dir.absolutePath(),
                destination=self.game_save_dir(),
                is_directory=True,
                create_target=True,
            )
        ]

    def prepareProfile(self, profile: mobase.IProfile) -> bool:
        return profile.localSavesEnabled()
