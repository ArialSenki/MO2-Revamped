# Builds everything MO2 Revamped used to take precompiled from Mod Organizer
# 2.5.2: uibase, the plugins, plugin_python, archive, lootcli and helper.
# Build-UpstreamPlugins.ps1 configures it; the install tree (MO2_INSTALL_PATH)
# has the runtime layout, and the organizer links its libraries from there.
cmake_minimum_required(VERSION 3.22)

if (NOT MO2_INSTALL_PATH)
	message(FATAL_ERROR "MO2_INSTALL_PATH must name the install tree, outside the repository")
endif()
set(CMAKE_INSTALL_PREFIX "${MO2_INSTALL_PATH}" CACHE PATH "" FORCE)
set(DEPENDENCIES_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "" FORCE)
set(BOOST_DI_ROOT "${CMAKE_CURRENT_LIST_DIR}/deps/boost-di" CACHE PATH "" FORCE)
# lupdate writes its .ts into the build folder, not into these sources
set(MO2_LUPDATE_IN_BINARY_DIR ON)

project(mo2_revamped_upstream CXX)

# no local paths in the binaries: __FILE__ (logs, Qt message contexts) and the
# PDB path otherwise name the build machine's folders. The first match wins,
# so the nested folders come before the broader ones.
if (MSVC)
	set(MO2_PATH_MAPS)
	file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" _build)
	list(APPEND MO2_PATH_MAPS "${_build}=MO2_BUILD")
	get_filename_component(_repo "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
	list(APPEND MO2_PATH_MAPS "${_repo}=MO2_SOURCE")
	foreach (_root BOOST_ROOT QT_ROOT PYTHON_ROOT LOOT_PATH SEVENZ_ROOT)
		if (${_root})
			get_filename_component(_dep "${${_root}}" DIRECTORY)
			file(TO_CMAKE_PATH "${_dep}" _dep)
			list(APPEND MO2_PATH_MAPS "${_dep}=MO2_DEPS")
		endif()
	endforeach()
	file(TO_CMAKE_PATH "$ENV{USERPROFILE}" _user)
	if (_user)
		list(APPEND MO2_PATH_MAPS "${_user}=MO2_USER")
	endif()
	list(REMOVE_DUPLICATES MO2_PATH_MAPS)
	foreach (_map ${MO2_PATH_MAPS})
		add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/pathmap:${_map}>")
	endforeach()
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/experimental:deterministic>")
	add_link_options("/PDBALTPATH:%_PDB%")
endif()

# the sip.h of the sip module PyQt6 6.7.1 ships (ABI 13.8)
include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}/deps/sip")

set(S ${CMAKE_CURRENT_LIST_DIR}/modorganizer_super)

# libraries first: the targets below (and the organizer) find them by name
foreach (lib uibase bsatk esptk archive githubpp game_gamebryo)
	add_subdirectory(${S}/${lib} ${lib})
endforeach()

set(MO2_UPSTREAM_PROJECTS
	game_enderal game_enderalse game_fallout3 game_fallout4 game_fallout4vr
	game_falloutnv game_morrowind game_nehrim game_oblivion game_skyrim
	game_skyrimse game_skyrimvr game_starfield game_ttw
	installer_bain installer_bundle installer_fomod installer_fomod_csharp
	installer_manual installer_omod installer_quick
	bsa_extractor bsapacker preview_base preview_bsa diagnose_basic check_fnis
	tool_inieditor tool_inibakery
	plugin_python lootcli helper)
foreach (p ${MO2_UPSTREAM_PROJECTS})
	add_subdirectory(${S}/${p} ${p})
endforeach()
