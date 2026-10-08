include(GoogleTest)

add_executable(cigi_smoke_test smoke_test.cpp)
target_link_libraries(cigi_smoke_test PRIVATE cigi::cigi GTest::gmock_main)
cigi_apply_project_options(cigi_smoke_test)
gtest_discover_tests(cigi_smoke_test)
