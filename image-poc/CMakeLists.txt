find_package(OpenCV REQUIRED)
find_package(argparse CONFIG REQUIRED)

add_executable(image-publisher image-publisher.cc)
target_link_libraries(image-publisher kickcat argparse::argparse opencv::opencv)
set_kickcat_properties(image-publisher)

add_executable(image-subscriber image-subscriber.cc)
target_link_libraries(image-subscriber kickcat argparse::argparse opencv::opencv)
set_kickcat_properties(image-subscriber)
