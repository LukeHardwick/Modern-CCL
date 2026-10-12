cmake_minimum_required(VERSION 3.28)
project(ModernCCL VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# No C++20 modules are used; module dependency scanning only adds dyndep steps (and trips a
# ninja 1.12 assertion in the GCC 14 Docker build).
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

if(PROJECT_IS_TOP_LEVEL)
    set(_cigi_top ON)
else()
    set(_cigi_top OFF)
endif()

option(CIGI_BUILD_EXAMPLES "Build the examples" ${_cigi_top})
option(CIGI_BUILD_TOOLS "Build the developer tools (pcap writer)" ${_cigi_top})
option(CIGI_BUILD_TESTS "Build the unit tests" ${_cigi_top})
option(CIGI_BUILD_TRANSPORT "Build the UDP transport (fetches Boost.Asio) and platform helpers"
       ${_cigi_top})
option(CIGI_BUILD_MOCK_IG "Build apps/mock_ig (headless mock-IG core)" ${_cigi_top})
option(CIGI_BUILD_VIEWER "Build apps/ig_viewer (fetches raylib, Dear ImGui and rlImGui)" OFF)
option(CIGI_BUILD_SESSION "Build the Host/IG sessions (fetches spdlog)" ${_cigi_top})
option(CIGI_ENABLE_SANITIZERS "Enable ASan (+UBSan on Clang/GCC)" OFF)
option(CIGI_ENABLE_FUZZING "Build the libFuzzer targets (Clang only; implies ASan + UBSan)" OFF)
option(CIGI_ENABLE_COVERAGE "Enable Clang source-based coverage" OFF)
option(CIGI_ENABLE_CLANG_TIDY "Run clang-tidy on cigi targets" OFF)
option(CIGI_WARNINGS_AS_ERRORS "Treat warnings as errors" ON)

# Sanitizers are applied globally (before third-party fetch) so all code is consistently instrumented.
include(Sanitizers)
include(CompilerWarnings)
include(Coverage)
include(StaticAnalysis)
include(Dependencies)

include(GenerateExportHeader)

add_library(cigi src/version.cpp src/error.cpp)
add_library(cigi::cigi ALIAS cigi)
target_sources(cigi PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES include/cigi/version.hpp
          include/cigi/dispatch.hpp
          include/cigi/error.hpp
          include/cigi/packet.hpp
          include/cigi/packet_set.hpp
          include/cigi/packets.hpp
          include/cigi/packets/articulated_part_control.hpp
          include/cigi/packets/celestial_sphere_control.hpp
          include/cigi/packets/common.hpp
          include/cigi/packets/component_control.hpp
          include/cigi/packets/component_data.hpp
          include/cigi/packets/entity_control.hpp
          include/cigi/packets/frame_lsn.hpp
          include/cigi/packets/hat_hot_extended_response.hpp
          include/cigi/packets/hat_hot_request.hpp
          include/cigi/packets/hat_hot_response.hpp
          include/cigi/packets/ig_control.hpp
          include/cigi/packets/rate_control.hpp
          include/cigi/packets/start_of_frame.hpp
          include/cigi/packets/view_control.hpp
          include/cigi/packets/view_definition.hpp
          include/cigi/message/builder.hpp
          include/cigi/message/view.hpp
          include/cigi/detail/assert.hpp
          include/cigi/wire/bits.hpp
          include/cigi/wire/byte_order.hpp
          include/cigi/wire/codec.hpp
          include/cigi/wire/fields.hpp
          include/cigi/wire/padding.hpp
          include/cigi/wire/utf8.hpp)
target_include_directories(cigi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(cigi PUBLIC cxx_std_23)
set_target_properties(cigi PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
generate_export_header(cigi
    BASE_NAME CIGI
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/cigi/export.hpp")

# The library must build without exceptions (RTTI stays enabled).
if(MSVC)
    target_compile_options(cigi PRIVATE /EHs-c-)
    target_compile_definitions(cigi PRIVATE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(cigi PRIVATE -fno-exceptions)
endif()

cigi_apply_project_options(cigi)

if(CIGI_BUILD_TRANSPORT)
    # Timing abstraction: the only place with timing-related platform #ifdefs.
    add_library(cigi_platform STATIC src/platform/sleep.cpp)
    add_library(cigi::platform ALIAS cigi_platform)
    target_sources(cigi_platform PUBLIC
        FILE_SET HEADERS BASE_DIRS include FILES include/cigi/platform/sleep.hpp)
    target_compile_features(cigi_platform PUBLIC cxx_std_23)
    if(WIN32)
        # PLATFORM: timeBeginPeriod/timeEndPeriod
        target_link_libraries(cigi_platform PRIVATE winmm)
    endif()

    # UDP transport on Boost.Asio, hidden behind a pimpl (no Boost in public headers).
    add_library(cigi_transport STATIC src/transport/udp.cpp src/transport/no_exceptions.cpp)
    add_library(cigi::transport ALIAS cigi_transport)
    target_sources(cigi_transport PUBLIC
        FILE_SET HEADERS BASE_DIRS include FILES include/cigi/transport/udp.hpp)
    target_compile_features(cigi_transport PUBLIC cxx_std_23)
    target_link_libraries(cigi_transport PUBLIC cigi::cigi PRIVATE Boost::asio_core)

    if(WIN32)
        # PLATFORM: Asio needs the Windows 10 API level and the Winsock libraries.
        target_compile_definitions(cigi_transport PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(cigi_transport PRIVATE ws2_32 mswsock)
    endif()

    foreach(_t cigi_platform cigi_transport)
        set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        if(MSVC)
            target_compile_options(${_t} PRIVATE /EHs-c-)
            target_compile_definitions(${_t} PRIVATE _HAS_EXCEPTIONS=0)
        else()
            target_compile_options(${_t} PRIVATE -fno-exceptions)
        endif()
        cigi_apply_project_options(${_t})
    endforeach()
endif()

if(CIGI_BUILD_TOOLS OR CIGI_BUILD_TESTS OR CIGI_BUILD_EXAMPLES)
    add_subdirectory(tools)
endif()

if(CIGI_BUILD_SESSION)
    # Host / IG sessions: per-peer protocol state on top of the core, transport-agnostic.
    add_library(cigi_session STATIC src/session/host_session.cpp src/session/ig_session.cpp
                                    src/session/session.cpp)
    add_library(cigi::session ALIAS cigi_session)
    target_sources(cigi_session PUBLIC
        FILE_SET HEADERS BASE_DIRS include
        FILES include/cigi/session/detail.hpp
              include/cigi/session/host_session.hpp
              include/cigi/session/ig_session.hpp
              include/cigi/session/session.hpp)
    target_compile_features(cigi_session PUBLIC cxx_std_23)
    target_link_libraries(cigi_session PUBLIC cigi::cigi spdlog::spdlog)
    set_target_properties(cigi_session PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(cigi_session PRIVATE /EHs-c-)
        target_compile_definitions(cigi_session PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(cigi_session PRIVATE -fno-exceptions)
    endif()
    cigi_apply_project_options(cigi_session)
endif()

if(TARGET cigi_session AND TARGET cigi_transport)
    # Host / Ig facades: the receive -> session -> send loop, timeouts and fault injection on top
    # of the sessions and the UDP transport. No Boost types in the public headers.
    add_library(cigi_runtime STATIC src/runtime/host.cpp src/runtime/ig.cpp
                                    src/runtime/memory_transport.cpp
                                    src/runtime/fault_transport.cpp)
    add_library(cigi::runtime ALIAS cigi_runtime)
    target_sources(cigi_runtime PUBLIC
        FILE_SET HEADERS BASE_DIRS include
        FILES include/cigi/datagram_transport.hpp
              include/cigi/fault_transport.hpp
              include/cigi/host.hpp
              include/cigi/ig.hpp
              include/cigi/link_state.hpp)
    target_compile_features(cigi_runtime PUBLIC cxx_std_23)
    target_link_libraries(cigi_runtime PUBLIC cigi::session
                          PRIVATE cigi::transport cigi::platform)
    target_include_directories(cigi_runtime PRIVATE src/runtime)
    set_target_properties(cigi_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(MSVC)
        target_compile_options(cigi_runtime PRIVATE /EHs-c-)
        target_compile_definitions(cigi_runtime PRIVATE _HAS_EXCEPTIONS=0)
    else()
        target_compile_options(cigi_runtime PRIVATE -fno-exceptions)
    endif()
    cigi_apply_project_options(cigi_runtime)
endif()

if(CIGI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# The mock IG (no graphics dependencies) builds everywhere; the viewer only on request.
if(CIGI_BUILD_VIEWER AND NOT CIGI_BUILD_MOCK_IG)
    message(STATUS "CIGI_BUILD_VIEWER needs the mock IG: enabling CIGI_BUILD_MOCK_IG")
    set(CIGI_BUILD_MOCK_IG ON)
endif()
if(CIGI_BUILD_MOCK_IG)
    add_subdirectory(apps/mock_ig)
endif()
if(CIGI_BUILD_VIEWER)
    add_subdirectory(apps/ig_viewer)
endif()

if(CIGI_BUILD_TESTS OR CIGI_ENABLE_FUZZING)
    enable_testing()
endif()

if(CIGI_BUILD_TESTS)
    add_subdirectory(tests)
    cigi_define_coverage_target()
endif()

# Corpus-replay ctests (always, with the tests) and the libFuzzer targets (CIGI_ENABLE_FUZZING).
if(CIGI_BUILD_TESTS OR CIGI_ENABLE_FUZZING)
    add_subdirectory(fuzz)
endif()
