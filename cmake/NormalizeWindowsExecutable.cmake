# A build tree created by a sandbox can inherit a Low integrity label. Windows
# then launches the EXE at Low even from a normal IDE, preventing profile writes
# and UIAccess handoff. Normalize only our generated executable; retain its
# original DACL and leave workspace directories, user data and privileges alone.
if (NOT DEFINED APP_FILE OR NOT EXISTS "${APP_FILE}")
    message(FATAL_ERROR "Windows executable is missing: ${APP_FILE}")
endif ()
execute_process(COMMAND powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass
        -File "${CMAKE_CURRENT_LIST_DIR}/NormalizeWindowsExecutable.ps1" -AppFile "${APP_FILE}"
        RESULT_VARIABLE integrity_result OUTPUT_VARIABLE integrity_output ERROR_VARIABLE integrity_error)
if (NOT integrity_result EQUAL 0)
    message(FATAL_ERROR
            "Could not set the generated EXE's integrity level to Medium. "
            "Do not run this build: a Low-integrity EXE cannot write normal user data. "
            "Build from a normal Windows desktop terminal/IDE outside the execution sandbox. "
            "${integrity_output}${integrity_error}")
endif ()
