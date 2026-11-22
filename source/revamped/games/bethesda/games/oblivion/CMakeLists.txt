# MO2 Revamped: the Oblivion game plugin on the Bethesda framework.
# GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_oblivion.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target is rv_game_* to keep it apart
# from the old plugin's sources in source/upstream
add_library(rv_game_oblivion SHARED)
set_target_properties(rv_game_oblivion PROPERTIES OUTPUT_NAME game_oblivion)
mo2_configure_plugin(rv_game_oblivion
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_oblivion PRIVATE revamped_bethesda)
mo2_install_target(rv_game_oblivion)
