# SPDX-License-Identifier: GPL-3.0-only
# OpenEM - open remake of Eishockey Manager (Software 2000, 1993).
# Build (max. 3 parallel compilers on this machine):
#   cmake -S . -B build -G "Visual Studio 18 2026"
#   cmake --build build --config Release -- -m:3 -p:CL_MPCount=1 -v:minimal
#   ctest --test-dir build -C Release -j3
cmake_minimum_required(VERSION 3.24)
project(OpenEM VERSION 0.0.1 LANGUAGES CXX C)
add_compile_definitions(OPENEM_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
endif()

# Directory with the original game (EM.EXE, KB\, TORE\, *.HOK). Only used by the data tests;
# the original files are never copied into the build or the source tree.
set(EM_DATA "${CMAKE_CURRENT_SOURCE_DIR}/Original/Eishockey Manager (1993)/EM" CACHE PATH "Original Eishockey Manager directory")

# SDL2: pinned release fetched at configure time (same pin as OpenBMH)
option(EM_USE_SYSTEM_SDL2 "Use an installed SDL2 instead of fetching the pinned release" OFF)
if(EM_USE_SYSTEM_SDL2)
    find_package(SDL2 REQUIRED CONFIG)
    set(EM_SDL_TARGET SDL2::SDL2)
else()
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.32.10
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(SDL2)
    set(EM_SDL_TARGET SDL2::SDL2-static)
endif()
# SDL2 renames main when SDL.h is included; link SDL2main first where it exists.
if(TARGET SDL2::SDL2main)
    list(PREPEND EM_SDL_TARGET SDL2::SDL2main)
endif()

# Original data formats (no SDL dependency)
add_library(em_formats STATIC
    src/formats/binary.cpp
    src/formats/db.cpp
    src/formats/hok.cpp
    src/formats/tore.cpp
    src/formats/vga.cpp)
target_include_directories(em_formats PUBLIC src)

# em_game: game logic (schedule, ...), built on the original data read by em_formats
add_library(em_game STATIC
    src/game/calendar.cpp
    src/game/competitions.cpp
    src/game/contracts.cpp
    src/game/finances.cpp
    src/game/game.cpp
    src/game/match.cpp
    src/game/schedule.cpp
    src/game/scorers.cpp
    src/game/transfers.cpp
    src/game/season.cpp
    src/game/season_end.cpp
    src/game/sponsors.cpp
    src/game/training.cpp
    src/game/injuries.cpp)
target_link_libraries(em_game PUBLIC em_formats)

# em_dump: command-line inspection of original files (see src/tools/em_dump.cpp)
add_executable(em_dump src/tools/em_dump.cpp)
target_link_libraries(em_dump PRIVATE em_formats em_game)

# em_view: SDL window showing one KB image with its palette
add_executable(em_view src/tools/em_view.cpp)
target_link_libraries(em_view PRIVATE em_formats ${EM_SDL_TARGET})

enable_testing()
add_executable(em_format_tests tests/format_tests.cpp)
target_link_libraries(em_format_tests PRIVATE em_formats em_game)
add_test(NAME formats_unit COMMAND em_format_tests)
# Reference saves made with the original in DOSBox-X (see work/README.md)
set(EM_REFERENCE "${CMAKE_CURRENT_SOURCE_DIR}/work/EM" CACHE PATH "Directory with reference savegames (START/TAG1/MONAT1.HOK)")
if(IS_DIRECTORY "${EM_DATA}")
    add_test(NAME formats_original_kb COMMAND em_format_tests "${EM_DATA}" "${EM_REFERENCE}")
endif()
