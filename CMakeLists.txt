cmake_minimum_required(VERSION 3.21)

project(Translex VERSION 1.0.0 LANGUAGES CXX)

set(TRANSLEX_VERSION "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if(NOT DEFINED CMAKE_PREFIX_PATH)
    if(EXISTS "D:/Software/Qt/6.5.3/msvc2019_64")
        list(PREPEND CMAKE_PREFIX_PATH "D:/Software/Qt/6.5.3/msvc2019_64")
    elseif(EXISTS "C:/Qt/6.5.3/msvc2019_64")
        list(PREPEND CMAKE_PREFIX_PATH "C:/Qt/6.5.3/msvc2019_64")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network Qml Quick QuickWidgets Svg ShaderTools PrintSupport Pdf)

# FluentUI (BSD-3-Clause) : Microsoft Fluent Design component library for Qt QML ----
# 全量迁移到 QML：FluentUI 静态库作为子目录集成（third_party/FluentUI）。
set(FLUENTUI_BUILD_EXAMPLES OFF CACHE BOOL "Build FluentUI demo applications." FORCE)
set(FLUENTUI_BUILD_STATIC_LIB ON CACHE BOOL "Build FluentUI as a static library." FORCE)
add_subdirectory(third_party/FluentUI)

# ---- zlib (zlib license) : QuaZip 依赖，静态编译（docx 导入） ----
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "Build zlib tests" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "Install zlib" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "Skip zlib install rules" FORCE)
add_subdirectory(third_party/zlib)

