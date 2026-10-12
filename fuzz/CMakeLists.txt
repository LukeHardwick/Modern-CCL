# Fuzzing. Two layers:
#  * always (with the tests): <target>_replay executables that run the seed corpus through the
#    fuzz entry points, registered as ctests - no libFuzzer needed;
#  * with CIGI_ENABLE_FUZZING (Clang): the libFuzzer executables themselves.

set(CIGI_FUZZ_TARGETS message_view roundtrip)
if(TARGET cigi::session)
    list(APPEND CIGI_FUZZ_TARGETS sessions)
endif()

add_executable(cigi_gen_corpus gen_corpus.cpp)
target_link_libraries(cigi_gen_corpus PRIVATE cigi::cigi)
cigi_apply_project_options(cigi_gen_corpus)
# Test-support code (like tests/): warnings as errors, but not clang-tidy (C stdio, LLVMFuzzer* names).
set_target_properties(cigi_gen_corpus PROPERTIES CXX_CLANG_TIDY "")

foreach(t IN LISTS CIGI_FUZZ_TARGETS)
    add_executable(fuzz_${t}_replay fuzz_${t}.cpp replay_main.cpp)
    target_link_libraries(fuzz_${t}_replay PRIVATE cigi::cigi)
    if(t STREQUAL "sessions")
        target_link_libraries(fuzz_${t}_replay PRIVATE cigi::session)
    endif()
    cigi_apply_project_options(fuzz_${t}_replay)
    set_target_properties(fuzz_${t}_replay PROPERTIES CXX_CLANG_TIDY "")
    add_test(NAME fuzz_${t}_corpus_replay
        COMMAND fuzz_${t}_replay ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${t})
    set_tests_properties(fuzz_${t}_corpus_replay PROPERTIES LABELS fuzz)

    if(CIGI_ENABLE_FUZZING)
        add_executable(fuzz_${t} fuzz_${t}.cpp)
        target_link_libraries(fuzz_${t} PRIVATE cigi::cigi)
        if(t STREQUAL "sessions")
            target_link_libraries(fuzz_${t} PRIVATE cigi::session)
        endif()
        target_link_options(fuzz_${t} PRIVATE -fsanitize=fuzzer)
        cigi_apply_project_options(fuzz_${t})
        set_target_properties(fuzz_${t} PROPERTIES CXX_CLANG_TIDY "")
    endif()
endforeach()
