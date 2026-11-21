# MO2 Revamped: the Bethesda game framework (revamped_bethesda) and the game
# plugins built on it. GPL-3.0-or-later, like Mod Organizer 2.
cmake_minimum_required(VERSION 3.22)

add_library(revamped_bethesda STATIC)
mo2_configure_library(revamped_bethesda
	WARNINGS 4
	AUTOMOC ON
	PUBLIC_DEPENDS uibase
	# Skyrim SE's zlib save bodies (the static zlib the old plugins link; LZ4
	# is decoded by saves/lz4block.cpp, no liblz4)
	PRIVATE_DEPENDS zlib)

# mo2_configure_library globs every source under this folder; the game plugins
# in games/ are their own targets
get_target_property(_bethesda_sources revamped_bethesda SOURCES)
list(FILTER _bethesda_sources EXCLUDE REGEX "/bethesda/games/")
set_property(TARGET revamped_bethesda PROPERTY SOURCES ${_bethesda_sources})

# the Saves tab card shared with the old game plugins (header-only)
target_include_directories(revamped_bethesda PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../../../upstream/modorganizer_super/revamped_fluent)

add_subdirectory(games/fallout3)
add_subdirectory(games/fallout4)
add_subdirectory(games/falloutnv)
add_subdirectory(games/skyrimse)
add_subdirectory(games/oblivion)
add_subdirectory(games/nehrim)
add_subdirectory(games/morrowind)
add_subdirectory(games/ttw)
add_subdirectory(games/starfield)
