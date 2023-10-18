#### Bus debug management
option(DEBUG_BUS_ERROR   "Enable bus debug error level traces" OFF)
if (DEBUG_BUS_ERROR)
  add_compile_definitions(DEBUG_BUS_ERROR)
endif()

option(DEBUG_BUS_WARNING "Enable bus debug warning level traces" OFF)
if (DEBUG_BUS_WARNING)
  add_compile_definitions(DEBUG_BUS_WARNING)
endif()

option(DEBUG_BUS_INFO    "Enable bus debug info level traces" OFF)
if (DEBUG_BUS_INFO)
  add_compile_definitions(DEBUG_BUS_INFO)
endif()

#### Link debug management
option(DEBUG_LINK_ERROR   "Enable link debug error level traces" OFF)
if (DEBUG_LINK_ERROR)
  add_compile_definitions(DEBUG_LINK_ERROR)
endif()

option(DEBUG_LINK_WARNING "Enable link debug warning level traces" OFF)
if (DEBUG_LINK_WARNING)
  add_compile_definitions(DEBUG_LINK_WARNING)
endif()

option(DEBUG_LINK_INFO    "Enable link debug info level traces" OFF)
if (DEBUG_LINK_INFO)
  add_compile_definitions(DEBUG_LINK_INFO)
endif()

#### Socket debug management
option(DEBUG_SOCKET_ERROR   "Enable socket debug error level traces" OFF)
if (DEBUG_SOCKET_ERROR)
  add_compile_definitions(DEBUG_SOCKET_ERROR)
endif()

option(DEBUG_SOCKET_WARNING "Enable socket debug warning level traces" OFF)
if (DEBUG_SOCKET_WARNING)
  add_compile_definitions(DEBUG_SOCKET_WARNING)
endif()

option(DEBUG_SOCKET_INFO    "Enable socket debug info level traces" OFF)
if (DEBUG_SOCKET_INFO)
  add_compile_definitions(DEBUG_SOCKET_INFO)
endif()

#### Gateway debug management
option(DEBUG_GATEWAY_ERROR   "Enable gateway debug error level traces" OFF)
if (DEBUG_GATEWAY_ERROR)
  add_compile_definitions(DEBUG_GATEWAY_ERROR)
endif()

option(DEBUG_GATEWAY_WARNING "Enable gateway debug warning level traces" OFF)
if (DEBUG_GATEWAY_WARNING)
  add_compile_definitions(DEBUG_GATEWAY_WARNING)
endif()

option(DEBUG_GATEWAY_INFO    "Enable gateway debug info level traces" OFF)
if (DEBUG_GATEWAY_INFO)
  add_compile_definitions(DEBUG_GATEWAY_INFO)
endif()

#### CoE debug management
option(DEBUG_COE_ERROR   "Enable CoE debug error level traces" OFF)
if (DEBUG_COE_ERROR)
  add_compile_definitions(DEBUG_COE_ERROR)
endif()

option(DEBUG_COE_WARNING "Enable CoE debug warning level traces" OFF)
if (DEBUG_COE_WARNING)
  add_compile_definitions(DEBUG_COE_WARNING)
endif()

option(DEBUG_COE_INFO    "Enable CoE debug info level traces" OFF)
if (DEBUG_COE_INFO)
  add_compile_definitions(DEBUG_COE_INFO)
endif()

#### Slave debug management
option(DEBUG_SLAVE_ERROR   "Enable slave debug error level traces" OFF)
if (DEBUG_SLAVE_ERROR)
  add_compile_definitions(DEBUG_SLAVE_ERROR)
endif()

option(DEBUG_SLAVE_WARNING "Enable slave debug warning level traces" OFF)
if (DEBUG_SLAVE_WARNING)
  add_compile_definitions(DEBUG_SLAVE_WARNING)
endif()

option(DEBUG_SLAVE_INFO    "Enable slave debug info level traces" OFF)
if (DEBUG_SLAVE_INFO)
  add_compile_definitions(DEBUG_SLAVE_INFO)
endif()

#### Simulator debug management
option(DEBUG_SIMU_ERROR   "Enable simulator debug error level traces" OFF)
if (DEBUG_SIMU_ERROR)
  add_compile_definitions(DEBUG_SIMU_ERROR)
endif()

option(DEBUG_SIMU_WARNING "Enable simulator debug warning level traces" OFF)
if (DEBUG_SIMU_WARNING)
  add_compile_definitions(DEBUG_SIMU_WARNING)
endif()

option(DEBUG_SIMU_INFO    "Enable simulator debug info level traces" OFF)
if (DEBUG_SIMU_INFO)
  add_compile_definitions(DEBUG_SIMU_INFO)
endif()
