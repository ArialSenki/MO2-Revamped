cmake_minimum_required(VERSION 3.16)

# installer_omod\installer_omod.dll, where MO2 has always loaded it; the real
# plugin and its .NET libraries install to installer_omod\dotnet\
add_library(installer_omod_shim SHARED)
mo2_configure_plugin(installer_omod_shim WARNINGS OFF TRANSLATIONS OFF)
target_sources(installer_omod_shim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/dotnetplugin.h)
set_target_properties(installer_omod_shim PROPERTIES
	OUTPUT_NAME installer_omod PDB_NAME installer_omod_shim CXX_STANDARD 20)
install(TARGETS installer_omod_shim RUNTIME DESTINATION bin/plugins/installer_omod)
install(FILES $<TARGET_PDB_FILE:installer_omod_shim> DESTINATION pdb)
