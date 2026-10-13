cmake_minimum_required(VERSION 3.25)
project(BaritoneBedrock VERSION 1.0.0 LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Minecraft Bedrock is x64 only: configure with -A x64")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(cmake/Dependencies.cmake)

# ---------------------------------------------------------------------------
# Game-independent core: pathfinding, goals, movements, cache, processes...
# Built as a static library so the tests can link it without the game.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS src/baritone/*.cpp src/baritone/*.h)
add_library(baritone_core STATIC ${CORE_SOURCES})
target_include_directories(baritone_core PUBLIC src)
target_compile_options(baritone_core PRIVATE /W4 /permissive- /utf-8 /MP)
if(NOT CORE_SOURCES MATCHES "\\.cpp")
    set_target_properties(baritone_core PROPERTIES LINKER_LANGUAGE CXX)
endif()

# ---------------------------------------------------------------------------
# The injected DLL: game SDK, hooks, commands, rendering.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE DLL_SOURCES CONFIGURE_DEPENDS
    src/dllmain.cpp
    src/core/*.cpp src/core/*.h
    src/memory/*.cpp src/memory/*.h
    src/sdk/*.cpp src/sdk/*.h
    src/hooks/*.cpp src/hooks/*.h
    src/command/*.cpp src/command/*.h
    src/platform/*.cpp src/platform/*.h
)
add_library(BaritoneBedrock SHARED ${DLL_SOURCES})
target_include_directories(BaritoneBedrock PRIVATE src)
target_compile_definitions(BaritoneBedrock PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS
    BARITONE_BEDROCK_VERSION="${PROJECT_VERSION}")
target_compile_options(BaritoneBedrock PRIVATE /W4 /permissive- /utf-8 /MP /wd4201 /wd4324)
target_link_libraries(BaritoneBedrock PRIVATE baritone_core minhook libhat::libhat EnTT::EnTT)
# Keep the PDB next to the DLL: crash logs point into it.
set_target_properties(BaritoneBedrock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_link_options(BaritoneBedrock PRIVATE /DEBUG /OPT:REF /OPT:ICF)

# ---------------------------------------------------------------------------
# Offline tests of the core (no game needed).
# ---------------------------------------------------------------------------
option(BARITONE_BUILD_TESTS "Build the offline tests of the core" ON)
if(BARITONE_BUILD_TESTS)
    file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp tests/*.h)
    if(TEST_SOURCES)
        add_executable(baritone_tests ${TEST_SOURCES})
        target_compile_options(baritone_tests PRIVATE /W4 /permissive- /utf-8)
        target_link_libraries(baritone_tests PRIVATE baritone_core)
        set_target_properties(baritone_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        enable_testing()
        add_test(NAME baritone_tests COMMAND baritone_tests)
    endif()
endif()
