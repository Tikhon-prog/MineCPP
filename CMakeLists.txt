cmake_minimum_required(VERSION 3.20)
project(MineCPP VERSION 0.1.0 LANGUAGES CXX)

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

add_executable(minecpp-launcher
    src/launcher_main.cpp
)
target_link_libraries(minecpp-launcher PRIVATE minecpp_import)

if(MSVC)
    target_compile_options(minecpp_import PRIVATE /W4 /permissive-)
    target_compile_options(minecpp-launcher PRIVATE /W4 /permissive-)
else()
    target_compile_options(minecpp_import PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
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
    target_link_libraries(minecpp PRIVATE minecpp_world glfw OpenGL::GL)

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

    add_executable(minecpp_items_tests tests/items_tests.cpp)
    target_include_directories(minecpp_items_tests PRIVATE src)
    add_test(NAME minecpp_items_tests COMMAND minecpp_items_tests)

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
