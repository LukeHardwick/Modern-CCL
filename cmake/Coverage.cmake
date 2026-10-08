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

    set(_cov_dir "${CMAKE_BINARY_DIR}/coverage")
    add_custom_target(coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${_cov_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_cov_dir}"
        COMMAND ${CMAKE_COMMAND} -E env "LLVM_PROFILE_FILE=${_cov_dir}/%p-%m.profraw"
                ${CMAKE_CTEST_COMMAND} --test-dir "${CMAKE_BINARY_DIR}" --output-on-failure
        COMMAND ${CMAKE_COMMAND} "-DPROFDATA=${LLVM_PROFDATA}" "-DCOV_DIR=${_cov_dir}"
                -P "${CMAKE_CURRENT_LIST_DIR}/CoverageMerge.cmake"
        COMMAND ${LLVM_COV} show -format=html "-output-dir=${_cov_dir}/html"
                "-instr-profile=${_cov_dir}/merged.profdata"
                "-ignore-filename-regex=(tests/|_deps/)"
                $<TARGET_FILE:cigi_smoke_test>
        COMMAND ${LLVM_COV} report "-instr-profile=${_cov_dir}/merged.profdata"
                "-ignore-filename-regex=(tests/|_deps/)"
                $<TARGET_FILE:cigi_smoke_test>
        COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${_cov_dir}/html/index.html"
        DEPENDS cigi_smoke_test
        VERBATIM USES_TERMINAL)
endif()
