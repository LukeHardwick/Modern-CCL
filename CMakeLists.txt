cmake_minimum_required(VERSION 3.28)
project(ModernCCL VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

if(PROJECT_IS_TOP_LEVEL)
    set(_cigi_top ON)
else()
    set(_cigi_top OFF)
endif()

option(CIGI_BUILD_EXAMPLES "Build the examples" ${_cigi_top})
option(CIGI_BUILD_TESTS "Build the unit tests" ${_cigi_top})
option(CIGI_BUILD_TRANSPORT "Build the UDP transport (fetches Boost.Asio) and platform helpers"
       ${_cigi_top})
option(CIGI_ENABLE_SANITIZERS "Enable ASan (+UBSan on Clang/GCC)" OFF)
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
          include/cigi/error.hpp
          include/cigi/packet.hpp
          include/cigi/packets/common.hpp
          include/cigi/packets/ig_control.hpp
          include/cigi/packets/start_of_frame.hpp
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

if(CIGI_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(CIGI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    cigi_define_coverage_target()
endif()
