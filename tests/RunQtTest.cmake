# Capture Qt Test output even when the Windows runner has no console attached.
file(REMOVE "${TEST_LOG}")
execute_process(COMMAND "${TEST_EXECUTABLE}" -o "${TEST_LOG},txt"
        RESULT_VARIABLE result
        TIMEOUT 25)
if (EXISTS "${TEST_LOG}")
    file(READ "${TEST_LOG}" output)
    message("${output}")
endif ()
if (NOT result STREQUAL "0")
    message(FATAL_ERROR "Qt Test failed: ${result}")
endif ()
