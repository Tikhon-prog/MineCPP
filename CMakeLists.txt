cmake_minimum_required(VERSION 3.20)
project(MineCPP VERSION 0.1.0 LANGUAGES CXX)

option(MINECPP_BUILD_TESTS "Build MineCPP tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Produce a self-contained Windows executable with the static CRT.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(ZLIB REQUIRED)

add_executable(minecpp
    src/main.cpp
    src/json.cpp
    src/zip.cpp
    src/minecraft.cpp
    src/process.cpp
)

target_include_directories(minecpp PRIVATE src)
target_link_libraries(minecpp PRIVATE ZLIB::ZLIB)

if(MSVC)
    target_compile_options(minecpp PRIVATE /W4 /permissive- /EHsc)
    target_compile_definitions(minecpp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    target_compile_options(minecpp PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # GCC 14 may emit a false-positive maybe-uninitialized warning for std::variant teardown.
        target_compile_options(minecpp PRIVATE -Wno-maybe-uninitialized)
    endif()
endif()

if(MINECPP_BUILD_TESTS)
    enable_testing()
    add_test(NAME minecpp_help COMMAND minecpp --help)
    add_test(NAME minecpp_version COMMAND minecpp --version)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME minecpp_zip_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/zip_smoke.py $<TARGET_FILE:minecpp> ${CMAKE_CURRENT_BINARY_DIR}/zip-smoke)
        add_test(NAME minecpp_launch_smoke
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/launch_smoke.py $<TARGET_FILE:minecpp> ${CMAKE_CURRENT_BINARY_DIR}/launch-smoke)
    endif()
endif()

install(TARGETS minecpp RUNTIME DESTINATION bin)
