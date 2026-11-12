cmake_minimum_required(VERSION 3.16)

add_library(bsa_extractor SHARED)
mo2_configure_plugin(bsa_extractor
    WARNINGS OFF
    PRIVATE_DEPENDS DirectXTex bsatk)
# MO2 Revamped: the Windows 11 content dialog
target_include_directories(bsa_extractor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../revamped_fluent)
mo2_install_target(bsa_extractor)
