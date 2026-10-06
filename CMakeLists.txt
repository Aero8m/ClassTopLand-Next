cmake_minimum_required(VERSION 4.0)
project(ClassTopLand-Next)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_PREFIX_PATH "C:/Qt/6.9.3/msvc2022_64/lib/cmake")

find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        REQUIRED)

if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt")
    message(FATAL_ERROR "yaml-cpp submodule is missing. Run: git submodule update --init --recursive")
endif ()

# AUTO INSERT THE MSVC PROPERTIES

set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_MSVC_SHARED_RT ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/yaml-cpp)

add_subdirectory(third_party/ElaWidgetTools-Plus/ElaWidgetTools)

add_executable(ClassTopLand-Next
        main.cpp
        resourecs.qrc
        src/VERSION.h

        src/Core/Model/AppearanceConfig.h
        src/Core/Model/Class.h
        src/Core/Model/Config.h
        src/Core/Model/CourseBarConfig.h
        src/Core/Model/Profile.h
        src/Core/Model/Schedule.h
        src/Core/Model/Subject.h
        src/Core/Model/TimeLine.h
        src/Core/DateSchedule/DateSchedule.cpp
        src/Core/DateSchedule/DateSchedule.h
        src/ScheduleAdjustments/DateScheduleWidgets.cpp
        src/ScheduleAdjustments/DateScheduleWidgets.h

        src/Core/AppComposer/AppComposer.cpp
        src/Core/AppComposer/AppComposer.h
        src/Core/ConfigManager/ConfigManager.cpp
        src/Core/ConfigManager/ConfigManager.h
        src/Core/CourseRefreshService/CourseRefreshService.cpp
        src/Core/CourseRefreshService/CourseRefreshService.h
        src/Core/Logger/Logger.cpp
        src/Core/Logger/Logger.h
        src/Core/ProfileManager/ProfileExchange.cpp
        src/Core/ProfileManager/ProfileExchange.h
        src/Core/ProfileManager/ProfileManager.cpp
        src/Core/ProfileManager/ProfileManager.h
        src/Core/ThemeManager/ThemeManager.cpp
        src/Core/ThemeManager/ThemeManager.h

        src/CourseBar/CourseBar.cpp
        src/CourseBar/CourseBar.h
        src/CourseBar/ICourseBarComponent.h
        src/CourseBar/BuiltinComponents/CourseView/CourseView.cpp
        src/CourseBar/BuiltinComponents/CourseView/CourseView.h
        src/CourseBar/BuiltinComponents/Date/Date.cpp
        src/CourseBar/BuiltinComponents/Date/Date.h
        src/CourseBar/BuiltinComponents/TextTip/TextTip.cpp
        src/CourseBar/BuiltinComponents/TextTip/TextTip.h

        src/ProfileEditWindow/ProfileEditSession.cpp
        src/ProfileEditWindow/ProfileEditSession.h
        src/ProfileEditWindow/ProfileEditWidgets.cpp
        src/ProfileEditWindow/ProfileEditWidgets.h
        src/ProfileEditWindow/ProfileEditWindow.cpp
        src/ProfileEditWindow/ProfileEditWindow.h
        src/ProfileEditWindow/tabs/ProfileInfoTab.cpp
        src/ProfileEditWindow/tabs/ProfileInfoTab.h
        src/ProfileEditWindow/tabs/SchedulesTab.cpp
        src/ProfileEditWindow/tabs/SchedulesTab.h
        src/ProfileEditWindow/tabs/SubjectsTab.cpp
        src/ProfileEditWindow/tabs/SubjectsTab.h
        src/ProfileEditWindow/tabs/TimeLinesTab.cpp
        src/ProfileEditWindow/tabs/TimeLinesTab.h
        src/ProfileEditWindow/widgets/TimeLineCanvas.cpp
        src/ProfileEditWindow/widgets/TimeLineCanvas.h

        src/SettingsWindow/SettingsWindow.cpp
        src/SettingsWindow/SettingsWindow.h
        src/SettingsWindow/AboutDialog.cpp
        src/SettingsWindow/AboutDialog.h
        src/SettingsWindow/tabs/AppearanceSettingsTab.cpp
        src/SettingsWindow/tabs/AppearanceSettingsTab.h
        src/SettingsWindow/tabs/CourseBarSettingsTab.cpp
        src/SettingsWindow/tabs/CourseBarSettingsTab.h
        src/SettingsWindow/tabs/ProfileSettingsTab.cpp
        src/SettingsWindow/tabs/ProfileSettingsTab.h

        src/TaskbarTrayMenu/TaskbarTrayMenu.cpp
        src/TaskbarTrayMenu/TaskbarTrayMenu.h

        src/Utils/GlassHelper/GlassHelper.cpp
        src/Utils/GlassHelper/GlassHelper.h
        src/Utils/SupportFunctions.h
        src/VERSION.h)

