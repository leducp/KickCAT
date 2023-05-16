# ASM does not pass -I for include directories
set(CMAKE_INCLUDE_FLAG_ASM "-I")

# Set the suffix to match the target executable name
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# If we do not set to .o, some linker scripts aren't working correctly
set(CMAKE_C_OUTPUT_EXTENSION ".o")
set(CMAKE_CXX_OUTPUT_EXTENSION ".o")
set(CMAKE_ASM_OUTPUT_EXTENSION ".o")

# Where is the target environment
# Add all tools paths and include paths?, tools/sdk in platform path?
# message("ARDUINO_FIND_ROOT_PATH:${ARDUINO_FIND_ROOT_PATH}")
set(CMAKE_FIND_ROOT_PATH ${ARDUINO_FIND_ROOT_PATH})

set(CMAKE_SYSTEM_INCLUDE_PATH "/include")
set(CMAKE_SYSTEM_LIBRARY_PATH "/lib")
# message("ARDUINO_SYSTEM_PROGRAM_PATH:${ARDUINO_SYSTEM_PROGRAM_PATH}")
set(CMAKE_SYSTEM_PROGRAM_PATH ${ARDUINO_SYSTEM_PROGRAM_PATH})

