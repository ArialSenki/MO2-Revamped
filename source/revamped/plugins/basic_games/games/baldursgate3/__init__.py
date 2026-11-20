"""MO2 Revamped's Baldur's Gate 3 support: the parts of game_baldursgate3.py.

- util: logging, translation, small helpers (the only part loaded with the plugin)
- layout: the mod data checker and the Content column
- lspk, paks: what each pak holds (meta.lsx, dependencies), with a cache
- modsettings, loadorder: the load order file and how it's made
- launch: what happens when the game starts and closes
- mapper: the documents folder under the VFS
- lslib, dotnet: LSLib's Divine and the .NET runtime it needs
- saves: the save list and its details
- plugins: the BG3 tools and the load order check (IPluginDiagnose)
"""
