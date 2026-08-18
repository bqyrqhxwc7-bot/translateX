# Translex

Translex 是一个基于 Qt 6 的桌面翻译写作工具。它提供了一个带批注/翻译对照的编辑器，支持本地 Ollama、云端翻译服务与网络大模型 API 三种翻译后端，可将翻译结果作为批注写入文档，并支持按行、所选范围、整篇文档或未批注章节批量翻译。

## 主要功能

- **Ribbon 工具栏**（PPT 式）：文件 / 开始 / 翻译 / 章节 / 批注 / 查找 六个标签功能区
- **三种翻译后端**
  - 本地 Ollama（自动扫描模型、关闭 Thinking、上下文翻译）
  - 云端翻译服务（在线逐行翻译，本地失败自动回退）
  - 网络大模型 API（OpenAI 风格地址 + API Key，内置 DeepSeek v4-flash 预设）
- **翻译质量**：上下文感知（参考前后 N 行统一术语语气）、回显拦截（拒绝输出原文）、质量自检
- **降低成本**：缓存复用、失败自动降级、智能分块合并（一次请求多行）、严格输出
- **翻译浮窗**：可拖到屏幕任意位置的独立无边框窗口（Fluent 卡片样式），位置自动记忆；跟随主窗口生命周期（最小化隐藏/恢复显示，退出随主窗口关闭）
- **Outlook 式主窗口**：最左 44px 图标栏（可收起，含展开手柄）+ 可拖拽分割侧边栏面板（180-400px，章节/批注/翻译历史）+ 内容区 NoStack 切换（编辑页 / 设置页）
- **插件化（L3 动态插件）**：`ServiceRegistry` 统一注册/查询/健康度聚合；启动时 `QPluginLoader` 扫描 `<exe>/plugins/`；自带示例插件（`translation.echo` 回显后端 + 侧边栏面板）验证全链路
- **设置页服务调试卡片**：服务健康度列表（状态色点 + 消息）、插件加载诊断、配置文件 / 日志文件路径
- **编辑能力**：撤销/重做、多选行翻译、Enter 拆行、行首 Backspace 合并
- **批注管理**：翻译结果写为批注、上一条/下一条跳转、清空、JSON 导出/导入
- **章节导航**：自动识别标题行（中文"第X章"、Markdown `#`）、上一章/下一章、重新检测
- **查找替换**：全文查找/替换、大小写/整词/模糊查找开关、匹配计数
- **设置页**：schema 驱动渲染（`ConfigSectionCard`），翻译/界面/显示/查找多组配置
- **.trx / .docx / .pdf 格式**：.trx 显示层（富文本/图片）随文档往返保存；docx 导入（图文混排）；PDF 每行一页导出、导入完整往返（`samples/demo.docx`/`samples/demo.pdf`）
- **大文件性能**：ListView 虚拟化 + 懒加载模型，50 万行加载 < 100ms
- **安全存储**：API Key 经 `SecureStorage` 加密落盘，无明文

## 项目结构

