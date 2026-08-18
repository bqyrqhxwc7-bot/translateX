cmake_minimum_required(VERSION 3.21)

# 示例插件（迭代5 插件化 A3）：验证插件全链路——
# 编译为独立 DLL → 部署到 <translex_exe_dir>/plugins/ → 应用启动扫描加载。
# 第三方插件参考本目录结构（见 docs/services/plugin-development.md）。
project(example_translation_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

qt_add_library(example_translation_plugin SHARED
    exampleplugin.cpp
    exampleplugin.h
)

target_link_libraries(example_translation_plugin PRIVATE Translex_sdk)

# 部署：仅 MSVC 主构建（clang-cl/MinGW 用于编译验证，跳过部署）
if(WIN32 AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT MINGW)
    add_custom_command(TARGET example_translation_plugin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:translex>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:example_translation_plugin>"
                "$<TARGET_FILE_DIR:translex>/plugins"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/qml/ExamplePanel.qml"
                "$<TARGET_FILE_DIR:translex>/plugins"
        COMMENT "Deploying example plugin to translex plugins dir"
    )
endif()