cmake_minimum_required(VERSION 3.16)

add_library(installer_bundle SHARED)
mo2_configure_plugin(installer_bundle WARNINGS OFF)
mo2_install_target(installer_bundle)
target_include_directories(installer_bundle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
