add_library(app_stdio app_stdio.h app_stdio.cpp)

# To allow the dependents to include app_stdio.h
target_include_directories(app_stdio
INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

if (ARDUINO)

	# For Arduino, link core library for Serial functions

	target_link_arduino_libraries(app_stdio
	PRIVATE
		core
	)

else()

	# For other platforms standard library is linked by default

endif()

