include(Testing)

cigi_add_test(cigi_smoke_test SOURCES smoke_test.cpp)

cigi_add_test(cigi_wire_test SOURCES
    wire/bits_test.cpp
    wire/byte_order_test.cpp
    wire/codec_test.cpp
    wire/error_test.cpp
    wire/fields_test.cpp
    wire/padding_test.cpp
    wire/utf8_test.cpp)

cigi_add_test(cigi_packet_test SOURCES
    packets/packet_test.cpp
    packets/ig_control_test.cpp
    packets/start_of_frame_test.cpp
    packets/celestial_sphere_control_test.cpp
    packets/view_control_test.cpp
    packets/view_definition_test.cpp
    packets/random_roundtrip_test.cpp)

cigi_add_test(cigi_message_test SOURCES
    message/builder_test.cpp
    message/view_test.cpp)

# Wireshark round trip: only when the example is built and the Wireshark command-line tools exist.
find_program(CIGI_TSHARK tshark)
find_program(CIGI_TEXT2PCAP text2pcap)
find_program(CIGI_MERGECAP mergecap)
if(TARGET cigi_frames AND CIGI_TSHARK AND CIGI_TEXT2PCAP AND CIGI_MERGECAP)
    get_filename_component(_cigi_wireshark_bin "${CIGI_TSHARK}" DIRECTORY)
    add_test(NAME wireshark_cigi_frames
        COMMAND ${CMAKE_SOURCE_DIR}/examples/wireshark/check_pcap.sh
                $<TARGET_FILE:cigi_frames> ${CMAKE_CURRENT_BINARY_DIR}/wireshark)
    set_tests_properties(wireshark_cigi_frames PROPERTIES
        LABELS wireshark
        ENVIRONMENT "PATH=${_cigi_wireshark_bin}:$ENV{PATH}")
endif()
