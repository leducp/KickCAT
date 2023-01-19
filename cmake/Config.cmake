set(WARNINGS_FLAGS "-Wall -Wextra -pedantic -Wcast-qual -Wcast-align -Wduplicated-cond -Wshadow -Wmissing-noreturn")

macro(set_kickcat_properties binary)
    set_target_properties(${binary} PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS NO
      POSITION_INDEPENDENT_CODE ON
      COMPILE_FLAGS ${WARNINGS_FLAGS}
    )
endmacro()
