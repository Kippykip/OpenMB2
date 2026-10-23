# SPDX-License-Identifier: GPL-2.0-or-later
#
# libopenmpt plays the game's music (Music/mb01.mo3, a MO3 module). It
# has no CMake build of its own, so this builds its sources as a static
# library. Its Vorbis-compressed MO3 samples are decoded by our copy of
# stb_vorbis (src/third_party/stb_vorbis_impl.c).
#
# Sets LIBOPENMPT_TARGET.

set(OPENMB2_LIBOPENMPT_URL
    "https://lib.openmpt.org/files/libopenmpt/src/libopenmpt-0.7.13+release.autotools.tar.gz"
    CACHE STRING "Where to download libopenmpt's source from")

include(FetchContent)
FetchContent_Declare(libopenmpt URL ${OPENMB2_LIBOPENMPT_URL} DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(libopenmpt)
if(NOT libopenmpt_POPULATED)
    FetchContent_Populate(libopenmpt)
endif()

enable_language(CXX)
set(_mpt ${libopenmpt_SOURCE_DIR})
file(GLOB_RECURSE _mpt_sources CONFIGURE_DEPENDS
    ${_mpt}/common/*.cpp
    ${_mpt}/soundlib/*.cpp
    ${_mpt}/sounddsp/*.cpp)
list(APPEND _mpt_sources
    ${_mpt}/libopenmpt/libopenmpt_c.cpp
    ${_mpt}/libopenmpt/libopenmpt_cxx.cpp
    ${_mpt}/libopenmpt/libopenmpt_ext_impl.cpp
    ${_mpt}/libopenmpt/libopenmpt_impl.cpp)

add_library(openmb2_openmpt STATIC ${_mpt_sources})
set_target_properties(openmb2_openmpt PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(openmb2_openmpt
    PRIVATE LIBOPENMPT_BUILD MPT_WITH_STBVORBIS STB_VORBIS_HEADER_ONLY STB_VORBIS_NO_STDIO
    PUBLIC LIBOPENMPT_STATIC)
if(WIN32)
    target_compile_definitions(openmb2_openmpt PRIVATE UNICODE _UNICODE NOMINMAX)
endif()
target_include_directories(openmb2_openmpt
    PRIVATE ${_mpt} ${_mpt}/src ${_mpt}/common ${CMAKE_CURRENT_SOURCE_DIR}/src/third_party
    PUBLIC ${_mpt})
target_link_libraries(openmb2_openmpt PUBLIC openmb2_third_party)
if(EMSCRIPTEN)
    target_compile_options(openmb2_openmpt PRIVATE -sDISABLE_EXCEPTION_CATCHING=0)
endif()
set(LIBOPENMPT_TARGET openmb2_openmpt)
