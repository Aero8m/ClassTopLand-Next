foreach (suite IN ITEMS date_schedule course_refresh profile_exchange persistence profile_edit reminders)
    add_executable(test_${suite} ${suite}.cpp)
    target_link_libraries(test_${suite} PRIVATE classtopland_core Qt6::Test)
    target_include_directories(test_${suite} PRIVATE "${PROJECT_SOURCE_DIR}/src")
    if (MSVC)
        target_compile_options(test_${suite} PRIVATE /utf-8)
    endif ()

    add_test(NAME ${suite} COMMAND "${CMAKE_COMMAND}"
            "-DTEST_EXECUTABLE=$<TARGET_FILE:test_${suite}>"
            "-DTEST_LOG=${CMAKE_CURRENT_BINARY_DIR}/${suite}.log"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RunQtTest.cmake")
    set_tests_properties(${suite} PROPERTIES TIMEOUT 30)
    if (WIN32)
        set_tests_properties(${suite} PROPERTIES
                ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;TEMP=set:${CMAKE_CURRENT_BINARY_DIR};TMP=set:${CMAKE_CURRENT_BINARY_DIR}")
    endif ()
endforeach ()
