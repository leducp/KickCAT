cmake_minimum_required(VERSION 3.16.3)
project(KickCAT)

add_library(kickcat src/Bus.cc
                    src/Error.cc
                    src/LinuxSocket.cc)
target_include_directories(kickcat PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_compile_options(-Wall -Wextra -pedantic)# -Werror)

include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.10.0
)
FetchContent_MakeAvailable(googletest)

add_executable(unit unit/bus-t.cc)
target_link_libraries(unit kickcat gtest gtest_main)

add_subdirectory(example)
