cmake_minimum_required(VERSION 3.27)

project(macos-zoom
    DESCRIPTION "macOS-like full-screen zoom controls for Hyprland"
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HYPRLAND_DEPS REQUIRED IMPORTED_TARGET
    hyprland
    libdrm
    libinput
    libudev
    lua
    pangocairo
    pixman-1
    wayland-server
    xkbcommon
)

add_library(macos-zoom SHARED src/main.cpp)
set_target_properties(macos-zoom PROPERTIES PREFIX "" OUTPUT_NAME "macos-zoom")
target_compile_options(macos-zoom PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(macos-zoom PRIVATE -fno-gnu-unique)
endif()
target_link_libraries(macos-zoom PRIVATE PkgConfig::HYPRLAND_DEPS)

include(CTest)
if(BUILD_TESTING)
    add_executable(zoom-math-test tests/zoom_math_test.cpp)
    add_test(NAME zoom-math COMMAND zoom-math-test)
endif()

install(TARGETS macos-zoom LIBRARY DESTINATION lib)
