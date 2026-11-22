# MO2 Revamped: the Tale of Two Wastelands game plugin on the Bethesda framework.
# GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_ttw.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target is rv_game_* to keep it apart
# from the old plugin's sources in source/upstream
add_library(rv_game_ttw SHARED)
set_target_properties(rv_game_ttw PROPERTIES OUTPUT_NAME game_ttw)
mo2_configure_plugin(rv_game_ttw
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_ttw PRIVATE revamped_bethesda)
mo2_install_target(rv_game_ttw)
