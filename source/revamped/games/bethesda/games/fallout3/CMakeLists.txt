# MO2 Revamped: the Fallout 3 game plugin on the Bethesda framework.
# GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_fallout3.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target is rv_game_* to keep it apart
# from the old plugin's sources in source/upstream
add_library(rv_game_fallout3 SHARED)
set_target_properties(rv_game_fallout3 PROPERTIES OUTPUT_NAME game_fallout3)
mo2_configure_plugin(rv_game_fallout3
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_fallout3 PRIVATE revamped_bethesda)
mo2_install_target(rv_game_fallout3)
