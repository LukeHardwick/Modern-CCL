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
