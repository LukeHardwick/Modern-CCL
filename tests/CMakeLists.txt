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
    packets/hat_hot_test.cpp
    packets/view_control_test.cpp
    packets/view_definition_test.cpp
    packets/random_roundtrip_test.cpp)

cigi_add_test(cigi_entity_packet_test SOURCES
    packets/articulated_part_control_test.cpp
    packets/component_control_test.cpp
    packets/entity_control_test.cpp
    packets/rate_control_test.cpp)

cigi_add_test(cigi_message_test SOURCES
    message/builder_test.cpp
    message/dispatch_test.cpp
    message/view_test.cpp)


# Replaces the global operator new to prove dispatch is allocation-free.
cigi_add_test(cigi_dispatch_alloc_test SOURCES message/dispatch_alloc_test.cpp)
# clang-tidy cannot accept a user-provided operator new: libc++ declares its parameter as __sz.
set_target_properties(cigi_dispatch_alloc_test PROPERTIES CXX_CLANG_TIDY "")

if(CIGI_BUILD_TRANSPORT)
    cigi_add_test(cigi_platform_test SOURCES platform/sleep_test.cpp LINK cigi::platform)
    cigi_add_test(cigi_transport_test SOURCES transport/udp_test.cpp transport/internal_test.cpp
        LINK cigi::transport Boost::asio_core LABELS network)
    target_include_directories(cigi_transport_test PRIVATE ${PROJECT_SOURCE_DIR}/src/transport)
    # The internal tests exercise Boost's no-exceptions hook, so they are built like the library.
    if(NOT MSVC)
        set_source_files_properties(transport/internal_test.cpp PROPERTIES
            COMPILE_OPTIONS -fno-exceptions)
    endif()
endif()

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
