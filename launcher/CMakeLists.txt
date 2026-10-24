add_executable(petka_launcher
    src/main.c src/platform.c src/film_pipe.c src/film_av.c src/crt.c
    src/common.c src/text.c src/resources.c src/audio.c src/visual.c src/scene.c src/heroes.c src/conversation.c src/host.c src/film_play.c src/inventory.c src/pad.c src/star.c src/screens.c src/saves.c src/menu.c src/part.c src/play.c src/extras.c src/highlight.c src/restored.c src/scriptfix.c src/phrasefix.c)

target_link_libraries(petka_launcher PRIVATE petka_core)

# Prefer pkg-config over find_package(raylib)'s CMake package: some distro
# raylib CMake configs (e.g. the Homebrew bottle at the time of writing)
# bake in absolute SDK framework paths from the machine that built them,
# which breaks on a different machine. pkg-config just gives plain flags.
find_package(PkgConfig REQUIRED)
pkg_check_modules(RAYLIB REQUIRED raylib)

target_include_directories(petka_launcher PRIVATE ${RAYLIB_INCLUDE_DIRS})

# PETKA_STATIC_RAYLIB=ON puts raylib inside the program, so that the one file
# can be copied into a game's folder and run on a machine with no raylib
# installed -- the way a release is built.
option(PETKA_STATIC_RAYLIB "Link raylib statically" OFF)
if(PETKA_STATIC_RAYLIB)
    find_library(RAYLIB_ARCHIVE NAMES libraylib.a HINTS ${RAYLIB_LIBRARY_DIRS} NO_DEFAULT_PATH)
    if(NOT RAYLIB_ARCHIVE)
        message(FATAL_ERROR "PETKA_STATIC_RAYLIB: no libraylib.a in ${RAYLIB_LIBRARY_DIRS}")
    endif()
    target_link_libraries(petka_launcher PRIVATE ${RAYLIB_ARCHIVE})
else()
    target_link_directories(petka_launcher PRIVATE ${RAYLIB_LIBRARY_DIRS})
    target_link_libraries(petka_launcher PRIVATE ${RAYLIB_LIBRARIES})
endif()

if(APPLE)
    target_link_libraries(petka_launcher PRIVATE
        "-framework IOKit" "-framework Cocoa" "-framework OpenGL"
    )
    if(PETKA_STATIC_RAYLIB)
        # What the archive itself needs: the window's display link and the
        # audio device.
        target_link_libraries(petka_launcher PRIVATE
            "-framework CoreVideo" "-framework CoreAudio" "-framework AudioToolbox" "-framework CoreFoundation"
        )
    endif()
endif()
