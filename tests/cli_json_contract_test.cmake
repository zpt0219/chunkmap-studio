if(NOT DEFINED CLI)
    message(FATAL_ERROR "CLI is required")
endif()

function(assert_contract output expected_ok expected_command expected_code)
    string(STRIP "${output}" json)
    string(JSON schema ERROR_VARIABLE json_error GET "${json}" schema_version)
    if(json_error OR NOT schema EQUAL 1)
        message(FATAL_ERROR "Invalid schema_version: ${json}")
    endif()
    string(JSON ok GET "${json}" ok)
    if(expected_ok AND NOT ok)
        message(FATAL_ERROR "Expected success: ${json}")
    elseif(NOT expected_ok AND ok)
        message(FATAL_ERROR "Expected failure: ${json}")
    endif()
    string(JSON command GET "${json}" command)
    if(NOT command STREQUAL expected_command)
        message(FATAL_ERROR "Unexpected command: ${json}")
    endif()
    string(JSON project_type TYPE "${json}" project)
    if(NOT project_type STREQUAL "NULL" AND NOT project_type STREQUAL "STRING")
        message(FATAL_ERROR "Project must be null or string: ${json}")
    endif()
    if(expected_ok)
        string(JSON data_type ERROR_VARIABLE data_error TYPE "${json}" data)
        if(data_error OR NOT data_type STREQUAL "OBJECT")
            message(FATAL_ERROR "Success response must contain data: ${json}")
        endif()
    else()
        string(JSON code ERROR_VARIABLE code_error GET "${json}" error code)
        string(JSON message ERROR_VARIABLE message_error GET "${json}" error message)
        if(code_error OR message_error OR NOT code STREQUAL expected_code OR message STREQUAL "")
            message(FATAL_ERROR "Invalid error response: ${json}")
        endif()
    endif()
endfunction()

execute_process(
    COMMAND "${CLI}" --json --version
    RESULT_VARIABLE success_result
    OUTPUT_VARIABLE success_output
    ERROR_VARIABLE success_error
)
if(NOT success_result EQUAL 0 OR NOT success_error STREQUAL "")
    message(FATAL_ERROR "JSON success polluted stderr or failed: ${success_error}")
endif()
assert_contract("${success_output}" TRUE "version" "")

execute_process(
    COMMAND "${CLI}" --json unknown
    RESULT_VARIABLE usage_result
    OUTPUT_VARIABLE usage_output
)
if(usage_result EQUAL 0)
    message(FATAL_ERROR "Unknown command must fail")
endif()
assert_contract("${usage_output}" FALSE "usage" "invalid_usage")

execute_process(
    COMMAND "${CLI}" --json --workspace
    RESULT_VARIABLE parser_result
    OUTPUT_VARIABLE parser_output
)
if(parser_result EQUAL 0)
    message(FATAL_ERROR "Missing option value must fail")
endif()
assert_contract("${parser_output}" FALSE "usage" "missing_option_value")

execute_process(
    COMMAND "${CLI}" --json project current
    RESULT_VARIABLE current_result
    OUTPUT_VARIABLE current_output
)
if(current_result EQUAL 0)
    message(FATAL_ERROR "Current project must fail when Desktop has no open project")
endif()
assert_contract("${current_output}" FALSE "project current" "no_project_open")

execute_process(
    COMMAND "${CLI}" --json --project missing project status
    RESULT_VARIABLE project_result
    OUTPUT_VARIABLE project_output
)
if(project_result EQUAL 0)
    message(FATAL_ERROR "Missing project must fail")
endif()
assert_contract("${project_output}" FALSE "project status" "file_open_failed")
string(JSON project GET "${project_output}" project)
if(NOT project STREQUAL "missing")
    message(FATAL_ERROR "Project name was not preserved: ${project_output}")
endif()
