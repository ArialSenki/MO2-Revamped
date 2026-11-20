cmake_minimum_required(VERSION 3.16)

add_library(installer_fomod_csharp SHARED)
mo2_configure_plugin(installer_fomod_csharp WARNINGS OFF CLI ON)
# MO2 Revamped: in plugins\installer_fomod_csharp\dotnet\, deeper than MO2
# looks for plugins (a folder plugin is plugins\<name>\<a dll>); the native
# stand-in (installer_dotnet_shim) is plugins\installer_fomod_csharp.dll and
# loads this one when a C# FOMOD is installed
install(TARGETS installer_fomod_csharp RUNTIME DESTINATION bin/plugins/installer_fomod_csharp/dotnet)
install(TARGETS installer_fomod_csharp ARCHIVE DESTINATION libs)
install(FILES $<TARGET_PDB_FILE:installer_fomod_csharp> DESTINATION pdb)

set_target_properties(installer_fomod_csharp PROPERTIES CXX_STANDARD 20)
target_include_directories(installer_fomod_csharp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
