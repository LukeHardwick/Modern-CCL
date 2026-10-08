add_library(cigi_coverage INTERFACE)
if(CIGI_ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CIGI_ENABLE_COVERAGE requires Clang (source-based coverage)")
    endif()
    target_compile_options(cigi_coverage INTERFACE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(cigi_coverage INTERFACE -fprofile-instr-generate)

    get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(LLVM_PROFDATA llvm-profdata HINTS "${_cxx_dir}" REQUIRED)
    find_program(LLVM_COV llvm-cov HINTS "${_cxx_dir}" REQUIRED)
endif()

# Defines the `coverage` target from every test registered with cigi_add_test (global property
# CIGI_TEST_TARGETS). Call it once, after add_subdirectory(tests).
function(cigi_define_coverage_target)
    if(NOT CIGI_ENABLE_COVERAGE)
        return()
    endif()
    get_property(_tests GLOBAL PROPERTY CIGI_TEST_TARGETS)
    if(NOT _tests)
        return()
    endif()
    list(POP_FRONT _tests _first)
    set(_objects "$<TARGET_FILE:${_first}>")
    foreach(_t IN LISTS _tests)
        list(APPEND _objects -object "$<TARGET_FILE:${_t}>")
    endforeach()
    get_property(_all_tests GLOBAL PROPERTY CIGI_TEST_TARGETS)

    set(_cov_dir "${CMAKE_BINARY_DIR}/coverage")
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_cov_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_cov_dir}"
        COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${_cov_dir}/%p-%m.profraw"
                ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
        COMMAND ${CMAKE_COMMAND} "-DPROFDATA=${LLVM_PROFDATA}" "-DCOV_DIR=${_cov_dir}"
                -P "${CMAKE_SOURCE_DIR}/cmake/CoverageMerge.cmake"
        COMMAND ${LLVM_COV} show -format=html "-output-dir=${_cov_dir}/html"
                "-instr-profile=${_cov_dir}/merged.profdata"
                "-ignore-filename-regex=(tests/|_deps/)"
                ${_objects}
        COMMAND ${LLVM_COV} report "-instr-profile=${_cov_dir}/merged.profdata"
                "-ignore-filename-regex=(tests/|_deps/)"
                ${_objects}
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${_cov_dir}/html/index.html"
        DEPENDS ${_all_tests}
        VERBATIM USES_TERMINAL)
endfunction()
