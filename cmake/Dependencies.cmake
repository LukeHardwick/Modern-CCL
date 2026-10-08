include(FetchContent)

# Third-party dependencies are always fetched (pinned, hashed) for reproducibility.
set(FETCHCONTENT_QUIET OFF)

if(CIGI_BUILD_TESTS)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
        URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_MakeAvailable(googletest)
    # Third-party code: silence its own warnings (ours are never applied to it).
    if(NOT MSVC)
        foreach(_t gtest gtest_main gmock gmock_main)
            target_compile_options(${_t} PRIVATE -w)
        endforeach()
    endif()
endif()

if(CIGI_BUILD_TRANSPORT)
    # Boost.Asio via the official Boost CMake release tarball (superproject). Only asio and its
    # dependencies are configured. Official release, published 2026-08-12.
    set(BOOST_INCLUDE_LIBRARIES asio CACHE STRING "" FORCE)
    set(BOOST_ENABLE_MPI OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(Boost
        URL https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz
        URL_HASH SHA256=9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(Boost)
endif()
