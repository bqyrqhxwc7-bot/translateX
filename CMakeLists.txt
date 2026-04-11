cmake_minimum_required(VERSION 3.21)

project(translateX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT DEFINED CMAKE_PREFIX_PATH AND EXISTS "C:/Qt/6.5.3/msvc2019_64")
    list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.5.3/msvc2019_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

add_executable(translateX
    src/main.cpp
    src/annotatedtexteditor.cpp
    src/annotatedtexteditor.h
    src/mainwindow.cpp
    src/mainwindow.h
    src/mainwindow.ui
)

target_link_libraries(translateX PRIVATE Qt6::Widgets Qt6::Concurrent Qt6::Network)

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(QT_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")

    if(QT_WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET translateX POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:translateX>"
            COMMENT "Deploying Qt runtime for translateX"
        )
    endif()

    install(TARGETS translateX
        RUNTIME DESTINATION .
    )

    if(QT_WINDEPLOYQT_EXECUTABLE)
        install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/"
            DESTINATION .
            FILES_MATCHING
            PATTERN "translateX.exe"
            PATTERN "*.dll")
    endif()
endif()

if(WIN32)
    if(DEFINED ENV{MAKENSIS} AND NOT "$ENV{MAKENSIS}" STREQUAL "")
        if(EXISTS "$ENV{MAKENSIS}")
            set(NSIS_EXECUTABLE "$ENV{MAKENSIS}")
        else()
            find_program(NSIS_EXECUTABLE makensis HINTS "$ENV{MAKENSIS}")
        endif()
    else()
        find_program(NSIS_EXECUTABLE makensis)
    endif()

    if(NSIS_EXECUTABLE)
        message(STATUS "Found NSIS executable: ${NSIS_EXECUTABLE}")
        set(CPACK_GENERATOR "NSIS;ZIP")
    else()
        message(WARNING "NSIS not found in PATH or MAKENSIS; package target will build ZIP only.")
        set(CPACK_GENERATOR "ZIP")
    endif()
else()
    set(CPACK_GENERATOR "ZIP")
endif()

set(CPACK_PACKAGE_NAME "translateX")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "translateX")
set(CPACK_NSIS_DISPLAY_NAME "translateX")
set(CPACK_NSIS_ENABLE_UNINSTALL ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
