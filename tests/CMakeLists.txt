include(Testing)

cigi_add_test(cigi_smoke_test SOURCES smoke_test.cpp)

cigi_add_test(cigi_wire_test SOURCES
    wire/bits_test.cpp
    wire/byte_order_test.cpp
    wire/codec_test.cpp
    wire/error_test.cpp
    wire/padding_test.cpp
    wire/utf8_test.cpp)
