if(NOT DEFINED CLI OR NOT DEFINED WORKSPACE)
    message(FATAL_ERROR "CLI and WORKSPACE are required")
endif()

file(REMOVE_RECURSE "${WORKSPACE}")
execute_process(
    COMMAND "${CLI}" --workspace "${WORKSPACE}" --project offline --json project status
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(result EQUAL 0 OR NOT error STREQUAL "")
    message(FATAL_ERROR "Offline CLI must fail through JSON stdout only: ${output} ${error}")
endif()
string(JSON code GET "${output}" error code)
if(NOT code STREQUAL "desktop_not_running")
    message(FATAL_ERROR "Expected desktop_not_running: ${output}")
endif()
if(EXISTS "${WORKSPACE}")
    message(FATAL_ERROR "Offline CLI must not create or modify the workspace")
endif()
