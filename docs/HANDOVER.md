# Translex 交接与指挥蓝图

> 生成：2026-08-15 · 用途：**opencode / 后续开发者接手时的总纲**
> 接手顺序：先读本文件 → 再读 [`AGENTS.md`](../AGENTS.md)（协作规范）→ [`ARCHITECTURE.md`](ARCHITECTURE.md)（架构）
> 本文件由 Copilot 整理，是当前工程状态、路线图与踩坑的**唯一权威汇总**；实现细节一律以 `docs/services/`、`docs/ui/` 下文档为准。

---

## 0. 项目一句话

Qt 6 桌面**翻译写作工具**：带批注/翻译对照的编辑器，支持 Ollama / 云端 / 网络大模型三后端，翻译结果写为批注，支持行/选区/整篇/未批注章节批量翻译；`.txt` / `.trx` / `.docx` 三格式。

## 1. 技术栈与构建（每次接手必看）

| 项 | 值 |
| --- | --- |
| Qt | **6.11.1**，`D:/Software/Qt/6.11.1/msvc2022_64`（2026-08-18 自 6.5.3 升级；旧版已卸载） |
| C++ | C++17；CMake 4.2.1；生成器 **VS 2026 x64**（`build-vs2026-x64/`） |
| UI | QML + **FluentUI 1.7.7**（子模块 `third_party/FluentUI`） |
| 第三方 | **QuaZip 1.7.2** + **zlib 1.3.2**（子模块，docx 导入，均**静态**编译） |
| 测试 | 16 个目标，共享服务抽为 `translex_services` 静态库 |

**关键命令**（Windows PowerShell，工作目录=仓库根）：

```powershell
# 构建（应用）
cmake --build build-vs2026-x64 --config Debug

# 重新配置（改 CMakeLists/子模块后；BUILD_TESTING 可能被缓存成 OFF，务必显式 ON）
cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.11.1/msvc2022_64" -DBUILD_TESTING=ON

# 测试（必须先加 Qt bin 到 PATH，否则 0xc0000135）
$env:PATH = "D:/Software/Qt/6.11.1/msvc2022_64/bin;" + $env:PATH
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 单测
ctest --test-dir build-vs2026-x64 -C Debug -R tst_docx --output-on-failure

# 应用 exe 部署（升级 Qt / 换版本后必须 --force，否则旧 DLL 残留导致启动失败）
D:\Software\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --debug --force --qmldir qml build-vs2026-x64\Debug\translex.exe

# 推送（网络不稳，失败重试）
git push origin main
```

**注意**：测试 exe 在 `build-vs2026-x64/tests/Debug/`（非 `Debug/`），运行前 PATH 必须含 Qt bin。

## 2. 功能状态（全部 ✅ 已实现并推送）

