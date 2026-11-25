cmake_minimum_required(VERSION 3.20)
project(MineCPP VERSION 1.0.0 LANGUAGES CXX)

option(MINECPP_BUILD_TESTS "Build all MineCPP tests" ON)
option(MINECPP_BUILD_VOXEL_PROTOTYPE "Build the MineCPP native C++ voxel client" ON)

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

add_library(minecpp_metadata STATIC
    src/json.cpp
    src/version_metadata.cpp
    src/resource_index.cpp
)
target_include_directories(minecpp_metadata PUBLIC src)

add_library(minecpp_resources STATIC
    src/sha1.cpp
    src/resource_preparer.cpp
)
target_include_directories(minecpp_resources PUBLIC src)
target_link_libraries(minecpp_resources PUBLIC minecpp_metadata)

add_library(minecpp_formats STATIC
    src/nbt.cpp
    src/region_file.cpp
    src/anvil_chunk.cpp
)
target_include_directories(minecpp_formats PUBLIC src)
target_link_libraries(minecpp_formats PUBLIC ZLIB::ZLIB)


add_executable(minecpp-launcher
    src/launcher_main.cpp
)
target_link_libraries(minecpp-launcher PRIVATE minecpp_import minecpp_resources)
target_compile_definitions(minecpp-launcher PRIVATE MINECPP_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(minecpp_import PRIVATE /W4 /permissive-)
    target_compile_options(minecpp-launcher PRIVATE /W4 /permissive-)
    target_compile_options(minecpp_metadata PRIVATE /W4 /permissive-)
    target_compile_options(minecpp_resources PRIVATE /W4 /permissive-)
    target_compile_options(minecpp_formats PRIVATE /W4 /permissive-)
else()
    target_compile_options(minecpp_import PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp-launcher PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp_metadata PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp_resources PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(minecpp_formats PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(MINECPP_BUILD_VOXEL_PROTOTYPE)
    find_package(Threads REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(glfw3 3.3 CONFIG REQUIRED)

    add_library(minecpp_world STATIC
        src/world.cpp
        src/mesh.cpp
        src/physics.cpp
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
        target_compile_definitions(minecpp PRIVATE MINECPP_VERSION="${PROJECT_VERSION}")
        target_compile_definitions(minecpp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(minecpp_world PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_options(minecpp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
        target_compile_definitions(minecpp PRIVATE MINECPP_VERSION="${PROJECT_VERSION}")
    endif()
endif()

if(MINECPP_BUILD_TESTS)
    enable_testing()

    add_executable(minecpp_import_tests tests/importer_tests.cpp)
    target_link_libraries(minecpp_import_tests PRIVATE minecpp_import)
    add_test(NAME minecpp_import_tests COMMAND minecpp_import_tests)

    add_executable(minecpp_metadata_tests tests/metadata_tests.cpp)
    target_link_libraries(minecpp_metadata_tests PRIVATE minecpp_metadata)
    add_test(NAME minecpp_metadata_tests COMMAND minecpp_metadata_tests)

    add_executable(minecpp_resource_tests tests/resource_tests.cpp)
    target_link_libraries(minecpp_resource_tests PRIVATE minecpp_resources)
    add_test(NAME minecpp_resource_tests COMMAND minecpp_resource_tests)
    add_test(NAME minecpp_launcher_verify_prepared_missing
        COMMAND minecpp-launcher verify-prepared missing --data-dir "${CMAKE_CURRENT_BINARY_DIR}/missing-prepared")
    set_tests_properties(minecpp_launcher_verify_prepared_missing PROPERTIES WILL_FAIL TRUE)
    add_test(NAME minecpp_launcher_inspect_missing
        COMMAND minecpp-launcher inspect missing --data-dir "${CMAKE_CURRENT_BINARY_DIR}/missing-inspect")
    set_tests_properties(minecpp_launcher_inspect_missing PROPERTIES WILL_FAIL TRUE)
    add_executable(minecpp_nbt_tests tests/nbt_tests.cpp)
    target_link_libraries(minecpp_nbt_tests PRIVATE minecpp_formats)
    add_test(NAME minecpp_nbt_tests COMMAND minecpp_nbt_tests)

    add_executable(minecpp_anvil_chunk_tests tests/anvil_chunk_tests.cpp)
    target_link_libraries(minecpp_anvil_chunk_tests PRIVATE minecpp_formats)
    add_test(NAME minecpp_anvil_chunk_tests COMMAND minecpp_anvil_chunk_tests)

    add_test(NAME minecpp_launcher_help COMMAND minecpp-launcher --help)
    add_test(NAME minecpp_launcher_version COMMAND minecpp-launcher --version)
    add_test(NAME minecpp_launcher_list_empty
        COMMAND minecpp-launcher list --data-dir "${CMAKE_CURRENT_BINARY_DIR}/empty-import-list")
    add_test(NAME minecpp_launcher_verify_missing
        COMMAND minecpp-launcher verify missing --data-dir "${CMAKE_CURRENT_BINARY_DIR}/missing-import")
    set_tests_properties(minecpp_launcher_verify_missing PROPERTIES WILL_FAIL TRUE)

    if(MINECPP_BUILD_VOXEL_PROTOTYPE)
        add_executable(minecpp_tests tests/world_tests.cpp)
        target_link_libraries(minecpp_tests PRIVATE minecpp_world)
        add_test(NAME minecpp_world_tests COMMAND minecpp_tests)

        add_executable(minecpp_engine_tests tests/engine_tests.cpp)
        target_link_libraries(minecpp_engine_tests PRIVATE minecpp_world)
        add_test(NAME minecpp_engine_tests COMMAND minecpp_engine_tests)
        add_test(NAME minecpp_help COMMAND minecpp --help)
        add_test(NAME minecpp_version COMMAND minecpp --version)
    endif()
endif()

install(TARGETS minecpp-launcher RUNTIME DESTINATION bin)
if(MINECPP_BUILD_VOXEL_PROTOTYPE)
    install(TARGETS minecpp RUNTIME DESTINATION bin)
endif()
