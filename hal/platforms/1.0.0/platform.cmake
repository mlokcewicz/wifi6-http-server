# platform targets - will be included in hal/CMakeLists.txt

add_subdirectory(platforms/${HW_BOARD_REVISION})

add_subdirectory(drivers)
add_subdirectory(ext_drivers)
add_subdirectory(startup)