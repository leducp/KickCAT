add_executable(run ./run.cc)
target_link_libraries(run kickcat)
set_target_properties(run PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
    POSITION_INDEPENDENT_CODE ON
)
