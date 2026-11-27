cmake_minimum_required(VERSION 3.20)
project(MineCPP VERSION 0.3.0 LANGUAGES CXX)

option(MINECPP_BUILD_TESTS "Build the native world and importer tests" ON)
option(MINECPP_BUILD_VOXEL_PROTOTYPE "Build the experimental OpenGL voxel sandbox" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(ZLIB REQUIRED)

add_library(minecpp_import STATIC
    src/jar_importer.cpp
)
target_include_directories(minecpp_import PUBLIC src)
target_link_libraries(minecpp_import PUBLIC ZLIB::ZLIB)

add_library(minecpp_region STATIC
    src/region.cpp
)
target_include_directories(minecpp_region PUBLIC src)
target_link_libraries(minecpp_region PUBLIC ZLIB::ZLIB)

# PNG decoding and the block texture atlas built from an imported client jar.
add_library(minecpp_assets STATIC
    src/png.cpp
    src/texture_atlas.cpp
)
target_include_directories(minecpp_assets PUBLIC src)
target_link_libraries(minecpp_assets PUBLIC ZLIB::ZLIB)

add_executable(minecpp-launcher
    src/launcher_main.cpp
)
target_link_libraries(minecpp-launcher PRIVATE minecpp_import)

if(MSVC)
    target_compile_options(minecpp_import PRIVATE /W4 /permissive-)
    target_compile_options(minecpp_region PRIVATE /W4 /permissive-)
    target_compile_options(minecpp_assets PRIVATE /W4 /permissive-)
    target_compile_options(minecpp-launcher PRIVATE /W4 /permissive-)
else()
    target_compile_options(minecpp_import PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp_region PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp_assets PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp-launcher PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(MINECPP_BUILD_VOXEL_PROTOTYPE)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 CONFIG REQUIRED)

    add_library(minecpp_world STATIC
        src/world.cpp
        src/mesh.cpp
    )
    target_include_directories(minecpp_world PUBLIC src)
    target_link_libraries(minecpp_world PUBLIC Threads::Threads)

    add_executable(minecpp
        src/main.cpp
        src/renderer.cpp
    )
    target_include_directories(minecpp PRIVATE src)
    target_link_libraries(minecpp PRIVATE minecpp_world minecpp_assets minecpp_import glfw OpenGL::GL)

    if(MSVC)
        target_compile_options(minecpp_world PRIVATE /W4 /permissive-)
        target_compile_options(minecpp PRIVATE /W4 /permissive-)
        target_compile_definitions(minecpp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(minecpp_world PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_options(minecpp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
endif()

if(MINECPP_BUILD_TESTS)
    enable_testing()

    add_executable(minecpp_import_tests tests/importer_tests.cpp)
    target_link_libraries(minecpp_import_tests PRIVATE minecpp_import)
    add_test(NAME minecpp_import_tests COMMAND minecpp_import_tests)
    add_test(NAME minecpp_launcher_help COMMAND minecpp-launcher --help)

    add_executable(minecpp_packed_storage_tests tests/packed_storage_tests.cpp)
    target_include_directories(minecpp_packed_storage_tests PRIVATE src)
    add_test(NAME minecpp_packed_storage_tests COMMAND minecpp_packed_storage_tests)

    add_executable(minecpp_paletted_container_tests tests/paletted_container_tests.cpp)
    target_include_directories(minecpp_paletted_container_tests PRIVATE src)
    add_test(NAME minecpp_paletted_container_tests COMMAND minecpp_paletted_container_tests)

    add_executable(minecpp_chunk_section_tests tests/chunk_section_tests.cpp)
    target_include_directories(minecpp_chunk_section_tests PRIVATE src)
    add_test(NAME minecpp_chunk_section_tests COMMAND minecpp_chunk_section_tests)

    add_executable(minecpp_collision_tests tests/collision_tests.cpp)
    target_include_directories(minecpp_collision_tests PRIVATE src)
    add_test(NAME minecpp_collision_tests COMMAND minecpp_collision_tests)

    add_executable(minecpp_player_physics_tests tests/player_physics_tests.cpp)
    target_include_directories(minecpp_player_physics_tests PRIVATE src)
    add_test(NAME minecpp_player_physics_tests COMMAND minecpp_player_physics_tests)

    add_executable(minecpp_items_tests tests/items_tests.cpp)
    target_include_directories(minecpp_items_tests PRIVATE src)
    add_test(NAME minecpp_items_tests COMMAND minecpp_items_tests)

    add_executable(minecpp_nbt_tests tests/nbt_tests.cpp)
    target_include_directories(minecpp_nbt_tests PRIVATE src)
    add_test(NAME minecpp_nbt_tests COMMAND minecpp_nbt_tests)

    add_executable(minecpp_region_tests tests/region_tests.cpp)
    target_link_libraries(minecpp_region_tests PRIVATE minecpp_region)
    add_test(NAME minecpp_region_tests COMMAND minecpp_region_tests)

    add_executable(minecpp_chunk_io_tests tests/chunk_io_tests.cpp)
    target_link_libraries(minecpp_chunk_io_tests PRIVATE minecpp_region)
    add_test(NAME minecpp_chunk_io_tests COMMAND minecpp_chunk_io_tests)

    add_executable(minecpp_random_tests tests/random_tests.cpp)
    target_include_directories(minecpp_random_tests PRIVATE src)
    add_test(NAME minecpp_random_tests COMMAND minecpp_random_tests)

    add_executable(minecpp_protocol_tests tests/protocol_tests.cpp)
    target_link_libraries(minecpp_protocol_tests PRIVATE minecpp_region)
    add_test(NAME minecpp_protocol_tests COMMAND minecpp_protocol_tests)

    add_executable(minecpp_png_tests tests/png_tests.cpp)
    target_include_directories(minecpp_png_tests PRIVATE tests)
    target_link_libraries(minecpp_png_tests PRIVATE minecpp_assets)
    add_test(NAME minecpp_png_tests COMMAND minecpp_png_tests)

    add_executable(minecpp_texture_atlas_tests tests/texture_atlas_tests.cpp)
    target_include_directories(minecpp_texture_atlas_tests PRIVATE tests)
    target_link_libraries(minecpp_texture_atlas_tests PRIVATE minecpp_assets)
    add_test(NAME minecpp_texture_atlas_tests COMMAND minecpp_texture_atlas_tests)

    add_executable(minecpp_settings_tests tests/settings_tests.cpp)
    target_include_directories(minecpp_settings_tests PRIVATE src)
    add_test(NAME minecpp_settings_tests COMMAND minecpp_settings_tests)

    add_executable(minecpp_jar_locator_tests tests/jar_locator_tests.cpp)
    target_include_directories(minecpp_jar_locator_tests PRIVATE src)
    add_test(NAME minecpp_jar_locator_tests COMMAND minecpp_jar_locator_tests)

    if(MINECPP_BUILD_VOXEL_PROTOTYPE)
        add_executable(minecpp_tests tests/world_tests.cpp)
        target_link_libraries(minecpp_tests PRIVATE minecpp_world)
        add_test(NAME minecpp_world_tests COMMAND minecpp_tests)
        add_test(NAME minecpp_help COMMAND minecpp --help)
    endif()
endif()

install(TARGETS minecpp-launcher RUNTIME DESTINATION bin)
if(MINECPP_BUILD_VOXEL_PROTOTYPE)
    install(TARGETS minecpp RUNTIME DESTINATION bin)
endif()
