cmake_minimum_required(VERSION 3.21)
project(MO2RevampedSetupQt LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_WIN32_EXECUTABLE ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui Widgets)

qt_add_executable(MO2RevampedSetupQt
    main.cpp
    resources.qrc
    app.rc
)

target_link_libraries(MO2RevampedSetupQt PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(MO2RevampedSetupQt PRIVATE UNICODE _UNICODE NOMINMAX)
# the release version, shown in the window's header
if(MO2R_VERSION)
    target_compile_definitions(MO2RevampedSetupQt PRIVATE MO2R_VERSION="${MO2R_VERSION}")
endif()

if(MINGW)
    target_link_options(MO2RevampedSetupQt PRIVATE -mwindows)
endif()