```text
.
├── CMakeLists.txt              # CMake 构建 / 测试 / CPack 打包
├── qml/                        # QML 界面（FluentUI）
│   ├── Main.qml                # 主窗口（Outlook 式布局：图标栏 + 侧边栏面板 + 内容区 NoStack 切换）
│   ├── IconBarButton.qml       # 图标栏按钮组件（tooltip / active 指示条）
│   ├── TranslateHomePage.qml   # 翻译编辑页：Ribbon + 虚拟化编辑器 + 翻译浮窗
│   ├── TranslatePanelContent.qml # 翻译面板复用组件（浮窗主体）
│   ├── TranslateSettingsPage.qml # 设置页（含服务调试卡片）
│   ├── ConfigSectionCard.qml   # 设置项 schema 渲染卡片
│   └── panels/                 # service 侧边栏面板（sidebarPanels() 动态注册）
│       ├── ChapterPanel.qml    # 章节导航
│       ├── CommentPanel.qml    # 批注列表
│       └── HistoryPanel.qml    # 翻译历史
├── src/
│   ├── main_qml.cpp            # QML 版入口（当前主线，service 注册到 ServiceRegistry）
│   └── services/               # ★ 可插拔服务层（Q_INVOKABLE，QML 直接调用）
│       ├── documentmodel.*     # 懒加载文档行模型（大文件性能核心）
│       ├── translationservice.* # 翻译编排：上下文/分块/降级/回显拦截/缓存
│       ├── translationbackend.* # 三种后端：Ollama / 云翻译 / 网络大模型
│       ├── serviceregistry.*   # 服务注册表（注册/查询/健康度聚合/插件扫描）
│       ├── iservice.*          # IService 接口（serviceId/displayName/健康度/侧边栏面板）
│       ├── commentservice.*    # 批注单一数据源（provider 委托）
│       ├── chapterservice.*    # 章节识别与跳转
│       ├── findservice.*       # 查找替换
│       ├── documentmanager.*   # 文档打开/保存/最近文件
│       ├── trxparser.*         # .trx 格式读写（显示层往返）
│       ├── configservice.*     # 配置（schema 驱动，ui.json/translation.json）
│       ├── securestorage.*     # 敏感设置加密存储
│       ├── termglossary.*      # 术语表
│       ├── qualitygate.*       # 质量自检（回显拦截）
│       ├── translationcache.*  # 翻译缓存
│       └── appguard.*          # 稳定性：日志 / 崩溃诊断
├── plugins/                    # 示例插件（example_translation_plugin：回显后端 + 面板）
├── tests/                      # 单元测试 + 性能基准（16 个目标）
├── docs/                       # 架构与服务设计文档
├── third_party/FluentUI/       # FluentUI 1.7.7（BSD-3-Clause，git 子模块）
└── .vscode/                    # VS Code 构建/调试/运行配置
```

> 完整架构说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)，服务设计见 `docs/services/`。

## 构建

### 可执行目标

| 目标 | 说明 |
| --- | --- |
| `translex` | QML 版（当前主线，Outlook 式主窗口 + FluentUI） |
| `example_translation_plugin` | 示例插件 DLL（回显后端 `translation.echo` + 侧边栏面板），构建后自动部署到 `<exe>/plugins/` |

> 旧 QtWidgets 版已拆分到 `widgets` 分支维护，本分支（main）仅保留 QML 版。

## 构建要求

- **克隆后先拉取 FluentUI 子模块**：`git submodule update --init`
- Qt 6（本机示例路径：`D:\Software\Qt\6.5.3\msvc2019_64`，需含 Quick/Qml/QuickWidgets 模块）
- CMake 3.21+
- Microsoft Visual Studio（Windows，项目生成器为 `Visual Studio 18 2026`，x64）

## 构建步骤

CMake 会自动在常见位置查找 Qt；如未找到，请通过 `-DCMAKE_PREFIX_PATH` 显式指定 Qt 路径：

```powershell
cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.11.1/msvc2022_64"
cmake --build build-vs2026-x64 --config Debug
```

> 测试默认随 `include(CTest)` 开启；若某次构建目录曾以 `BUILD_TESTING=OFF` 配置过，
> `BUILD_TESTING` 可能被缓存成 OFF，重新配置时请显式加回：
> `cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.11.1/msvc2022_64" -DBUILD_TESTING=ON`

构建成功后，可执行文件位于 `build-vs2026-x64\Debug\translex.exe`，Qt 运行时会通过 `windeployqt` 自动部署。

> 提示：如果构建时出现 “找不到 Qt6”，请检查是否在 `D:\Software\Qt\<版本>\<套件>\` 下存在 `lib\cmake\Qt6\Qt6Config.cmake`，并确保 `CMAKE_PREFIX_PATH` 指向该套件目录。

## 测试

```powershell
# 运行全部 16 个测试目标（需将 Qt bin 目录加入 PATH）
$env:PATH = "D:/Software/Qt/6.11.1/msvc2022_64/bin;" + $env:PATH
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 仅性能基准（大文件）
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

