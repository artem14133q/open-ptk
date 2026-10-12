cmake_minimum_required(VERSION 3.16)
project(petka_engine C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# petka_core has no platform dependencies -- builds the same on desktop and
# on Switch (devkitA64 toolchain).
add_subdirectory(core)

# The launcher needs raylib. On desktop this is just find_package(); the
# Switch build will need its own raylib build wired in separately later
# (see docs/str-format.md's tooling notes) -- not done yet, so for now the
# launcher only builds when NOT cross-compiling for Switch.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    add_subdirectory(launcher)
    add_subdirectory(tools/flictest)
    add_subdirectory(tools/dlgtest)
    add_subdirectory(tools/topotest)
    add_subdirectory(tools/scripttest)
    add_subdirectory(tools/walktest)
else()
    message(STATUS "Switch toolchain detected: skipping launcher (raylib-for-switch not wired up yet)")
endif()
