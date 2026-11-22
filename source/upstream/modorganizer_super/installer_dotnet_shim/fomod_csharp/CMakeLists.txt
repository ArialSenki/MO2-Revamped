cmake_minimum_required(VERSION 3.16)

# installer_fomod_csharp.dll in plugins\, where MO2 has always loaded it; the
# real plugin installs to plugins\installer_fomod_csharp\dotnet\ (installer_fomod_csharp/src)
add_library(installer_fomod_csharp_shim SHARED)
mo2_configure_plugin(installer_fomod_csharp_shim WARNINGS OFF TRANSLATIONS OFF)
# MO2 Revamped: the real plugin is C++/CLI. A .NET error that leaves it (a
# library missing from the dotnet folder, the WinForms set-up) reaches this
# native code as a structured exception, which catch (...) only catches
# with /EHa: under the default /EHsc it ended MO2 at the first install
target_compile_options(installer_fomod_csharp_shim PRIVATE /EHa)
target_sources(installer_fomod_csharp_shim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../common/dotnetplugin.h)
set_target_properties(installer_fomod_csharp_shim PROPERTIES
	OUTPUT_NAME installer_fomod_csharp PDB_NAME installer_fomod_csharp_shim CXX_STANDARD 20)
install(TARGETS installer_fomod_csharp_shim RUNTIME DESTINATION bin/plugins)
install(FILES $<TARGET_PDB_FILE:installer_fomod_csharp_shim> DESTINATION pdb)
