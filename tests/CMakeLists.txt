include(GoogleTest)

add_executable(cigi_smoke_test smoke_test.cpp)
target_link_libraries(cigi_smoke_test PRIVATE cigi::cigi GTest::gmock_main)
cigi_apply_project_options(cigi_smoke_test)
gtest_discover_tests(cigi_smoke_test)

add_executable(cigi_wire_test
    wire/bits_test.cpp
    wire/byte_order_test.cpp
    wire/codec_test.cpp
    wire/error_test.cpp
    wire/padding_test.cpp
    wire/utf8_test.cpp)
target_link_libraries(cigi_wire_test PRIVATE cigi::cigi GTest::gmock_main)
cigi_apply_project_options(cigi_wire_test)
gtest_discover_tests(cigi_wire_test)
