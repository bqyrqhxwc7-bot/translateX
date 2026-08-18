# 服务单元测试与性能基准
# 通过 `ctest` 运行：cmake --build ... --target test 或 ctest

find_package(Qt6 REQUIRED COMPONENTS Test Pdf)

set(TEST_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/services"
)

# 共享的服务源（被测代码）
set(SERVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/documentmodel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/securestorage.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/serviceregistry.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/translationbackend.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/translationcache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/translationservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/termglossary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/qualitygate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/configservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/config/config.qrc
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/commentservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/documentmanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/chapterservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/findservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/trxparser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/docxparser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/pdfparser.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/texttospeechservice.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/services/translationhistoryservice.cpp
)

# 共享服务抽为静态库：11 个测试目标只需编译一次服务源码（避免约 130 次重复编译）。
# qrc 资源在静态库中需 Q_INIT_RESOURCE 显式初始化（见 configservice.cpp）。
qt_add_library(translex_services STATIC
    ${SERVICE_SOURCES}
)
target_include_directories(translex_services PUBLIC ${TEST_INCLUDE_DIRS})
target_link_libraries(translex_services PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui Qt6::Pdf QuaZip::QuaZip)
if(TARGET Qt6::TextToSpeech)
    target_link_libraries(translex_services PUBLIC Qt6::TextToSpeech)
    target_compile_definitions(translex_services PUBLIC TRANSLEX_HAS_TTS=1)
endif()

# ---- 测试自包含：把 Qt 调试 DLL + 平台/图片插件拷到测试输出目录 ----
# 否则测试 exe 在无 Qt bin PATH 的环境运行会弹「找不到 Qt6Xxxd.dll」系统错误框
#（火绒挂起排查时也遇到过）。拷贝目录 = 测试 exe 所在目录。
if(WIN32)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_TEST_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    get_filename_component(QT_TEST_PLUGIN_DIR "${QT_TEST_BIN_DIR}/../plugins" ABSOLUTE)
    # 调试版 Qt DLL 带 d 后缀（Qt6Cored.dll），Release 无；用生成器表达式自适应
    set(QT_DBG_SUFFIX "$<$<CONFIG:Debug>:d>")
    set(QT_TEST_RUNTIME_DLLS
        "${QT_TEST_BIN_DIR}/Qt6Core${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Gui${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Network${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Concurrent${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Pdf${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Test${QT_DBG_SUFFIX}.dll"
        "${QT_TEST_BIN_DIR}/Qt6Core5Compat${QT_DBG_SUFFIX}.dll"
    )
    if(TARGET Qt6::TextToSpeech)
        list(APPEND QT_TEST_RUNTIME_DLLS
            "${QT_TEST_BIN_DIR}/Qt6TextToSpeech${QT_DBG_SUFFIX}.dll")
    endif()
    add_custom_command(TARGET translex_services POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${QT_TEST_RUNTIME_DLLS}
                "$<TARGET_FILE_DIR:translex_services>"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_TEST_PLUGIN_DIR}/platforms" "$<TARGET_FILE_DIR:translex_services>/platforms"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_TEST_PLUGIN_DIR}/imageformats" "$<TARGET_FILE_DIR:translex_services>/imageformats"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${QT_TEST_PLUGIN_DIR}/tls" "$<TARGET_FILE_DIR:translex_services>/tls"
        COMMENT "Copying Qt debug runtime for self-contained tests"
    )
endif()

# ---- 服务健康度测试：DocumentModel ----
qt_add_executable(tst_documentmodel tst_documentmodel.cpp)
target_link_libraries(tst_documentmodel PRIVATE translex_services Qt6::Test)
add_test(NAME tst_documentmodel COMMAND tst_documentmodel)

# ---- 安全存储测试 ----
qt_add_executable(tst_securestorage tst_securestorage.cpp)
target_link_libraries(tst_securestorage PRIVATE translex_services Qt6::Test)
add_test(NAME tst_securestorage COMMAND tst_securestorage)

