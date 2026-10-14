# Runs the real demo_ig and demo_host binaries against each other on loopback and checks that
# both exit with 0. The two commands of one execute_process run concurrently. demo_ig is silent
# (--log-level off) because its stdout is piped into demo_host, which has ended by then.
# The ports are picked at random from 20000-39999 and the run is retried (up to 5 times, new ports
# each time) when it fails, so a port that happens to be taken does not fail the test.
# Usage: cmake -DDEMO_IG=<exe> -DDEMO_HOST=<exe> -P smoke.cmake
set(ig_result "")
set(host_result "")
foreach(attempt RANGE 1 5)
    string(RANDOM LENGTH 4 ALPHABET 0123456789 seed_a)
    string(RANDOM LENGTH 4 ALPHABET 0123456789 seed_b)
    math(EXPR IG_PORT "20000 + (${seed_a} * 2) % 10000")
    math(EXPR HOST_PORT "30000 + (${seed_b} * 2) % 10000")
    execute_process(
        COMMAND ${DEMO_IG} --listen-addr 127.0.0.1 --listen-port ${IG_PORT} --duration 4
                --log-level off
        COMMAND ${DEMO_HOST} --ig-addr 127.0.0.1 --ig-port ${IG_PORT} --listen-port ${HOST_PORT}
                --duration 2
        RESULTS_VARIABLE results
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors
        TIMEOUT 30)
    message("${output}${errors}")
    list(GET results 0 ig_result)
    list(GET results 1 host_result)
    if(ig_result STREQUAL "0" AND host_result STREQUAL "0")
        return()
    endif()
    message(STATUS "attempt ${attempt} on ports ${IG_PORT}/${HOST_PORT} failed; retrying")
endforeach()
message(FATAL_ERROR "demo_ig exited with '${ig_result}', demo_host with '${host_result}'")
