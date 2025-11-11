# Example programs
add_executable(simple_load simple_load.c)
target_link_libraries(simple_load PRIVATE nmo)

add_executable(simple_save simple_save.c)
target_link_libraries(simple_save PRIVATE nmo)

add_executable(file_converter file_converter.c)
target_link_libraries(file_converter PRIVATE nmo)

add_executable(custom_manager custom_manager.c)
target_link_libraries(custom_manager PRIVATE nmo)

add_executable(object_inspector object_inspector.c)
target_link_libraries(object_inspector PRIVATE nmo)
