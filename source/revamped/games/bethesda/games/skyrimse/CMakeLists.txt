# MO2 Revamped: the Skyrim Special Edition game plugin on the Bethesda
# framework. GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_skyrimSE.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target name keeps it apart from the old one
# while both are built
add_library(rv_game_skyrimse SHARED)
set_target_properties(rv_game_skyrimse PROPERTIES OUTPUT_NAME game_skyrimSE)
mo2_configure_plugin(rv_game_skyrimse
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_skyrimse PRIVATE revamped_bethesda)