if (WIN32)
    set_target_properties(ClassTopLand-Next PROPERTIES
            WIN32_EXECUTABLE "$<CONFIG:Release>")
    enable_language(RC)
    target_sources(ClassTopLand-Next PRIVATE app.rc)
    target_include_directories(ClassTopLand-Next PRIVATE
            "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}>")
    set_source_files_properties(app.rc PROPERTIES OBJECT_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico")
    target_compile_definitions(ClassTopLand-Next PRIVATE
            "$<$<AND:$<COMPILE_LANGUAGE:RC>,$<CONFIG:Debug>>:_DEBUG>")
endif ()

target_link_libraries(ClassTopLand-Next
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Ela::WidgetTools
        yaml-cpp::yaml-cpp
)

option(CLASSTOPLAND_BUILD_EXCHANGE_TESTS "Build profile format exchange tests" OFF)
if (CLASSTOPLAND_BUILD_EXCHANGE_TESTS)
    enable_testing()
    add_executable(profile_exchange_tests
            tests/ProfileExchangeTests.cpp
            src/Core/DateSchedule/DateSchedule.cpp
            src/Core/ProfileManager/ProfileExchange.cpp)
    set_target_properties(profile_exchange_tests PROPERTIES
            AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_link_libraries(profile_exchange_tests PRIVATE Qt6::Core yaml-cpp::yaml-cpp)
    add_test(NAME profile_exchange COMMAND profile_exchange_tests)
    if (WIN32)
        set_tests_properties(profile_exchange PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endif ()
endif ()

option(CLASSTOPLAND_BUILD_DATE_SCHEDULE_TESTS "Build date schedule adjustment tests" OFF)
if (CLASSTOPLAND_BUILD_DATE_SCHEDULE_TESTS)
    enable_testing()
    add_executable(date_schedule_tests
            tests/DateScheduleTests.cpp
            src/Core/DateSchedule/DateSchedule.cpp
            src/Core/ProfileManager/ProfileManager.cpp
            src/Core/ProfileManager/ProfileExchange.cpp
            src/Core/CourseRefreshService/CourseRefreshService.cpp
            src/ProfileEditWindow/ProfileEditSession.cpp)
    target_link_libraries(date_schedule_tests PRIVATE Qt6::Core yaml-cpp::yaml-cpp)
    add_test(NAME date_schedule COMMAND date_schedule_tests)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    get_target_property(date_ui_sources ClassTopLand-Next SOURCES)
    list(REMOVE_ITEM date_ui_sources main.cpp app.rc)
    add_executable(date_schedule_ui_tests tests/DateScheduleUiTests.cpp ${date_ui_sources})
    target_link_libraries(date_schedule_ui_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
            Ela::WidgetTools yaml-cpp::yaml-cpp)
    add_test(NAME date_schedule_ui COMMAND date_schedule_ui_tests)
    set_tests_properties(date_schedule_ui PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            TIMEOUT 30)
    add_test(NAME date_schedule_ui_scaled COMMAND date_schedule_ui_tests)
    set_tests_properties(date_schedule_ui_scaled PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;QT_SCALE_FACTOR=1.5"
            TIMEOUT 30)
    if (WIN32)
        set_tests_properties(date_schedule PROPERTIES
                ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
        set_tests_properties(date_schedule_ui PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>")
        set_tests_properties(date_schedule_ui_scaled PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<TARGET_FILE_DIR:ElaWidgetTools>")
    endif ()
endif ()

if (WIN32 AND NOT ELAWIDGETTOOLS_BUILD_STATIC_LIB)
    add_custom_command(TARGET ClassTopLand-Next POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ElaWidgetTools>"
            "$<TARGET_FILE_DIR:ClassTopLand-Next>"
            VERBATIM)
endif ()

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    foreach (QT_LIB Core Gui Widgets)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()
