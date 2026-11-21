cmake_minimum_required(VERSION 3.16)

add_library(check_fnis SHARED)
mo2_configure_plugin(check_fnis WARNINGS OFF)
mo2_install_target(check_fnis)
# MO2 Revamped: the content dialogs
target_include_directories(check_fnis PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
