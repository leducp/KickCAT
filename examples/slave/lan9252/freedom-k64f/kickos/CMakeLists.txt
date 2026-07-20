# SPDX-License-Identifier: CECILL-C
# Copyright (c) 2026 Philippe Leduc
#
# Out-of-tree KickOS consumer: the KickCAT LAN9252 EtherCAT slave on the Freedom-K64F
# over DSPI0. Configured standalone against an installed KickOS package (find_package);
# KickCAT is brought in from this same tree with the KickOS backend selected (KICKOS=ON)
# and linked as the plain `kickcat` target. The DSPI0 driver and the full-C++ runtime are
# LINKED from KickOS (kickos_k64dspi, kickos_cxx) -- never vendored here.
#
# Configure against an installed frdmk64f KickOS package:
#   cmake --install <kickos-build> --prefix <prefix>
#   cmake -S . -B <build> -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=<prefix>/lib/cmake/KickOS/toolchain-arm-none-eabi.cmake \
#     -DCMAKE_PREFIX_PATH=<prefix>
# The board auto-resolves from the single-board package (no -DKICKOS_BOARD).

cmake_minimum_required(VERSION 3.24) # export/LINK_GROUP need >= 3.24
project(freedom_slave LANGUAGES C CXX ASM)

find_package(KickOS REQUIRED)

# This app lives at examples/slave/lan9252/freedom-k64f/kickos inside the KickCAT tree.
get_filename_component(KICKCAT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)

# The CTT-proven slave core is shared verbatim with the NuttX example: ONE source, no copy.
set(FREEDOM_CORE_DIR "${KICKCAT_ROOT}/examples/slave/lan9252/freedom-k64f/nuttx")
if (NOT EXISTS "${FREEDOM_CORE_DIR}/freedom_slave.cc")
  message(FATAL_ERROR "freedom-slave: shared core not found at ${FREEDOM_CORE_DIR}")
endif()

# Select the KickOS OS backend inside KickCAT before descending (time-only, no socket/pthreads).
set(KICKOS ON CACHE BOOL "" FORCE)
add_subdirectory("${KICKCAT_ROOT}" "${CMAKE_BINARY_DIR}/kickcat" EXCLUDE_FROM_ALL)

add_executable(freedom_slave
  main.cc
  ${FREEDOM_CORE_DIR}/freedom_app.cc
  ${FREEDOM_CORE_DIR}/freedom_slave.cc
  ${FREEDOM_CORE_DIR}/od_populator.cc)

# kickos_cxx  = exported full-C++ leaf (exceptions/STL/RTTI + frdmk64f linker script + boot
#               ctor split). kickos_k64dspi = exported DSPI0 driver (<kickos/driver/k64dspi.h>).
# kickcat references the driver's spi_* and the OS's kos_* symbols; if GNU ld reports an
# undefined spi_*/kos_* from the mutual reference, wrap these three in a $<LINK_GROUP:RESCAN,...>.
target_link_libraries(freedom_slave PRIVATE kickcat kickos_cxx kickos_k64dspi)

# main.cc includes the shared core's freedom_app.h out of the KickCAT example dir.
target_include_directories(freedom_slave PRIVATE ${FREEDOM_CORE_DIR})

# Opt-in CoE bring-up tracing: KickCAT's slave/CoE debug macros route fprintf -> newlib
# _write -> console. OFF by default.
option(FREEDOM_SLAVE_COE_TRACE "Enable KickCAT mailbox/CoE debug tracing" OFF)
if (FREEDOM_SLAVE_COE_TRACE)
  target_compile_definitions(kickcat PRIVATE
    DEBUG_SLAVE_INFO DEBUG_SLAVE_WARNING DEBUG_SLAVE_ERROR
    DEBUG_COE_INFO DEBUG_COE_WARNING DEBUG_COE_ERROR)
endif()
