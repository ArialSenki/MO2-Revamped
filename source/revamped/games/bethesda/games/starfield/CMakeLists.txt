# MO2 Revamped: the Starfield game plugin on the Bethesda framework.
# GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

# named game_starfield.dll like the plugin it replaces (MO2 and the installer
# know the game by that file); the target is rv_game_* to keep it apart
# from the old plugin's sources in source/upstream
add_library(rv_game_starfield SHARED)
set_target_properties(rv_game_starfield PROPERTIES OUTPUT_NAME game_starfield)
mo2_configure_plugin(rv_game_starfield
	WARNINGS 4
	TRANSLATIONS OFF
	PRIVATE_DEPENDS uibase)
target_link_libraries(rv_game_starfield PRIVATE revamped_bethesda)
mo2_install_target(rv_game_starfield)
