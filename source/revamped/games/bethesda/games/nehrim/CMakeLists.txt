# MO2 Revamped: the Nehrim game plugin on the Bethesda framework.
# GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_nehrim.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target name keeps it apart from the old one
# while both are built
add_library(rv_game_nehrim SHARED)
set_target_properties(rv_game_nehrim PROPERTIES OUTPUT_NAME game_nehrim)
mo2_configure_plugin(rv_game_nehrim
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_nehrim PRIVATE revamped_bethesda)