# ---- 性能基准（大文件） ----
qt_add_executable(tst_performance tst_performance.cpp)
target_link_libraries(tst_performance PRIVATE translex_services Qt6::Test)
# 性能测试默认标签；完整回归可用 ctest -L perf
add_test(NAME tst_performance COMMAND tst_performance)
set_tests_properties(tst_performance PROPERTIES LABELS "perf")

# ---- 翻译服务测试（注册表/缓存/门面）----
qt_add_executable(tst_translation tst_translation.cpp)
target_link_libraries(tst_translation PRIVATE translex_services Qt6::Test)
add_test(NAME tst_translation COMMAND tst_translation)

# ---- 质量与成本增强测试（术语表/质量门/磁盘缓存/智能分块）----
qt_add_executable(tst_quality tst_quality.cpp)
target_link_libraries(tst_quality PRIVATE translex_services Qt6::Test)
add_test(NAME tst_quality COMMAND tst_quality)

# ---- 配置服务测试（VSCode-like 声明/读写/持久化/加密）----
qt_add_executable(tst_configservice tst_configservice.cpp)
target_link_libraries(tst_configservice PRIVATE translex_services Qt6::Test)
add_test(NAME tst_configservice COMMAND tst_configservice)

# ---- 批注服务测试（CommentService + DocumentModel provider 集成）----
qt_add_executable(tst_comment tst_comment.cpp)
target_link_libraries(tst_comment PRIVATE translex_services Qt6::Test)
add_test(NAME tst_comment COMMAND tst_comment)

# ---- 文档管理服务测试 ----
qt_add_executable(tst_documentmanager tst_documentmanager.cpp)
target_link_libraries(tst_documentmanager PRIVATE translex_services Qt6::Test)
add_test(NAME tst_documentmanager COMMAND tst_documentmanager)

# ---- 章节服务测试 ----
qt_add_executable(tst_chapter tst_chapter.cpp)
target_link_libraries(tst_chapter PRIVATE translex_services Qt6::Test)
add_test(NAME tst_chapter COMMAND tst_chapter)

# ---- 查找替换服务测试 ----
qt_add_executable(tst_find tst_find.cpp)
target_link_libraries(tst_find PRIVATE translex_services Qt6::Test)
add_test(NAME tst_find COMMAND tst_find)

# ---- .trx 格式解析/往返测试（TrxParser + DocumentManager 集成）----
qt_add_executable(tst_trx tst_trx.cpp)
target_link_libraries(tst_trx PRIVATE translex_services Qt6::Test)
add_test(NAME tst_trx COMMAND tst_trx)

# ---- .docx 导入测试（DocxParser：残缺导入）----
qt_add_executable(tst_docx tst_docx.cpp)
target_link_libraries(tst_docx PRIVATE translex_services Qt6::Test)
add_test(NAME tst_docx COMMAND tst_docx)

# ---- .pdf 导入/导出测试（PdfParser：QPdfDocument/QPdfWriter）----
qt_add_executable(tst_pdf tst_pdf.cpp)
target_link_libraries(tst_pdf PRIVATE translex_services Qt6::Test)
target_compile_definitions(tst_pdf PRIVATE SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME tst_pdf COMMAND tst_pdf)

# ---- TTS 朗读服务测试（降级路径 + 配置往返；无引擎环境同样通过）----
qt_add_executable(tst_texttospeech tst_texttospeech.cpp)
target_link_libraries(tst_texttospeech PRIVATE translex_services Qt6::Test)
add_test(NAME tst_texttospeech COMMAND tst_texttospeech)

# ---- 翻译历史服务测试（迭代4b：记录/顺序/上限/清空）----
qt_add_executable(tst_history tst_history.cpp)
target_link_libraries(tst_history PRIVATE translex_services Qt6::Test)
add_test(NAME tst_history COMMAND tst_history)
