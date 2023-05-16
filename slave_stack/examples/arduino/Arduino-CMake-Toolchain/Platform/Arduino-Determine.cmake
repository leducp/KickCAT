set(_ARDUINO_DETERMINE_INCLUDED 1)
if (NOT ARDUINO_BOARD_IDENTIFIER)
	message(FATAL_ERROR
		"\nPlease select a valid arduino board and its menu options using one of the below methods.\n"
		"1. From CMake GUI\n" 
		"2. From the generated BoardOptions.cmake at ${CMAKE_BINARY_DIR}/BoardOptions.cmake\n"
		"3. Use yor own preset BoardOptions.cmake -DARDUINO_BOARD_OPTIONS_FILE=<file>\n"
		"4. Use -DARDUINO_BOARD=<board_id> and -DARDUINO_<BOARD_ID>_MENU_<MENU_ID>_<MENU_OPT_ID>=<TRUE/FALSE>!!!\n"
	)
endif()

