# 服务单元测试与性能基准
# 通过 `ctest` 运行：cmake --build ... --target test 或 ctest

find_package(Qt6 REQUIRED COMPONENTS Test)

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
)

# 共享服务抽为静态库：11 个测试目标只需编译一次服务源码（避免约 130 次重复编译）。
# qrc 资源在静态库中需 Q_INIT_RESOURCE 显式初始化（见 configservice.cpp）。
qt_add_library(translateX_services STATIC
    ${SERVICE_SOURCES}
)
target_include_directories(translateX_services PUBLIC ${TEST_INCLUDE_DIRS})
target_link_libraries(translateX_services PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Gui)

# ---- 服务健康度测试：DocumentModel ----
qt_add_executable(tst_documentmodel tst_documentmodel.cpp)
target_link_libraries(tst_documentmodel PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_documentmodel COMMAND tst_documentmodel)

# ---- 安全存储测试 ----
qt_add_executable(tst_securestorage tst_securestorage.cpp)
target_link_libraries(tst_securestorage PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_securestorage COMMAND tst_securestorage)

# ---- 性能基准（大文件） ----
qt_add_executable(tst_performance tst_performance.cpp)
target_link_libraries(tst_performance PRIVATE translateX_services Qt6::Test)
# 性能测试默认标签；完整回归可用 ctest -L perf
add_test(NAME tst_performance COMMAND tst_performance)
set_tests_properties(tst_performance PROPERTIES LABELS "perf")

# ---- 翻译服务测试（注册表/缓存/门面）----
qt_add_executable(tst_translation tst_translation.cpp)
target_link_libraries(tst_translation PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_translation COMMAND tst_translation)

# ---- 质量与成本增强测试（术语表/质量门/磁盘缓存/智能分块）----
qt_add_executable(tst_quality tst_quality.cpp)
target_link_libraries(tst_quality PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_quality COMMAND tst_quality)

# ---- 配置服务测试（VSCode-like 声明/读写/持久化/加密）----
qt_add_executable(tst_configservice tst_configservice.cpp)
target_link_libraries(tst_configservice PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_configservice COMMAND tst_configservice)

# ---- 批注服务测试（CommentService + DocumentModel provider 集成）----
qt_add_executable(tst_comment tst_comment.cpp)
target_link_libraries(tst_comment PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_comment COMMAND tst_comment)

# ---- 文档管理服务测试 ----
qt_add_executable(tst_documentmanager tst_documentmanager.cpp)
target_link_libraries(tst_documentmanager PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_documentmanager COMMAND tst_documentmanager)

# ---- 章节服务测试 ----
qt_add_executable(tst_chapter tst_chapter.cpp)
target_link_libraries(tst_chapter PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_chapter COMMAND tst_chapter)

# ---- 查找替换服务测试 ----
qt_add_executable(tst_find tst_find.cpp)
target_link_libraries(tst_find PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_find COMMAND tst_find)

# ---- .trx 格式解析/往返测试（TrxParser + DocumentManager 集成）----
qt_add_executable(tst_trx tst_trx.cpp)
target_link_libraries(tst_trx PRIVATE translateX_services Qt6::Test)
add_test(NAME tst_trx COMMAND tst_trx)
