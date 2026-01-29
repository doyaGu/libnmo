# Round-trip test macro
macro(add_round_trip_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE nmo test_framework)
    add_test(NAME ${name} COMMAND ${name})
endmacro()

# Round-trip tests
add_round_trip_test(test_round_trip)
