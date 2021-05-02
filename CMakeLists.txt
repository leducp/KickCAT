cmake_minimum_required(VERSION 3.16.3)
project(KickCAT)

add_library(kickcat src/Bus.cc
                    src/CoE.cc
                    src/Frame.cc
                    src/LinuxSocket.cc
                    src/Mailbox.cc
                    src/protocol.cc
                    src/Slave.cc
                    src/Time.cc
                    )
target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_compile_options(-Wall -Wextra -pedantic)# -Werror)
set_target_properties(kickcat PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.10.0
)
FetchContent_MakeAvailable(googletest)

add_executable(unit unit/bus-t.cc)
target_link_libraries(unit kickcat gtest gtest_main)
set_target_properties(unit PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)

add_subdirectory(example)
