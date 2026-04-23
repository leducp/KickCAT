set(KICKCAT_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/checksum/adler32.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/Frame.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/Mailbox.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/TapSocket.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/SIIParser.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Time.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/OD.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/protocol.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/request.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/mailbox/response.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/CiA/DS402/StateMachine.cc

  ${CMAKE_CURRENT_SOURCE_DIR}/src/EoE/protocol.cc
)


if (NUTTX)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/NuttX/Socket.cc
  )
  set(OS_LIBRARIES )
elseif(PIKEOS)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/PikeOS/ErrorCategory.cc
  )

  set(OS_LIBRARIES )
elseif (UNIX)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/UdpDiagSocket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Timer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Mutex.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/SharedMemory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/ConditionVariable.cc
  )
  set(OS_LIBRARIES pthread rt)
elseif (WIN32)
  set(OS_LIB_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Windows/Socket.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Windows/SharedMemory.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Windows/Timer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Time.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/Mutex.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Unix/ConditionVariable.cc
  )

  find_package(npcap CONFIG REQUIRED)
  set(OS_LIBRARIES npcap::npcap pthread)
endif()

if (ENABLE_ESI_PARSER)
  find_package(tinyxml2 CONFIG REQUIRED)
  list(APPEND KICKCAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/CoE/EsiParser.cc)
  list(APPEND OS_LIBRARIES tinyxml2::tinyxml2)
endif()

if (ENABLE_AF_XDP AND LINUX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBXDP REQUIRED libxdp)
  pkg_check_modules(LIBBPF REQUIRED libbpf)

  find_program(CLANG_BPF clang REQUIRED)

  set(BPF_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/xdp_ethercat.bpf.c)
  set(BPF_OBJ ${CMAKE_CURRENT_BINARY_DIR}/xdp_ethercat.bpf.o)
  set(BPF_HDR ${CMAKE_CURRENT_BINARY_DIR}/xdp_ethercat_bpf.h)

  execute_process(COMMAND uname -m OUTPUT_VARIABLE UNAME_ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(BPF_ARCH_INCLUDES "/usr/include/${UNAME_ARCH}-linux-gnu")
  set(BPF_TARGET_ARCH "${UNAME_ARCH}")
  if(UNAME_ARCH MATCHES "^(x86_64|i[3-6]86)$")
    set(BPF_TARGET_ARCH "x86")
  elseif(UNAME_ARCH MATCHES "^(aarch64|arm)")
    set(BPF_TARGET_ARCH "arm64")
  elseif(UNAME_ARCH MATCHES "^(ppc|powerpc)")
    set(BPF_TARGET_ARCH "powerpc")
  elseif(UNAME_ARCH MATCHES "^riscv")
    set(BPF_TARGET_ARCH "riscv")
  endif()

  add_custom_command(
    OUTPUT  ${BPF_OBJ}
    COMMAND ${CLANG_BPF} -g -O2 -target bpf
            -D__TARGET_ARCH_${BPF_TARGET_ARCH}
            -isystem ${BPF_ARCH_INCLUDES}
            ${LIBBPF_CFLAGS}
            -c ${BPF_SRC}
            -o ${BPF_OBJ}
    DEPENDS ${BPF_SRC}
    COMMENT "Compiling BPF program xdp_ethercat.bpf.c (arch: ${BPF_TARGET_ARCH})"
  )

  add_custom_command(
    OUTPUT  ${BPF_HDR}
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_FILE=${BPF_OBJ}
            -DOUTPUT_FILE=${BPF_HDR}
            -DVAR_NAME=xdp_ethercat_bpf
            -P ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    DEPENDS ${BPF_OBJ} ${CMAKE_SOURCE_DIR}/cmake/embed_binary.cmake
    COMMENT "Embedding BPF object into C header"
  )

  list(APPEND OS_LIB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/Linux/XdpSocket.cc)
  list(APPEND OS_LIBRARIES ${LIBXDP_LIBRARIES} ${LIBBPF_LIBRARIES})
endif()

add_library(kickcat ${KICKCAT_SOURCES} ${OS_LIB_SOURCES})
kickcat_publish_includes(kickcat ${CMAKE_CURRENT_SOURCE_DIR}/include)

if (ENABLE_AF_XDP AND LINUX)
  target_sources(kickcat PRIVATE ${BPF_HDR})
  target_include_directories(kickcat PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${LIBXDP_INCLUDE_DIRS}
    ${LIBBPF_INCLUDE_DIRS}
  )
  target_compile_definitions(kickcat PUBLIC KICKCAT_AF_XDP_ENABLED)
endif()

# Provide default implementation for since_epoch()
add_library(kickcat_time ${CMAKE_CURRENT_SOURCE_DIR}/src/OS/SinceEpoch.cc)
target_include_directories(kickcat_time
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/kickcat)

target_link_libraries(kickcat ${OS_LIBRARIES} kickcat_time)

set_kickcat_properties(kickcat)

add_subdirectory(master)
add_subdirectory(slave)

kickcat_install_package("${OS_LIBRARIES}")
