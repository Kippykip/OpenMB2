# SPDX-License-Identifier: GPL-2.0-or-later
# Cross-compile for 64-bit ARM Linux (handhelds such as the Anbernic
# H700 devices) from an x86-64 Linux:
#   cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake \
#         -DSDL2_DIR=<an aarch64 SDL2 install>/lib/cmake/SDL2
# Link against the device's own shared SDL2 (it knows the device's screen
# and pads); libstdc++ is built in so the game doesn't depend on its version.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
