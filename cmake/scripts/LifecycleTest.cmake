cmake_minimum_required(VERSION 3.17)

#
# Runs a lifecycle test: generates a new game or loads a save, runs for N ticks,
# and checks that the game doesn't crash.
#
# Required variables:
#   OPENTTD_EXECUTABLE  - Path to the OpenTTD executable
#   LIFECYCLE_TEST      - Test name (used for crash file naming)
#
# Optional variables:
#   OPENTTD_ARGS        - Additional arguments to pass to OpenTTD
#   LIFECYCLE_TICKS     - Number of ticks to run (default: 100)
#

if(NOT OPENTTD_EXECUTABLE)
    message(FATAL_ERROR "Script needs OPENTTD_EXECUTABLE defined")
endif()
if(NOT LIFECYCLE_TEST)
    message(FATAL_ERROR "Script needs LIFECYCLE_TEST defined")
endif()
if(NOT LIFECYCLE_TICKS)
    set(LIFECYCLE_TICKS 100)
endif()

# If editbin is given, copy the executable and set console subsystem.
if(EDITBIN_EXECUTABLE)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${OPENTTD_EXECUTABLE} lifecycle_${LIFECYCLE_TEST}.exe)
    set(OPENTTD_EXECUTABLE "${CMAKE_CURRENT_BINARY_DIR}/lifecycle_${LIFECYCLE_TEST}.exe")
    if(NOT EXISTS "${OPENTTD_EXECUTABLE}")
        set(OPENTTD_EXECUTABLE "./lifecycle_${LIFECYCLE_TEST}.exe")
    endif()
    execute_process(COMMAND ${EDITBIN_EXECUTABLE} /nologo /subsystem:console ${OPENTTD_EXECUTABLE})
endif()

# Remove previous crash files from both possible locations
file(GLOB CRASH_FILES "regression/crash*" "crash*.log" "crash*.json.log")
if(CRASH_FILES)
    file(REMOVE ${CRASH_FILES})
endif()

# Build the command line
separate_arguments(EXTRA_ARGS NATIVE_COMMAND "${OPENTTD_ARGS}")

# Run OpenTTD headlessly
execute_process(
    COMMAND ${OPENTTD_EXECUTABLE}
        -x
        -c regression/regression.cfg
        -snull
        -mnull
        -vnull:ticks=${LIFECYCLE_TICKS}
        -Q
        ${EXTRA_ARGS}
    OUTPUT_VARIABLE TEST_OUTPUT
    ERROR_VARIABLE TEST_STDERR
    RESULT_VARIABLE TEST_EXIT_CODE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    TIMEOUT 300
)

# Check for crash files in both regression/ dir and working directory.
# OpenTTD may write crash logs to either location depending on config.
file(GLOB CRASH_FILES
    "regression/crash*.log" "regression/crash*.json.log"
    "crash*.log" "crash*.json.log"
)
if(CRASH_FILES)
    foreach(CRASH_FILE ${CRASH_FILES})
        file(READ ${CRASH_FILE} CRASH_LOG)
        message(STATUS "Crash log (${CRASH_FILE}): ${CRASH_LOG}")
    endforeach()
    message(FATAL_ERROR "Lifecycle test '${LIFECYCLE_TEST}' CRASHED. See crash log(s) above.")
endif()

# Check exit code
if(NOT TEST_EXIT_CODE EQUAL 0)
    message(STATUS "stderr: ${TEST_STDERR}")
    message(STATUS "stdout: ${TEST_OUTPUT}")
    message(FATAL_ERROR "Lifecycle test '${LIFECYCLE_TEST}' failed with exit code ${TEST_EXIT_CODE}")
endif()

message(STATUS "Lifecycle test '${LIFECYCLE_TEST}' PASSED (${LIFECYCLE_TICKS} ticks)")