# ---- QuaZip (BSD-3-Clause) : docx 导入 zip 解压，静态编译 ----
# QuaZip 的 find_package(ZLIB) 需要 ZLIB_LIBRARY/ZLIB_INCLUDE_DIR 非空；实际链接
# 用 ZLIB::ZLIB target（zlib 子目录已创建 alias，FindZLIB 不会覆盖已存在 target）。
set(ZLIB_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/zlib" CACHE PATH "zlib include dir" FORCE)
set(ZLIB_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/third_party/zlib/zlibstatic.lib" CACHE FILEPATH "zlib library (QuaZip links ZLIB::ZLIB target)" FORCE)
set(QUAZIP_INSTALL OFF CACHE BOOL "Install QuaZip" FORCE)
set(QUAZIP_ENABLE_TESTS OFF CACHE BOOL "Build QuaZip tests" FORCE)
set(QUAZIP_BZIP2 OFF CACHE BOOL "Enable BZIP2 compression" FORCE)
add_subdirectory(third_party/quazip)

# QML 版版本号（Widgets 版已迁移至 widgets 分支）
set(TRANSLEX_QML_VERSION "1.1.0-qml")

# ---- QML 骨架（FluentUI 全量迁移目标）----
qt_add_executable(translex
    src/main_qml.cpp
    src/services/appguard.cpp
    src/services/appguard.h
    src/services/documentmodel.cpp
    src/services/documentmodel.h
    src/services/securestorage.cpp
    src/services/securestorage.h
    src/services/itranslationbackend.h
    src/services/serviceregistry.cpp
    src/services/serviceregistry.h
    src/services/translationbackend.cpp
    src/services/translationbackend.h
    src/services/translationcache.cpp
    src/services/translationcache.h
    src/services/translationservice.cpp
    src/services/translationservice.h
    src/services/termglossary.cpp
    src/services/termglossary.h
    src/services/qualitygate.cpp
    src/services/qualitygate.h
    src/services/configservice.cpp
    src/services/configservice.h
    src/services/config/config.qrc
    src/services/commentservice.cpp
    src/services/commentservice.h
    src/services/documentmanager.cpp
    src/services/documentmanager.h
    src/services/chapterservice.cpp
    src/services/chapterservice.h
    src/services/findservice.cpp
    src/services/findservice.h
    src/services/trxparser.cpp
    src/services/trxparser.h
    src/services/docxparser.cpp
    src/services/docxparser.h
    src/services/pdfparser.cpp
    src/services/pdfparser.h
)

qt_add_qml_module(translex
    URI Translex
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES
        qml/Main.qml
        qml/DesignTokens.qml
        qml/TranslateHomePage.qml
        qml/TranslateSettingsPage.qml
        qml/ConfigSectionCard.qml
        qml/TranslatePanelContent.qml
)

target_link_libraries(translex PRIVATE
    Qt6::Quick Qt6::Qml Qt6::Widgets Qt6::Svg Qt6::ShaderTools Qt6::PrintSupport Qt6::Network Qt6::Concurrent Qt6::Pdf
    fluentuiplugin
    QuaZip::QuaZip
)
target_compile_definitions(translex PRIVATE
    FLUENTUI_BUILD_STATIC_LIB
    TRANSLEX_VERSION="${TRANSLEX_QML_VERSION}"
)
target_include_directories(translex PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FluentUI/src")

# FluentUI 生成大量 QML cache 源文件，MSVC 并发编译共享 PDB 会触发 C1041；
# 改用 /Z7 将调试信息嵌入 .obj，避免共享 PDB 竞争。
if(MSVC AND TARGET fluentui)
    target_compile_options(fluentui PRIVATE /Z7)
endif()

if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(QT_WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")

    if(QT_WINDEPLOYQT_EXECUTABLE)
        # windeployqt 找不到 VS 会报 "Cannot find Visual Studio installation
        # directory, VCINSTALLDIR is not set"（无害但污染构建输出）。用 vswhere
        # 探测 VS 安装路径，设置 VCINSTALLDIR 消除该提示；探测失败则按原样运行。
        if(NOT DEFINED VCINSTALLDIR)
            find_program(WSWHERE_EXE vswhere
                HINTS "C:/Program Files (x86)/Microsoft Visual Studio/Installer")
            if(WSWHERE_EXE)
                execute_process(COMMAND "${WSWHERE_EXE}" -latest -property installationPath
                                OUTPUT_VARIABLE VS_INSTALL_DIR
                                OUTPUT_STRIP_TRAILING_WHITESPACE)
                if(VS_INSTALL_DIR AND EXISTS "${VS_INSTALL_DIR}/VC")
                    set(VCINSTALLDIR "${VS_INSTALL_DIR}/VC/")
                endif()
            endif()
        endif()

        get_filename_component(QT_QML_SRC_DIR "${QT_BIN_DIR}/../qml" ABSOLUTE)
        if(VCINSTALLDIR)
            add_custom_command(TARGET translex POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E env "VCINSTALLDIR=${VCINSTALLDIR}"
                        "${QT_WINDEPLOYQT_EXECUTABLE}" --no-translations --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:translex>"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${QT_QML_SRC_DIR}" "$<TARGET_FILE_DIR:translex>/qml"
                COMMENT "Deploying Qt runtime (incl. all QML modules) for translex"
            )
        else()
            add_custom_command(TARGET translex POST_BUILD
                COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}" --no-translations --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/qml" "$<TARGET_FILE:translex>"
                COMMAND "${CMAKE_COMMAND}" -E copy_directory "${QT_QML_SRC_DIR}" "$<TARGET_FILE_DIR:translex>/qml"
                COMMENT "Deploying Qt runtime (incl. all QML modules) for translex"
            )
        endif()
    endif()

    install(TARGETS translex
        RUNTIME DESTINATION .
    )

    if(QT_WINDEPLOYQT_EXECUTABLE)
        install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/"
            DESTINATION .
            FILES_MATCHING
            PATTERN "translex.exe"
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

# ---- 测试（默认开启；发布构建可用 -DBUILD_TESTING=OFF 关闭）----
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CPACK_PACKAGE_NAME "Translex")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Translex")
set(CPACK_NSIS_DISPLAY_NAME "Translex")
set(CPACK_NSIS_ENABLE_UNINSTALL ON)
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
