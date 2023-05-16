add_library(app_entry INTERFACE)

target_include_directories(app_entry
INTERFACE
	"${CMAKE_CURRENT_SOURCE_DIR}"
)

if (ARDUINO)

	# Linking with core library is required for the setup/loop entry points

	target_link_arduino_libraries(app_entry
	INTERFACE
		core
	)

endif()

