# Runs the real demo_ig and demo_host binaries against each other on loopback and checks that
# both exit with 0. The two commands of one execute_process run concurrently. demo_ig is silent
# (--log-level off) because its stdout is piped into demo_host, which has ended by then.
# Usage: cmake -DDEMO_IG=<exe> -DDEMO_HOST=<exe> -DIG_PORT=<n> -DHOST_PORT=<n> -P smoke.cmake
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
if(NOT ig_result STREQUAL "0" OR NOT host_result STREQUAL "0")
    message(FATAL_ERROR "demo_ig exited with '${ig_result}', demo_host with '${host_result}'")
endif()
