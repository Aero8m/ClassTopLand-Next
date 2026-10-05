cmake_minimum_required(VERSION 4.0)
project(ClassTopLand_Next)

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

add_subdirectory(third_party/ElaWidgetTools-Plus/ElaWidgetTools)

add_executable(ClassTopLand_Next main.cpp
        resourecs.qrc
        src/Core/Model/Subject.h
        src/Core/Model/Schedule.h
        src/Core/Model/Class.h
        src/Core/Model/TimeLine.h
        src/Utils/SupportFunctions.h
        src/Core/Model/Profile.h
        src/Core/ProfileManager/ProfileManager.h
        src/Core/ProfileManager/ProfileManager.cpp
        src/Core/Logger/Logger.h
        src/Core/Logger/Logger.cpp
        src/Core/AppComposer/AppComposer.cpp
        src/Core/AppComposer/AppComposer.h
        src/Core/Model/Config.h
        src/Core/ConfigManager/ConfigManager.h
        src/Core/ConfigManager/ConfigManager.cpp
        src/Core/CourseRefreshService/CourseRefreshService.cpp
        src/Core/CourseRefreshService/CourseRefreshService.h
        src/Utils/GlassHelper/GlassHelper.cpp
        src/Core/Model/CourseBarConfig.h
        src/CourseBar/CourseBar.cpp
        src/CourseBar/CourseBar.h
        src/CourseBar/ICourseBarComponent.h
        src/CourseBar/BuiltinComponents/Date/Date.cpp
        src/CourseBar/BuiltinComponents/Date/Date.h
        src/CourseBar/BuiltinComponents/CourseView/CourseView.cpp
        src/CourseBar/BuiltinComponents/CourseView/CourseView.h
        src/TaskbarTrayMenu/TaskbarTrayMenu.cpp
        src/TaskbarTrayMenu/TaskbarTrayMenu.h
        src/SettingsWindow/SettingsWindow.cpp
        src/SettingsWindow/SettingsWindow.h
        src/ProfileEditWindow/ProfileEditWindow.cpp
        src/ProfileEditWindow/ProfileEditWindow.h)
set(PROFILE_EDITOR_SOURCES
        src/ProfileEditWindow/ProfileEditSession.h
        src/ProfileEditWindow/ProfileEditSession.cpp
        src/ProfileEditWindow/ProfileEditWidgets.h
        src/ProfileEditWindow/ProfileEditWidgets.cpp
        src/ProfileEditWindow/widgets/TimeLineCanvas.h
        src/ProfileEditWindow/widgets/TimeLineCanvas.cpp
        src/ProfileEditWindow/tabs/ProfileInfoTab.h
        src/ProfileEditWindow/tabs/ProfileInfoTab.cpp
        src/ProfileEditWindow/tabs/SubjectsTab.h
        src/ProfileEditWindow/tabs/SubjectsTab.cpp
        src/ProfileEditWindow/tabs/TimeLinesTab.h
        src/ProfileEditWindow/tabs/TimeLinesTab.cpp
        src/ProfileEditWindow/tabs/SchedulesTab.h
        src/ProfileEditWindow/tabs/SchedulesTab.cpp)
target_sources(ClassTopLand_Next PRIVATE ${PROFILE_EDITOR_SOURCES})
target_link_libraries(ClassTopLand_Next
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Ela::WidgetTools
)

if (WIN32 AND NOT ELAWIDGETTOOLS_BUILD_STATIC_LIB)
    add_custom_command(TARGET ClassTopLand_Next POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:ElaWidgetTools>"
            "$<TARGET_FILE_DIR:ClassTopLand_Next>"
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


include(CTest)
if (BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    get_target_property(SCHEDULE_TEST_SOURCES ClassTopLand_Next SOURCES)
    list(REMOVE_ITEM SCHEDULE_TEST_SOURCES main.cpp)
    add_executable(ScheduleSwitchTests tests/ScheduleSwitchTests.cpp ${SCHEDULE_TEST_SOURCES})
    target_include_directories(ScheduleSwitchTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ScheduleSwitchTests PRIVATE Qt::Test Qt::Widgets Ela::WidgetTools)
    if (WIN32 AND NOT ELAWIDGETTOOLS_BUILD_STATIC_LIB)
        add_custom_command(TARGET ScheduleSwitchTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:ElaWidgetTools>" "$<TARGET_FILE_DIR:ScheduleSwitchTests>"
                VERBATIM)
    endif ()
    add_test(NAME ScheduleSwitchTests COMMAND ScheduleSwitchTests)
    set_tests_properties(ScheduleSwitchTests PROPERTIES
            TIMEOUT 90
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=C:/Qt/6.9.3/msvc2022_64/plugins/platforms"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:C:/Qt/6.9.3/msvc2022_64/bin")
endif ()
