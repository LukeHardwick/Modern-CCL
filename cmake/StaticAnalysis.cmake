get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)

if(CIGI_ENABLE_CLANG_TIDY)
    find_program(CIGI_CLANG_TIDY clang-tidy HINTS "${_cxx_dir}" REQUIRED)
    set(CIGI_CLANG_TIDY_COMMAND "${CIGI_CLANG_TIDY}")
endif()

# Called for each of OUR targets only (never third-party).
function(cigi_apply_project_options target)
    target_link_libraries(${target} PRIVATE cigi_warnings cigi_coverage)
    if(CIGI_ENABLE_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${CIGI_CLANG_TIDY_COMMAND}")
    endif()
endfunction()

find_program(CIGI_CLANG_FORMAT clang-format HINTS "${_cxx_dir}" /opt/homebrew/opt/llvm/bin)
if(CIGI_CLANG_FORMAT)
    file(GLOB_RECURSE _cigi_format_files CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/include/*.hpp" "${PROJECT_SOURCE_DIR}/include/*.h"
        "${PROJECT_SOURCE_DIR}/src/*.cpp" "${PROJECT_SOURCE_DIR}/src/*.hpp"
        "${PROJECT_SOURCE_DIR}/tests/*.cpp" "${PROJECT_SOURCE_DIR}/tests/*.hpp"
        "${PROJECT_SOURCE_DIR}/tools/*.cpp" "${PROJECT_SOURCE_DIR}/tools/*.hpp")
    add_custom_target(format
        COMMAND ${CIGI_CLANG_FORMAT} -i ${_cigi_format_files}
        COMMENT "Formatting sources" VERBATIM)
    add_custom_target(format-check
        COMMAND ${CIGI_CLANG_FORMAT} --dry-run --Werror ${_cigi_format_files}
        COMMENT "Checking formatting" VERBATIM)
endif()
