# Host-only simulation support, kept out of core kickcat (it pulls the ESI parser
# + JSON). Linked by the network_simulator and the tests.
find_package(nlohmann_json REQUIRED)

add_library(kickcat_simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SimulatedSlave.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Topology.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeviceApp.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SimulatorControlServer.cc
)

target_link_libraries(kickcat_simulation PUBLIC kickcat nlohmann_json::nlohmann_json)
kickcat_publish_includes(kickcat_simulation ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_kickcat_properties(kickcat_simulation)
