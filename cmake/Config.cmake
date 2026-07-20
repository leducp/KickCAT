set(WARNINGS_FLAGS "-Wall -Wextra -pedantic -Wcast-qual -Wcast-align -Wduplicated-cond -Wshadow -Wmissing-noreturn")

macro(set_kickcat_properties binary)
    set_target_properties(${binary} PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED YES
      CXX_EXTENSIONS NO
      COMPILE_FLAGS ${WARNINGS_FLAGS}
    )
    # Only the wheel build (SKBUILD) folds static libkickcat into the nanobind .so, which
    # needs PIC. Bare-metal arm (KickOS) has no loader to place the resulting .got/
    # .data.rel.ro, so it must never get PIC -- and SKBUILD is never set there.
    if (SKBUILD)
      set_target_properties(${binary} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
endmacro()
