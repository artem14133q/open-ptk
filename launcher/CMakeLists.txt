add_executable(petka_launcher src/main.c)

target_link_libraries(petka_launcher PRIVATE petka_core)

# Prefer pkg-config over find_package(raylib)'s CMake package: some distro
# raylib CMake configs (e.g. the Homebrew bottle at the time of writing)
# bake in absolute SDK framework paths from the machine that built them,
# which breaks on a different machine. pkg-config just gives plain flags.
find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED raylib)

target_include_directories(petka_launcher PRIVATE ${RAYLIB_INCLUDE_DIRS})
target_link_directories(petka_launcher PRIVATE ${RAYLIB_LIBRARY_DIRS})
target_link_libraries(petka_launcher PRIVATE ${RAYLIB_LIBRARIES})

if(APPLE)
    target_link_libraries(petka_launcher PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL"
    )
endif()
