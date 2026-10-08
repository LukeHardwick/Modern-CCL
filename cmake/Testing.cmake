include(GoogleTest)

# cigi_add_test(<name> SOURCES <files...>)
# Creates a GoogleTest executable, discovers its tests and registers it for coverage.
function(cigi_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE cigi::cigi GTest::gmock_main)
    cigi_apply_project_options(${name})
    gtest_discover_tests(${name})
    set_property(GLOBAL APPEND PROPERTY CIGI_TEST_TARGETS ${name})
endfunction()