| 功能 | 说明 | 文档 |
| --- | --- | --- |
| 编辑器 | 虚拟化 ListView + 懒加载模型（50 万行 <100ms）、Enter 拆行、Backspace 合并、撤销/重做 | `services/documentmodel.md` |
| 批注 | 单一数据源（CommentService）、行内直编、字号独立、跳转/清空/JSON 导入导出 | `services/comment-service.md` |
| 翻译 | 三后端 + 上下文感知 + 回显拦截 + 质量自检 + 缓存(L1内存/L2磁盘) + 智能分块 + 失败降级 | `services/translation-service.md` |
| 章节 | 「第X章」/ Markdown `#` 识别、上/下一章 | `services/chapter-service.md` |
| 查找 | 大小写/整词/**模糊**（子序列）、替换 | `services/find-service.md` |
| 浮窗 | 真独立 Window（Qt.Tool+Frameless）、位置记忆、启动显示 | `ui/translate-panel.md` |
| 设置页 | schema 驱动（`ui.json`），字号滑条、查找开关、浮窗开关 | `services/config-service.md` |
| TTS 朗读 | **迭代3**：独立 TextToSpeechService（朗读选中行/选区、语速、停止；跨平台 Qt6TextToSpeech，无模块/引擎优雅降级） | `services/text-to-speech.md` |
| 文档统计 | **迭代4**：`DocumentModel::stats()`（行/非空行/字/词/批注/富文本/图片行），状态栏「共 N 行 · M 字 · K 条批注」 | `services/iteration4-stats-autosave-glossary.md` |
| 自动保存 | **迭代4**：dirty 时每 60s 写 `%LOCALAPPDATA%/sr291/Translex/autosave/<名>-<路径哈希>.autosave.trx`（完整往返含批注）；正常保存/打开/新建后清理；启动检测崩溃残留（只弹一次）→ 恢复/丢弃弹窗（恢复还原原始路径）；受限模式与 `ui.autosaveEnabled` 关闭时跳过 | `services/iteration4-stats-autosave-glossary.md` |
| 术语自动提取 | **迭代4**：`TermGlossary::extractCandidates`（英文高频词 ≥3 次、停用词/已有术语过滤、频率降序）；设置页「从文档提取」弹窗勾选加入 | `services/iteration4-stats-autosave-glossary.md` |
| 翻译历史 | **迭代4b**：`TranslationHistoryService`（内存环形缓冲 500 条，会话级不落盘）；主页「翻译历史」弹窗（行号/原文/译文/时间/成败，点击跳转行、清空）；QML 在 onLineTranslated 里 record | `services/iteration4b-history-markdown-guide.md` |
| Markdown 导出 | **迭代4b**：另存为加 `*.md` filter，`writeDocument` md 分支（`# 标题` + 每行 `## 第 N 行` + 原文 + `> 译文` 引用块，空行跳过；单向导出） | `services/iteration4b-history-markdown-guide.md` |
| 功能引导 | **迭代4b**：不默认首启弹窗；设置页「帮助与引导」卡片 + 按钮 → 静态功能清单弹窗 | `services/iteration4b-history-markdown-guide.md` |
| **A3 .trx** | 显示层（富文本/图片）完整往返、编辑即降级 | `services/file-service.md` |
| **B docx 导入** | DocxParser：段落→行 + 粗/斜/颜色/字号/字体 + 图片(data URI) | `services/file-service.md` |
| **B2 docx 导出批注** | DocxParser::write：原文 + 译文批注，`docxCommentStyle: inline`（黄色高亮）/`native`（Word 原生批注，可读回） | `services/docx-comment-export.md` |
| **C pdf 导入/导出** | PdfParser：每页一行导入（QPdfDocument）+ 文本页导出（QPdfWriter）；**导出文本层可提取**（v2 自建 ToUnicode CMap 修复 Qt 6.5.3 缺陷，2026-08-16） | `services/pdf-service.md` |
| **D 大文件降级** | 超 5 万行 / 200MB 进受限模式：显示层回退纯文本 + 禁批注编辑/翻译，编辑/查找/章节保留；顶部提示条 | `services/large-file.md` |
| **迭代5 插件化** | `IService` 健康度接口（serviceId/displayName/healthCheck/sidebarPanel）+ `ServiceRegistry` 单例注册/查询；L3 动态插件（`QPluginLoader` 扫 `<exe>/plugins/*.dll`，示例插件回显后端 `translation.echo` + 面板） | `services/iteration5-plugin-ui-agent.md`、`services/plugin-development.md` |
| **迭代5 Outlook UI** | 主窗口重构：44px 图标栏（可收起）+ service 侧边栏面板（SplitView 180-400px）+ 内容区 NoStack 切换；`IconBarButton`、`panels/`（Chapter/Comment/History）；设置页新增服务调试卡片 | `docs/ARCHITECTURE.md` |
| **迭代5 窗口修复** | 窗口按钮失效修复（appBar 悬浮层 z:1 补丁，见 §7）；浮窗跟随主窗口（最小化隐藏/恢复显示、关闭放行防进程残留） | `services/iteration5-plugin-ui-agent.md` |

**测试**：16 目标全绿（MSVC + clang-cl 双编译器，含 perf 基准；`tst_registry` 插件注册表用例；`tst_docx` 10 用例：7 导入 + 3 导出（writeInline/writeNative/roundTrip，批注=译文往返保真）；`tst_pdf` 9 用例；`tst_documentmanager` 含 4 个受限模式 + 6 个自动保存 + markdownExport 用例；`tst_texttospeech` 4 用例（无 TTS 引擎环境 graceful 通过）；`tst_history` 5 用例（迭代4b）；`tst_documentmodel` 含 stats 用例；`tst_quality` 含 extractCandidates 用例）。

## 3. 路线图（下一步从这里开始）

| 优先级 | 任务 | 状态/要求 |
| --- | --- | --- |
| ~~迭代2~~ | ~~docx 导出批注（①译文内联高亮 ②Word 原生批注，做成选项 `docxCommentStyle: inline/native`）~~ | ✅ 完成（2026-08-17，见 `services/docx-comment-export.md`） |
| ~~迭代3~~ | ~~TTS 朗读（Qt6TextToSpeech 已装，Windows SAPI 系统语音）~~ | ✅ 完成（2026-08-17，见 `services/text-to-speech.md`；用户确认：选中行+选区朗读、语速+停止、语音跟随系统） |
| ~~迭代4（部分）~~ | ~~文档统计、自动保存、术语自动提取~~ | ✅ 完成（2026-08-17，见 `services/iteration4-stats-autosave-glossary.md`） |
| ~~迭代4（剩余）~~ | ~~翻译历史面板、Markdown 导出、功能引导（设置页按钮，不默认首启）~~ | ✅ 完成（2026-08-18，见 `services/iteration4b-history-markdown-guide.md`） |
| ~~迭代5~~ | ~~插件化（IService 健康度 + 注册表 + L3 动态插件 + 示例插件）、Outlook 式主窗口（图标栏/侧边栏面板/NoStack）、设置页调试卡片~~ | ✅ 完成（2026-08-18，见 `services/iteration5-plugin-ui-agent.md`；含窗口按钮修复 + 浮窗跟随修复） |
| 迭代6（规划） | **UI 底座迁移**：Qt 6.11 官方 `FluentWinUI3` QQC2 样式 + 自建轻量预制库 `translex-ui`（独立仓库 `C:/Users/sr291/Documents/projects/translex-ui`，submodule 引入）；页面从 FluentUI 1.7.7 逐步迁移（每步可构建可测试）；FluentUI 降级为过渡层，迁移完成移除 | 设计文档先行；FluentUI 在 Qt 6.11 下**实测兼容**（2026-08-18），迁移不紧迫、按节奏走 |
| 候选 | pdf 导出文本层复查（v2 CMap hack 在 Qt 6.11 下 tst_pdf 通过；"是否可删 hack"待单独验证）、.trx 图片 external 降级（>1MB 转外置）、docx 导出 `Original` 纯原文模式、更多示例插件（对照 plugin-development.md） | 非阻塞 |

> 迭代5（2026-08-18 完成）：插件化（`IService` 健康度接口 + `ServiceRegistry` + `QPluginLoader` 动态插件 + 示例回显插件 + `tst_registry`）、Outlook 式主窗口（`IconBarButton` + `panels/` 侧边栏面板 + NoStack 切换）、设置页调试卡片、窗口按钮失效修复（FluentUI `loader_app_bar z:1` 补丁）、浮窗跟随主窗口生命周期。

> 迭代2（2026-08-17 完成）：docx 导出批注（`DocxParser::write` + `docxCommentStyle` 配置 + tst_docx 3 个新用例）。
> 迭代3（2026-08-17 完成）：TTS 朗读（独立 `TextToSpeechService`，`TRANSLEX_HAS_TTS` 条件编译，无模块/引擎优雅降级；工具栏/右键菜单入口；tst_texttospeech 4 用例）。
> 迭代4 部分（2026-08-17 完成）：文档统计（`DocumentModel::stats` + 状态栏）、自动保存（60s tick + `.autosave.trx` + 崩溃恢复弹窗 + `ui.autosaveEnabled`）、术语自动提取（`TermGlossary::extractCandidates` + 设置页弹窗勾选；仅英文，中文暂不支持）。
> 迭代4b（2026-08-18 完成）：翻译历史（`TranslationHistoryService` 内存环形缓冲 500 条 + 主页弹窗）、Markdown 导出（`writeDocument` md 分支 + 另存为 filter）、功能引导（设置页按钮弹窗，不默认首启）。

> 迭代1（2026-08-17 完成）：P0 回归修复（backendCombo 残留引用）、A2 句边界分块（`sentenceAwareChunking`）、B1 后端连接测试（`testBackendConnection`）、B2 拖放打开、B3 快捷键总览（`?`）、A1 质量自检复核面板（qualityWarning 汇总+跳转）、设置页语言选择 Flow 换行修复。
> 每项任务实施蓝图见 §8；**开工前先读对应 `docs/services/` 文档，遵循 AGENTS.md**。

## 4. 架构铁律（违反必出回归 bug）

1. **NoStack 页面模式**：主窗口内容区每次切换**重建页面**（迭代5 起为 `Main.qml` 的 `contentLoader` Loader 切换，早前为 FluNavigationView `pageMode: NoStack`，机制已弃用、语义不变）。
   - 状态必须放**应用级**（`main_qml.cpp` 的 `setContextProperty` 单例），不能放页面属性
   - **Popup 控件按场景判定**：`FluMenu` 在主页 delegate 内错位/失效（已踩），但 `FluComboBox` 在**设置页卡片内实测可用**（语言选择即用它，2026-08-17 回退自 RadioButton 组）；结论：不可一票否决 Popup，新场景先小步实测，可用则用（按"已知可用姿势"记录）
   - **`Qt.callLater` 不可靠** → 一律用 `Timer`
2. **QML id 作用域**：子对象 `id` 不是父的属性。delegate 里直接引用顶层 `id`（如 `lineMenu`），**不能**写 `page.lineMenu`。
3. **DocumentModel 是应用级单例**：页面 `onCompleted` **仅当 `lineCount()==0`** 才 `loadDemoDocument()`，否则覆盖用户内容。
4. **显示层（rich/image）编辑即降级**：`onTextChanged` 无条件 `setLineRich("")` + `setLineImages([])` + `setLineDisplay("plain")`。
5. **新配置 key 必须**加 `src/services/config/` 下对应 schema JSON（`ui.json`/`translation.json`/`translation.ollama.json`/`translation.network_model.json`/`textToSpeech.json`；ConfigService 只认 schema 内 key，非 UI key 如 `docxCommentStyle` 在 translation.json）。
6. **行号显示用 `String(index + 1)`**，勿绑 `row.model.lineNumber`（插入后不刷新）。
7. 服务方法要能被 QML 调必须 `Q_INVOKABLE`（非虚或虚均可）；`documentModel` 等以 context property 暴露。
8. **浮窗（Qt.Tool + transientParent）**：启动时主窗口未稳定 → show 竞态失败，必须延迟（`floatShowTimer` 350ms）；`screen.virtualX` 未映射返回 undefined → `Number(scr && scr.virtualX) || 0` 防 NaN。
9. **appBar 悬浮层必须高于内容层**（FluentUI 本地补丁）：`FluWindow.qml` 的 `loader_app_bar` 保持 `z: 1`（> `layout_content` z=0）。`fitsAppBarWindows: true` 时内容从 y=0 起、后声明的 `layout_content` 会盖住 appBar → 窗口按钮点击被下层页面吞掉（仅最大化按钮因 Win11 原生 HTZOOM 特判可用，2026-08-18 踩过）。改 FluentUI 或窗口结构时核对。
10. **浮窗跟随主窗口生命周期**：主窗口最小化/关闭时浮窗（Qt.Tool 独立窗口）不会自动跟随——`TranslateHomePage.qml` 浮窗内必须保持 `minimized`/`appQuitting` 标志 + `Connections`（最小化隐藏/恢复显示；主窗口 onClosing 时 `appQuitting=true` + `floatWindow.close()` 放行），否则最小化后浮窗残留屏幕、关闭应用后进程不退出。

## 5. 文件地图

```
CMakeLists.txt             # 顶层：FluentUI + zlib + quazip + 主程序 + tests
src/main_qml.cpp           # 入口：应用级服务单例 setContextProperty
src/services/              # ★ 服务层（Q_INVOKABLE，QML 直接调）
  documentmodel / translationservice / translationbackend / commentservice /
  chapterservice / findservice / documentmanager / trxparser / docxparser /
  configservice / securestorage / termglossary / qualitygate / translationcache /
  serviceregistry / appguard
qml/TranslateHomePage.qml  # 核心 UI（约 2400 行：Ribbon/编辑器/右键菜单/浮窗/设置浮层/快捷键总览/质量复核面板）
qml/Main.qml               # 主窗口（迭代5 Outlook 布局：图标栏 + 侧边栏面板 + NoStack 内容区）
qml/IconBarButton.qml      # 图标栏按钮（tooltip/active 态）
qml/panels/                # service 侧边栏面板（Chapter/Comment/History，sidebarPanels() 动态注册）
qml/TranslateSettingsPage.qml / TranslatePanelContent.qml / ConfigSectionCard.qml / UiDriverActions.qml
tests/                     # 16 目标；CMakeLists 抽 translex_services 静态库
plugins/                   # 示例插件（example_translation_plugin：回显后端 + 面板）
samples/demo.trx           # .trx 示例（含富文本/图片显示层）
samples/demo.docx          # docx 示例（gen_docx.py 生成，纯 stdlib 可再生成）
docs/                      # 设计文档（services/ + ui/ + ARCHITECTURE.md + 本文件）
third_party/FluentUI       # 子模块（本地补丁，见 §7）
third_party/quazip,zlib    # 子模块（docx 依赖，静态；zlib 有本地补丁）
```

## 6. 踩坑清单（按子系统）

### QML / NoStack
- Popup 错位、FluMenu 按钮点不到、浮窗被页面边界裁剪 → 改独立 Window 或页面内覆盖层；**但 FluComboBox 在设置页卡片内可用**（2026-08-17 实测，语言选择已回退），不可一票否决
- `FluMenuItem: Created graphical object was not placed in the graphics scene` → 不要在不可见 Menu 内创建 item；菜单在 `onAboutToShow` 重建（`recentMenu` 已修复）
- 页面重建丢编辑（DocumentModel 曾页面内创建）→ 已提升应用级单例

### 浮窗
- 位置失效根因 = `screen.virtualX/Width` 未映射返回 undefined → NaN 污染钳制 → `Number()||fallback`
- 启动不显示 = Qt.Tool show 竞态 → 延迟 350ms + `onVisibleChanged` 时 `show()` 确认
- 位置保存：`onX/YChanged` 节流 350ms + `isFinite && >-10000` 守卫 + `restoringPos` 抑制恢复期保存
- **不跟随主窗口**：Qt.Tool 独立窗口在主窗口最小化/关闭时不自动跟随（WS_EX_TOOLWINDOW 非 WS_OWNED）→ 须手动（铁律 10，2026-08-18）

### 窗口按钮（appBar）
- **现象**：最小化/关闭/黑暗模式/固定全失效，仅最大化可用 → 根因是**内容层盖住 appBar**（`fitsAppBarWindows: true` 内容从 y=0 起，`layout_content` 在 `loader_app_bar` 后声明 → QML 命中测试把点击给下层页面）；最大化按钮走 Win11 `WM_NCHITTEST → HTZOOM` + sendEvent 原生路径所以幸免
- **诊断方法**：`containsCursorToItem` 临时加日志（写 `%TEMP%`/AppGuard 日志）确认 `WM_NCHITTEST` 返回 HTCLIENT（命中测试正常）→ 再查 QML 层命中（z 顺序）——**命中测试正常 ≠ 按钮可点**，两层要分开查
- **修复**：`loader_app_bar z:1`（铁律 9）；`fitsAppBarWindows: false` 会露底（窗口背景透明）不可用

### 富文本 / .trx / docx
- 编辑富文本行 → 必须降级纯文本（否则退出编辑不显示新内容）
- `.trx` 显示层（`display/rich/imageIds`）不参与 undo
- **docx `w:color/sz/rFonts` 属性带 `w:` 前缀**：`QXmlStreamAttributes::value("val")` 按 qualifiedName 匹配返回空 → 必须 `value(kWordNs, "val")`
- docx 图片：`w:drawing > a:blip r:embed`；rels 里 `Target` 相对 `word/` 需补前缀

### 翻译后端 / 配置
- **`ITranslationBackend::updateConfig` 默认空实现**：NetworkModelBackend 未重写 → 后端参数必须经 `TranslationOptions.extra` 传递（`apiEndpoint/apiKey/model`）。`testBackendConnection` 曾只 updateConfig → 永远"未配置网络大模型 API 地址"（2026-08-17 修复：探测时 `opts.extra = cfg`，与 `currentBackend()` 合并逻辑一致）
- `ConfigService::values()` 非 Q_INVOKABLE：QML 不可直接调用（UiDriverActions.getConfig 用 `sectionItems`+`get` 组合）
- 后端 section id（如 `translation.network_model`）即 ConfigService section 名，用户配置存 `%APPDATA%/sr291/Translex/config.ini`

### zlib / QuaZip（docx 依赖，静态）
- **zlib 默认构建 DLL（libzd.dll）** → 测试 0xc0000135。必须 `ZLIB_BUILD_SHARED=OFF` + `ZLIB_BUILD_STATIC=ON`
- zlib 静态模式下不自建 `ZLIB::ZLIB` alias → **本地补丁**：static 块内 `if(NOT ZLIB_BUILD_SHARED) add_library(ZLIB::ZLIB ALIAS zlibstatic)`
- zlib 静态库名是 `libzsd.lib`(Debug)/`libzs.lib`(Release)，**不是** `zlibstatic.lib`
- **`ZLIB_CONF_WRITTEN` 缓存变量**阻止 `zconf.h.cmakein` 重生成 → 删 zlib build 目录后须 `cmake -U ZLIB_CONF_WRITTEN`
- zlib `test/` + `contrib/` 的测试会**污染宿主 ctest** → 本地补丁注释 `add_subdirectory(test)` 与 `contrib`
- **`BUILD_TESTING` 可能被缓存成 OFF** → 重新配置务必显式 `-DBUILD_TESTING=ON`

### 其他
- **升级 Qt 后 exe 目录旧 DLL 残留（2026-08-18 踩过）**：`windeployqt` 默认**不覆盖**已存在文件——Qt 6.5.3→6.11.1 升级后 `Debug\` 里混装两版 Qt（Qt6Cored.dll 新版 + qml 模块旧版），启动失败且 AppGuard **无日志**（QML 引擎加载前即死），错误形如 `module "QtQuick.Controls.Windows" version 6.11 is not installed`（目录在但文件是旧版）。**修复：windeployqt 必须 `--force`**（部署命令见 §1）
- **QtPdf 组件不在默认安装里**：Qt 6.11.1 各套件均缺 QtPdf，需 Qt Maintenance Tool 手动添加（搜索 Qt PDF 组件）；CMakeLists 有硬检查（缺组件 FATAL_ERROR，不静默降级）
- 测试 exe 需 Qt bin 在 PATH（0xc0000135）
- 构建前停掉运行中的 `translex.exe`（LNK1168 文件占用）
- 构建日志/`reconfigure*.log` 等已 gitignore
- **QPdfWriter 文本层缺陷**（Qt 6.5.3，**已绕过**）：导出 PDF 提取乱码（ASCII 重复、CJK 变 ?），视觉正常；v2 已自建 ToUnicode CMap 重建修复（`PdfParser`，2026-08-16），文本可提取可再导入；**Qt 升级后复查**是否可删 hack 回归原生（详见 `pdf-service.md` §3.0/§6）
- **火绒安全会挂起新 exe**（行为分析以调试方式创建进程，症状：进程停在 DbgBreakPoint、cdb 附加被拒、`tst_docx`/`tst_pdf` 等含 ZIP/PDF 写入代码的新测试 exe 无法启动）→ 把项目目录加进火绒信任区（白名单）；若某次测试突然“卡死”，先怀疑它
- **QML pragma Singleton 绑定失效**（Qt 6.5，踩过）：`qml/DesignTokens.qml` 曾用 `pragma Singleton`，运行期全部属性 undefined（绑定从未求值，`NO_CACHEGEN` 也无效），报错形如 `Unable to assign [undefined] to double` 刷屏且探针不执行 → 改为**普通组件 + 页面内实例化**（`DesignTokens { id: tokens }`）；delegate/内联 Window 内不能访问实例 id，须经页面属性中转（`page.rowRadius`/`page.cardRadius` 模式，见 `qml/TranslateHomePage.qml` 头部注释）
- **中文标点比较必须用码点**：`QLatin1Char('。')` 对多字节 UTF-8 字面量会截断（取最低字节），永不匹配 → 用 `QChar::unicode()` 与 `0x3002` 等码点比较（`endsWithSentenceBoundary` 踩过）
- **测试配置隔离**：`ConfigService::set` 会落盘 `%APPDATA%`，测试进程间互相污染（MSVC 测试写 `sentenceAwareChunking=false` → clang 测试读到 false 失败）→ 测试必须 `ConfigService::setDataDirectoryForTest(临时目录)`（参照 `tst_quality::initTestCase`）
- **UI 驱动点击坐标**：DPI 缩放（120%）下 `SetCursorPos` 用物理坐标、WM_LBUTTONDOWN 用客户区逻辑坐标；PowerShell `[ref]` 传 out int 参数在 pwsh 7 有怪癖（只填充首参），用 csc 编译 C# 工具最稳（`%TEMP%\opencode\cap.exe`/`wmclick.exe` 模式）

## 7. 子模块补丁（⚠️ 保持本地状态，勿提交/勿还原）

`git status` 里 `third_party/*` 显示 `m`/`M` 是**有意补丁**，**绝不能** `git submodule update` 或提交子模块改动（会推坏/还原补丁）。

| 子模块 | 补丁 |
| --- | --- |
| FluentUI | `src/CMakeLists.txt`（PLUGIN_TARGET 条件化）、`src/FluFrameless.cpp`（防御）、`src/Qt6/imports/FluentUI/Controls/FluWindow.qml`（typeof 防御 + **`loader_app_bar z:1`**——appBar 悬浮层高于内容层，2026-08-18 窗口按钮修复）；`.gitignore` 加 `src/FluentUI/`（构建产物） |
| zlib | `CMakeLists.txt`（静态 ZLIB::ZLIB alias；注释 `test/`、`contrib/`） |
| quazip | 无补丁 |

## 8. 下一步实施蓝图

### 候选任务（迭代5 完成后的非阻塞项）
- docx 导出（对称补全 B）
- pdf 导出文本层修复（升级 Qt ≥6.8 后复查 pdf-service.md §3.0）
- .trx 图片 external 降级（>1MB 图片外置 `*.images/` 目录，已在 file-service.md §7 设计）
- 术语表 UI（`TermGlossary` 已有 C++ 层）
- 更多示例插件（对照 `docs/services/plugin-development.md` 写第三方插件演示）

## 9. 工作流程（AGENTS.md 摘要）

1. 读 `AGENTS.md` + 本文件 + 相关 `docs/` 设计
2. **重大决策（架构/接口/依赖/许可）→ 先 Ask 用户**，不要自作主张
3. 建 Todo 清单，小步实现，**每步可编译可测试**
4. 改动实现必须同步更新对应 `.md` 文档
5. 完成标准：构建通过 + `ctest` 全绿 + 文档同步 + 汇报（变更/影响/下一步）

### UI 驱动（应用内测试钩子，2026-08-17）
- 用途：review agent 模拟用户操作（打开文件/切主题/翻译/查状态），配合截图做 UI 自动化验证
- 架构：TRANSLEX_UI_DRIVER=1 启动 -> src/driver_service.cpp（QLocalServer named pipe translex-ui-driver，JSON 行协议）-> QML UiDriverActions（业务动作，onCompleted 注册 sink）
- 客户端：.opencode/scripts/ui-driver.mjs（Node net.connect 到 pipe；应用未运行自动以驱动模式启动）
- 命令：openFile / setDark / getState / translateLine / translateAll / navigate（--index，0=编辑 1=设置）/ testConnection / getConfig（--section）/ setConfig（--set section=key=value）
- 踩坑：QML 函数参数在 meta 系统暴露为 QVariant（invokeMethod 须 Q_ARG(QVariant)）；客户端需自行 end() 连接（服务端不主动断开）；ui_ 前缀文件名会被 AUTOUIC 误判（命名 driver_service 规避）；**鼠标点击类 UI 验证不可靠**（SendMessage/真实点击对 QML 窗口常无效，且多实例会串窗口）→ 优先用驱动命令直达服务层
- **挂起陷阱（2026-08-18）**：应用未运行时 ui-driver 会代启动应用（spawn detached），但 opencode 的 bash 工具会等待该 GUI 子进程退出 → 命令超时（用户看到「卡死」）→ **先确认应用已在运行（`Get-Process translex`），未运行则请用户手动启动后再连**；诊断日志优先走 AppGuard（`%APPDATA%/sr291/Translex/Translex-<日期>.log`）而非终端重定向
- review 流程：.opencode/prompts/review.md §0（驱动操作 -> 截图 -> vision 断言 -> Stop-Process 清理）

### Review 权限语义与导出往返（2026-08-17）
- 语义：review 不是只读——可运行程序/导出到临时目录/检查产物/清理自己产生的临时文件；唯一红线是**不修改项目文件与数据、不负责修改**
- 导出往返检查：驱动 saveFileAs 导出（txt/trx/docx/pdf）→ Get-Item 检查产物 → 再 openFile 导入 → getState/getLineText 断言往返保真（反复导出 2-3 次验证稳定）
- 无用文件/缓存检查：git status 未跟踪残留、未引用源文件、samples 临时产物；审查结束清理自己产生的截图/导出文件与进程
- 踩坑：ui-driver.mjs 须 socket.destroy() + process.exit(0)（优雅 end() 会致 node 事件循环挂起、进程残留堵 pipe）
