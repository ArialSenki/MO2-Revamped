cmake_minimum_required(VERSION 3.16)

add_library(diagnose_basic SHARED)
mo2_configure_plugin(diagnose_basic WARNINGS OFF PRIVATE_DEPENDS boost)
# MO2 Revamped: the Windows 11 content dialog
target_include_directories(diagnose_basic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
mo2_install_target(diagnose_basic)