测试覆盖：文档模型、安全存储（加密/防篡改/明文检测）、翻译（后端/回显拦截/缓存）、质量自检、配置服务、批注、文档管理、章节、查找、TTS、翻译历史、插件注册表（注册/查询/健康度聚合/后端）、.trx/.docx/.pdf 格式往返、大文件性能基准（50 万行加载 < 100ms）。

## NSIS 安装程序打包

当前项目支持 NSIS 安装包打包。要生成安装程序，需要先安装 NSIS 并确保 `makensis` 在系统 `PATH` 中（或设置环境变量 `MAKENSIS` 指向 `makensis.exe`）。

推荐安装方法：

- Windows: 从 <https://nsis.sourceforge.io/> 下载并安装
- 或者使用 `choco install nsis`（如果已安装 Chocolatey）

然后在构建目录中运行：

```powershell
cmake --build build-vs2026-x64 --config Release --target package
```

产物生成于 `build-vs2026-x64\Translex-1.0.0.exe`（NSIS 安装程序）与 `Translex-1.0.0.zip`。如果环境中未安装 NSIS，项目会自动回退生成 ZIP 包。

## 翻译配置

在「设置」页（schema 驱动渲染）配置：

- **翻译后端**：本地 Ollama / 云端翻译服务 / 网络大模型 API
- **源/目标语言**：自动 / 中 / 英 / 日 / 韩 / 法 / 德 / 西 / 俄
- **上下文行数**：目标行前后各参考多少行（0 表示只翻译目标行）
- **严格输出 / 缓存复用 / 失败降级 / 智能分块 / 质量自检**：开关
- **网络大模型**：OpenAI 风格 API 地址 + Key（DeepSeek 预设：`https://api.deepseek.com`，模型 `deepseek-v4-flash`，thinking 已禁用；也可用其他 OpenAI 兼容端点）
- **自定义提示词**：普通翻译 / 上下文翻译各一套模板（`%1` 原文，`%2` 上下文）

## 快捷键

| 功能 | 快捷键 |
| --- | --- |
| 撤销 / 重做 | `Ctrl+Z` / `Ctrl+Y` |
| 翻译当前行 | `Ctrl+Alt+T` |
| 翻译全部待译行 | `Ctrl+Alt+Shift+T` |

其余功能通过 Ribbon 工具栏按钮与翻译浮窗完成。

## 敏感信息与隐私

- **API Key 加密存储**：通过 `SecureStorage`（机器指纹密钥 + 随机盐 + 校验）加密后写入 `%APPDATA%/Translex/secure.ini`，磁盘上不出现明文。
- **日志**：写入 `%APPDATA%/sr291/Translex/` 下的每日日志（`Translex-<yyyyMMdd>.log`，AppDataLocation），不包含文档内容。
- **打包产物**：仅包含可执行文件与运行时依赖，不含任何用户配置。

## Qt AI 开发工具（skills + MCP）

本项目集成了 Qt 官方（The Qt Company RnD）的 AI 开发技能与 Qt 文档 MCP server，来源为 <https://github.com/TheQtCompanyRnD/agent-skills>（BSD-3-Clause 许可证）。

- **Skills**（`.agents/skills/`）：GitHub Copilot 会自动发现这些目录，提供 QML 编码/审查、Qt C++ 审查、Qt CMake 工程搭建、UI 设计、Qt Quick Test、Qt 文档生成等 12 个技能。
- **MCP**（`.mcp.json`）：注册了 Qt 官方托管的文档查询服务 `https://qt-docs-mcp.qt.io/mcp`，AI 助手可直接检索最新版与 LTS 版 Qt API 文档。

> 使用这些技能与 MCP 工具即表示同意 [Qt AI Services 条款](https://www.qt.io/terms-conditions/ai-services-2025-06)。

## 许可证

本项目采用 MIT 许可证，详见 `LICENSE` 文件。
