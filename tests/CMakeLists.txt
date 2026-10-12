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

cigi_add_test(cigi_user_packet_test SOURCES packets/user_packet_test.cpp)

cigi_add_test(cigi_message_test SOURCES
    message/builder_test.cpp
    message/dispatch_test.cpp
    message/view_test.cpp)

# Replaces the global operator new to prove dispatch is allocation-free.
cigi_add_test(cigi_dispatch_alloc_test SOURCES message/dispatch_alloc_test.cpp)
# clang-tidy cannot accept a user-provided operator new: libc++ declares its parameter as __sz.
set_target_properties(cigi_dispatch_alloc_test PROPERTIES CXX_CLANG_TIDY "")

# Mock IG (apps/mock_ig): scene model, HAT/HOT responder, terrain, describe(), scripted host.
if(CIGI_BUILD_MOCK_IG)
    cigi_add_test(cigi_mock_ig_test SOURCES
        mock_ig/describe_test.cpp
        mock_ig/geo_test.cpp
        mock_ig/hat_hot_test.cpp
        mock_ig/mock_ig_test.cpp
        mock_ig/scene_test.cpp
        mock_ig/scripted_host_test.cpp
        mock_ig/sky_test.cpp
        mock_ig/terrain_test.cpp
        LINK cigi_mock_ig)
endif()

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

if(CIGI_BUILD_SESSION)
    cigi_add_test(cigi_session_test SOURCES
        session/host_session_test.cpp
        session/ig_session_test.cpp
        session/exchange_test.cpp
        LINK cigi::session)
endif()

if(TARGET cigi_runtime)
    cigi_add_test(cigi_runtime_test SOURCES
        runtime/host_test.cpp
        runtime/ig_test.cpp
        runtime/transport_test.cpp
        LINK cigi::runtime)
    # Replaces the global operator new to prove the steady-state frame path is allocation-free.
    cigi_add_test(cigi_runtime_alloc_test SOURCES runtime/host_alloc_test.cpp LINK cigi::runtime)
    set_target_properties(cigi_runtime_alloc_test PROPERTIES CXX_CLANG_TIDY "")
    # Host and Ig over real loopback UDP.
    cigi_add_test(cigi_runtime_network_test SOURCES runtime/network_test.cpp
        LINK cigi::runtime cigi::transport LABELS network)
endif()

find_program(CIGI_TSHARK tshark)

cigi_add_test(cigi_tools_test SOURCES tools/pcap_writer_test.cpp LINK cigi::tools)
if(CIGI_TSHARK)
    # Reads the pcap writer's output back with tshark.
    cigi_add_test(cigi_tools_tshark_test SOURCES tools/pcap_writer_tshark_test.cpp
        LINK cigi::tools LABELS wireshark)
    target_compile_definitions(cigi_tools_tshark_test PRIVATE CIGI_TSHARK_PATH="${CIGI_TSHARK}")
endif()

# Corpus generator for the Wireshark verification (tests/interop/wireshark).
add_executable(make_wireshark_corpus interop/wireshark/make_wireshark_corpus.cpp)
target_link_libraries(make_wireshark_corpus PRIVATE cigi::cigi cigi::tools)
cigi_apply_project_options(make_wireshark_corpus)

# Wireshark verification: only when tshark is installed.
if(CIGI_TSHARK)
    get_filename_component(_cigi_wireshark_bin "${CIGI_TSHARK}" DIRECTORY)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        # Decode every MVP packet of the corpus with tshark and compare it with what was encoded.
        add_test(NAME wireshark_corpus
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/interop/wireshark/check_tshark.py
                    --generator $<TARGET_FILE:make_wireshark_corpus>
                    --out ${CMAKE_CURRENT_BINARY_DIR}/wireshark_corpus
                    --tshark ${CIGI_TSHARK})
        set_tests_properties(wireshark_corpus PROPERTIES LABELS wireshark)
    else()
        message(STATUS "wireshark_corpus test skipped: Python 3 interpreter not found")
    endif()
else()
    message(STATUS "Wireshark tests skipped: tshark not found (set CIGI_TSHARK to enable)")
endif()
if(TARGET cigi_frames AND CIGI_TSHARK)
    add_test(NAME wireshark_cigi_frames
        COMMAND ${CMAKE_SOURCE_DIR}/examples/wireshark/check_pcap.sh
                $<TARGET_FILE:cigi_frames> ${CMAKE_CURRENT_BINARY_DIR}/wireshark)
    set_tests_properties(wireshark_cigi_frames PROPERTIES
        LABELS wireshark
        ENVIRONMENT "PATH=${_cigi_wireshark_bin}:$ENV{PATH}")
endif()
