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
    # Boost.Asio from the official Boost CMake release tarball (superproject), version 1.92.0
    # (released 2026-08-12). The 108 MB tarball is downloaded once into CIGI_DEPS_CACHE and
    # extracted once, so every build tree (presets, Docker) reuses it.
    set(CIGI_DEPS_CACHE "${PROJECT_SOURCE_DIR}/.cache/deps" CACHE PATH
        "Directory caching downloaded dependency archives (shared between build trees)")
    # The source tree may be read-only (e.g. mounted read-only in Docker): fall back to the build tree.
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${CIGI_DEPS_CACHE}"
                    RESULT_VARIABLE _cache_mkdir OUTPUT_QUIET ERROR_QUIET)
    if(_cache_mkdir EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} -E touch "${CIGI_DEPS_CACHE}/.write-probe"
                        RESULT_VARIABLE _cache_touch OUTPUT_QUIET ERROR_QUIET)
    endif()
    if(NOT _cache_mkdir EQUAL 0 OR NOT _cache_touch EQUAL 0)
        message(STATUS "CIGI_DEPS_CACHE (${CIGI_DEPS_CACHE}) is not writable; using the build tree")
        set(CIGI_DEPS_CACHE "${CMAKE_BINARY_DIR}/deps-cache")
        file(MAKE_DIRECTORY "${CIGI_DEPS_CACHE}")
    endif()

    set(_boost_hash 9bed76128d4e46755dbe818487788c6fceb6f72b378f4daa49b7e1e600d9088d)
    set(_boost_url
        https://github.com/boostorg/boost/releases/download/boost-1.92.0/boost-1.92.0-cmake.tar.xz)
    set(_boost_tar "${CIGI_DEPS_CACHE}/boost-1.92.0-cmake.tar.xz")
    set(_boost_dir "${CIGI_DEPS_CACHE}/boost-1.92.0-extracted")
    set(_boost_stamp "${_boost_dir}/.sha256")

    set(_stamp_hash "")
    if(EXISTS "${_boost_stamp}")
        file(READ "${_boost_stamp}" _stamp_hash)
    endif()
    if(NOT _stamp_hash STREQUAL _boost_hash)
        if(NOT EXISTS "${_boost_tar}")
            message(STATUS "Downloading ${_boost_url}")
            file(DOWNLOAD "${_boost_url}" "${_boost_tar}.part"
                 EXPECTED_HASH SHA256=${_boost_hash} SHOW_PROGRESS STATUS _dl_status)
            list(GET _dl_status 0 _dl_code)
            if(NOT _dl_code EQUAL 0)
                file(REMOVE "${_boost_tar}.part")
                message(FATAL_ERROR "Boost download failed: ${_dl_status}")
            endif()
            file(RENAME "${_boost_tar}.part" "${_boost_tar}")
        endif()
        file(SHA256 "${_boost_tar}" _tar_hash)
        if(NOT _tar_hash STREQUAL _boost_hash)
            file(REMOVE "${_boost_tar}")
            message(FATAL_ERROR "Cached Boost archive has the wrong SHA256; deleted, re-run CMake")
        endif()
        file(REMOVE_RECURSE "${_boost_dir}")
        file(ARCHIVE_EXTRACT INPUT "${_boost_tar}" DESTINATION "${_boost_dir}")
        file(WRITE "${_boost_stamp}" "${_boost_hash}")
    endif()

    FetchContent_Declare(Boost
        URL ${_boost_url}
        URL_HASH SHA256=${_boost_hash}
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    set(FETCHCONTENT_SOURCE_DIR_BOOST "${_boost_dir}/boost-1.92.0")

    # Normal variables inside block() so a parent project's settings are never overridden.
    block()
        set(BOOST_INCLUDE_LIBRARIES asio)
        set(BOOST_ENABLE_MPI OFF)
        set(BUILD_TESTING OFF)
        FetchContent_MakeAvailable(Boost)
    endblock()
endif()

if(CIGI_BUILD_VIEWER)
    # Viewer dependencies (user-approved 2026-10-12). Versions are the latest releases at least
    # two weeks old on 2026-10-12: raylib 6.0 (2026-04-23), Dear ImGui 1.92.9b (2026-07-31);
    # rlImGui has no releases, so a commit (2026-08-24) is pinned. All third-party code is built
    # without warnings (-w), as SYSTEM includes, and outside clang-tidy and coverage.
    FetchContent_Declare(raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
        URL_HASH SHA256=2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
        URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
        DOWNLOAD_EXTRACT_TIMESTAMP ON)
    FetchContent_Declare(rlimgui
        URL https://github.com/raylib-extras/rlImGui/archive/1550009359ad975927f7f0e4a3f47e3f27123ea9.tar.gz
        URL_HASH SHA256=7d04ff0c996a89190e1050d3c4448ad784479723c76893efea2d731bd5eb839f
        DOWNLOAD_EXTRACT_TIMESTAMP ON)

    # Normal variables inside block() so they never leak out of this scope.
    block()
        set(BUILD_EXAMPLES OFF)
        set(BUILD_GAMES OFF)
        set(BUILD_SHARED_LIBS OFF)
        set(CUSTOMIZE_BUILD OFF)
        FetchContent_MakeAvailable(raylib imgui rlimgui)
    endblock()
    if(NOT MSVC)
        target_compile_options(raylib PRIVATE -w)
    endif()

    FetchContent_GetProperties(imgui SOURCE_DIR imgui_SOURCE_DIR)
    FetchContent_GetProperties(rlimgui SOURCE_DIR rlimgui_SOURCE_DIR)

    # Dear ImGui and rlImGui ship no CMake build: one static library for both.
    add_library(cigi_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${rlimgui_SOURCE_DIR}/rlImGui.cpp)
    target_include_directories(cigi_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
    target_link_libraries(cigi_imgui PUBLIC raylib)
    target_compile_features(cigi_imgui PUBLIC cxx_std_17)
    # 32-bit draw indices: the map window with many entities and trails exceeds 65535 vertices.
    # PUBLIC so rlImGui and the viewer agree with imgui.cpp on the ImDrawIdx type.
    target_compile_definitions(cigi_imgui PUBLIC ImDrawIdx=unsigned\ int)
    if(MSVC)
        target_compile_options(cigi_imgui PRIVATE /w)
    else()
        target_compile_options(cigi_imgui PRIVATE -w)
    endif()
endif()

if(CIGI_BUILD_SESSION)
    # spdlog 1.17.0 (released 2026-01-04; tag verified with `git ls-remote`, object 79524dd...),
    # built against std::format and without exceptions. Only cigi_session links it; the core
    # `cigi` library stays dependency-free.
    FetchContent_Declare(spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
        URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
        SYSTEM)
    # Normal variables inside block() so a parent project's settings are never overridden.
    block()
        set(SPDLOG_USE_STD_FORMAT ON)
        set(SPDLOG_NO_EXCEPTIONS ON)
        set(SPDLOG_BUILD_PIC ON)
        set(SPDLOG_BUILD_EXAMPLE OFF)
        set(SPDLOG_BUILD_TESTS OFF)
        set(SPDLOG_BUILD_BENCH OFF)
        set(SPDLOG_INSTALL OFF)
        FetchContent_MakeAvailable(spdlog)
    endblock()
endif()
