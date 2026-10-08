# Run with: cmake -DPROFDATA=... -DCOV_DIR=... -P CoverageMerge.cmake
# Expands the *.profraw glob (custom-target commands run without a shell) and merges.
file(GLOB _raw "${COV_DIR}/*.profraw")
if(NOT _raw)
    message(FATAL_ERROR "No .profraw files found in ${COV_DIR}")
endif()
execute_process(COMMAND "${PROFDATA}" merge -sparse -o "${COV_DIR}/merged.profdata" ${_raw}
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "llvm-profdata merge failed")
endif()
