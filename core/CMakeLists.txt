add_library(petka_core STATIC
    src/store.c
    src/bmp.c
    src/flic.c
    src/msk.c
    src/dialogue.c
    src/topology.c
)

target_include_directories(petka_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(petka_core PUBLIC c_std_11)
