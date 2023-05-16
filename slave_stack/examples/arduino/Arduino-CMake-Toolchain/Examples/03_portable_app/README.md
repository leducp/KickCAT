Example that demonstrates the professional use of the toolchain, mainly demonstrating portability as explaind below

1. Application code (*portable_app.cpp*) and *CMakeLists.txt* is portable across platforms (Arduino, Linux, Windows etc.) i.e. the application code and CMake script have no assumptions on the platform and should work with or without the toolchain.
1. Platform specifics are abstracted as libraries i.e. only those libraries have any dependency with the platform like Arduino.

