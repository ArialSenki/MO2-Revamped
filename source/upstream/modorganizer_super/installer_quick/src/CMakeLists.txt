cmake_minimum_required(VERSION 3.16)

add_library(installer_quick SHARED)
mo2_configure_plugin(installer_quick WARNINGS OFF)
mo2_install_target(installer_quick)
target_include_directories(installer_quick PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
