cmake_minimum_required(VERSION 3.16)

add_executable(nxmhandler)
mo2_configure_executable(nxmhandler
	WARNINGS OFF
	PRIVATE_DEPENDS uibase)

# MO2 Revamped: the Fluent window matches the title bar to light/dark (dwmapi)
# and reads programs' version resources (version)
target_link_libraries(nxmhandler PRIVATE dwmapi version)

# MO2 Revamped: keep local source paths out of Release binaries, as for
# ModOrganizer.exe (source/modorganizer/src/CMakeLists.txt); Build-Release.ps1
# rejects a payload that contains them. MSVC 19.30+ needs deterministic
# compilation for /pathmap to affect __FILE__ strings.
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1930)
	file(TO_CMAKE_PATH "$ENV{USERPROFILE}" NXM_USER_PROFILE_PATH)
	file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}" NXM_PROJECT_SOURCE_PATH)
	if(NOT NXM_PROJECT_SOURCE_PATH STREQUAL "")
		target_compile_options(nxmhandler PRIVATE
			"$<$<CONFIG:Release>:/pathmap:${NXM_PROJECT_SOURCE_PATH}=NXM_SOURCE>")
	endif()
	if(BOOST_ROOT)
		get_filename_component(NXM_DEPENDENCY_BUILD_ROOT "${BOOST_ROOT}" DIRECTORY)
		file(TO_CMAKE_PATH "${NXM_DEPENDENCY_BUILD_ROOT}" NXM_DEPENDENCY_BUILD_PATH)
		if(NOT NXM_DEPENDENCY_BUILD_PATH STREQUAL "")
			target_compile_options(nxmhandler PRIVATE
				"$<$<CONFIG:Release>:/pathmap:${NXM_DEPENDENCY_BUILD_PATH}=MO2_DEPS>")
		endif()
	endif()
	if(NOT NXM_USER_PROFILE_PATH STREQUAL "")
		target_compile_options(nxmhandler PRIVATE
			"$<$<CONFIG:Release>:/experimental:deterministic>"
			"$<$<CONFIG:Release>:/pathmap:${NXM_USER_PROFILE_PATH}=MO2_USER>")
	endif()
	target_link_options(nxmhandler PRIVATE
		"$<$<CONFIG:Release>:/PDBALTPATH:%_PDB%>")
endif()

mo2_install_target(nxmhandler)

mo2_deploy_qt(BINARIES nxmhandler.exe NOPLUGINS)
