# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.16)
project(OpenMB2 VERSION 1.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(OPENMB2_FETCH_SDL "Download and build SDL2 if it isn't installed" ON)
option(OPENMB2_BUILD_TOOLS "Build the command-line tools (mb2inspect)" ON)

# Warnings for our own code only (not for a downloaded SDL).
function(openmb2_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library: file access, parsers and game data. No SDL, no GL, so the
# tools and tests can use it on their own.
# ---------------------------------------------------------------------------
# Vendored libraries, built without our warning flags.
add_library(openmb2_third_party STATIC
    src/third_party/stb_image_impl.c
    src/third_party/stb_vorbis_impl.c
)
target_include_directories(openmb2_third_party PRIVATE src/third_party)

add_library(openmb2_core STATIC
    src/core/config.c
    src/core/des3.c
    src/core/image.c
    src/core/log.c
    src/core/matrix.c
    src/core/molebox.c
    src/core/vfs.c
    src/core/xfile.c
    src/game/board.c
    src/game/font.c
    src/game/lang.c
    src/game/level.c
    src/game/menu.c
    src/game/params.c
    src/game/settings.c
    src/game/play.c
    src/game/save.c
    src/game/scores.c
    src/game/scene.c
    src/game/textures.c
)
target_include_directories(openmb2_core PUBLIC src)
target_link_libraries(openmb2_core PUBLIC openmb2_third_party)
if(NOT MSVC)
    target_link_libraries(openmb2_core PUBLIC m)
endif()
openmb2_warnings(openmb2_core)

if(OPENMB2_BUILD_TOOLS)
    add_executable(mb2inspect tools/mb2inspect.c)
    target_link_libraries(mb2inspect PRIVATE openmb2_core)
    openmb2_warnings(mb2inspect)
endif()

# ---------------------------------------------------------------------------
# SDL2: Emscripten has its own port; elsewhere use an installed copy, or
# download a static build when there isn't one (handy on Windows).
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    set(SDL2_TARGET "")
elseif(NOT TARGET SDL2::SDL2)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND AND OPENMB2_FETCH_SDL)
        message(STATUS "SDL2 not found, downloading it")
        include(FetchContent)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(SDL2
            URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.tar.gz)
        FetchContent_MakeAvailable(SDL2)
    endif()
endif()

if(NOT EMSCRIPTEN)
    if(TARGET SDL2::SDL2-static)
        set(SDL2_TARGET SDL2::SDL2-static)
    elseif(TARGET SDL2::SDL2)
        set(SDL2_TARGET SDL2::SDL2)
    else()
        message(FATAL_ERROR "SDL2 is required. Install it or enable OPENMB2_FETCH_SDL.")
    endif()
endif()

# ---------------------------------------------------------------------------
# The game
# ---------------------------------------------------------------------------
add_executable(openmb2
    src/assets.c
    src/intro.c
    src/main.c
    src/menu_screen.c
    src/online.c
    src/pad.c
    src/play_screen.c
    src/userfile.c
    src/audio/audio.c
    src/crash.c
    src/platform/gl.c
    src/render/render.c
)
target_link_libraries(openmb2 PRIVATE openmb2_core)
target_compile_definitions(openmb2 PRIVATE OPENMB2_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(openmb2 PRIVATE wininet)
    # The icon and the version details in the exe's Properties.
    enable_language(RC)
    configure_file(res/openmb2.rc.in ${CMAKE_CURRENT_BINARY_DIR}/openmb2.rc @ONLY)
    target_sources(openmb2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/openmb2.rc)
endif()
openmb2_warnings(openmb2)

include(cmake/libopenmpt.cmake)
target_link_libraries(openmb2 PRIVATE ${LIBOPENMPT_TARGET})

if(EMSCRIPTEN)
    # The page (web/index.html) loads openmb2.js, the game data packed
    # with file_packager at /game, and starts it with --game /game.
    # web/pre.js keeps /save (saved games, scores, openmb2.ini) in the
    # browser's IndexedDB.
    target_compile_options(openmb2 PRIVATE -sUSE_SDL=2)
    target_link_options(openmb2 PRIVATE
        -sUSE_SDL=2 -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=2 -sGL_ENABLE_GET_PROC_ADDRESS
        -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB
        -sDISABLE_EXCEPTION_CATCHING=0 -sFORCE_FILESYSTEM -lidbfs.js -sFETCH
        -sEXPORTED_RUNTIME_METHODS=callMain,FS
        --pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web/pre.js)
    set_property(TARGET openmb2 APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web/pre.js)
else()
    if(TARGET SDL2::SDL2main)
        target_link_libraries(openmb2 PRIVATE SDL2::SDL2main)
    endif()
    target_link_libraries(openmb2 PRIVATE ${SDL2_TARGET})
endif()

# No console window on Windows; main.c borrows the command prompt's, or
# opens one for --console.
set_target_properties(openmb2 PROPERTIES WIN32_EXECUTABLE ON)
if(MINGW)
    target_link_options(openmb2 PRIVATE -static -static-libgcc)
    if(OPENMB2_BUILD_TOOLS)
        target_link_options(mb2inspect PRIVATE -static -static-libgcc)
    endif()
endif()
