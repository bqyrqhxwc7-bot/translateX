# translateX

translateX 是一个基于 Qt 6 的桌面翻译写作工具。它提供了一个带批注/翻译对照的编辑器，支持本地 Ollama、云端翻译服务与网络大模型 API 三种翻译后端，可将翻译结果作为批注写入文档，并支持按行、所选范围、整篇文档或未批注章节批量翻译。

## 主要功能

- **三种翻译后端**
  - 本地 Ollama（支持自动扫描模型、关闭 Thinking、上下文翻译）
  - 云端翻译服务（内置在线逐行翻译，本地失败可自动回退）
  - 网络大模型 API（OpenAI 风格地址 + API Key，兼容 DeepSeek v3.2）
- **灵活的翻译范围**：当前行 / 所选行 / 全文 / 当前章节 / 未批注章节
- **上下文翻译**：参考目标行前后 N 行统一术语与语气
- **自定义提示词**：普通翻译与上下文翻译各一套提示词模板
- **DeepSeek v3.2 预设**：一键套用推荐的大段翻译参数
- **严格输出模式**：只保留译文，过滤模型附加说明
- **合并分块翻译**：将多行合并为大块请求，减少 API 调用
- **文档格式**：支持 TXT 读写，支持 DOCX / TPX(TRX) 导入导出
- **批注管理**：插入、折叠、删除、批量操作、快速跳转
- **自动保存**：可配置间隔、手动立即保存
- **查找替换**、**专注模式**、**章节导航**、**文档信息面板**
- **默认配置保存与恢复**：一键将当前翻译设置保存为默认

## 项目结构

```text
.
├── CMakeLists.txt              # CMake 构建 / 测试 / CPack 打包
├── src/
│   ├── main.cpp                # Widgets 版入口（旧版，保留兼容）
│   ├── main_qml.cpp            # QML 版入口（当前主线，FluentUI）
│   ├── mainwindow.*            # Widgets 主窗口（迁移源）
│   ├── annotatedtexteditor.*   # Widgets 编辑器（迁移源）
│   └── services/               # ★ 可插拔服务层
│       ├── documentmodel.*     # 懒加载文档行模型（大文件性能核心）
│       ├── securestorage.*     # 敏感设置加密存储
│       └── appguard.*          # 稳定性：日志 / 崩溃诊断
├── qml/                        # QML 界面（FluentUI）
│   ├── Main.qml                # 主窗口 + 导航
│   ├── TranslateHomePage.qml   # 翻译编辑页（虚拟化编辑器）
│   └── TranslateSettingsPage.qml
├── tests/                      # 单元测试 + 性能基准
├── docs/ARCHITECTURE.md        # 架构与扩展文档
├── third_party/FluentUI/       # FluentUI 1.7.7（BSD-3-Clause）
└── .vscode/                    # VS Code 构建/调试/运行配置
```

> 完整架构说明见 [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)。

## 构建

### 两个可执行目标

| 目标 | 说明 |
| --- | --- |
| `translateX` | Widgets 版（旧版，功能完整，迁移期间保留） |
| `translateXqml` | QML 版（当前主线，FluentUI 界面） |

## 构建要求

- Qt 6（本机示例路径：`D:\Software\Qt\6.5.3\msvc2019_64` 或 `C:\Qt\6.5.3\msvc2019_64`，需含 Quick/Qml/QuickWidgets 模块）
- CMake 3.21+
- Microsoft Visual Studio（Windows，项目生成器为 `Visual Studio 18 2026`，x64）

## 构建步骤

CMake 会自动在常见位置查找 Qt；如未找到，请通过 `-DCMAKE_PREFIX_PATH` 显式指定 Qt 路径：

```powershell
cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.5.3/msvc2019_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build-vs2026-x64 --config Release
```

构建成功后，可执行文件位于 `build-vs2026-x64\Release\translateXqml.exe`（QML 版）与 `translateX.exe`（Widgets 版），Qt 运行时会通过 `windeployqt` 自动部署。

> 提示：如果构建时出现 “找不到 Qt6”，请检查是否在 `D:\Software\Qt\<版本>\<套件>\` 下存在 `lib\cmake\Qt6\Qt6Config.cmake`，并确保 `CMAKE_PREFIX_PATH` 指向该套件目录。

## 测试

```powershell
# 构建测试目标
cmake --build build-vs2026-x64 --config Debug --target tst_documentmodel tst_securestorage tst_performance

# 运行全部测试（需将 Qt bin 目录加入 PATH）
$env:PATH = "D:/Software/Qt/6.5.3/msvc2019_64/bin;" + $env:PATH
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 仅性能基准（大文件）
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

测试覆盖：文档模型健康度、安全存储（加密/防篡改/明文检测）、大文件性能基准（50 万行加载 < 100ms）。

## NSIS 安装程序打包

当前项目支持 NSIS 安装包打包。要生成安装程序，需要先安装 NSIS 并确保 `makensis` 在系统 `PATH` 中（或设置环境变量 `MAKENSIS` 指向 `makensis.exe`）。

推荐安装方法：

- Windows: 从 <https://nsis.sourceforge.io/> 下载并安装
- 或者使用 `choco install nsis`（如果已安装 Chocolatey）

然后在构建目录中运行：

```powershell
cmake --build build-vs2026-x64 --config Release --target package
```

产物生成于 `build-vs2026-x64\translateX-1.0.0.exe`（NSIS 安装程序）与 `translateX-1.0.0.zip`。如果环境中未安装 NSIS，项目会自动回退生成 ZIP 包。

## 翻译配置

在“设置 → 翻译”与“设置 → 高级”中配置：

- **翻译后端**：本地 Ollama / 云端翻译服务
- **Ollama 端点与模型**：默认 `http://127.0.0.1:11434`，可自动扫描可用模型
- **上下文大小**：目标行前后各参考多少行（0 表示只翻译目标行）
- **网络大模型 API 地址**：如 `https://api.deepseek.com/chat/completions`
- **网络大模型 API 密钥**：以 `Bearer` 方式写入请求头
- **提示词模板**：`%1` 表示目标原文，`%2` 表示上下文
- **合并分块**：最大合并目标行数与最大合并字符数
- **预设**：默认 / DeepSeek v3.2

## 快捷键

| 功能 | 快捷键 |
| --- | --- |
| 新建 / 打开 / 保存 / 另存为 | `Ctrl+N` / `Ctrl+O` / `Ctrl+S` / `Ctrl+Shift+S` |
| 立即自动保存 | `Ctrl+Alt+S` |
| 查找 / 下一个 / 上一个 / 替换 | `Ctrl+F` / `F3` / `Shift+F3` / `Ctrl+H` |
| 全部替换 | `Ctrl+Shift+H` |
| 翻译当前行或所选行 | `Ctrl+Alt+T` |
| 翻译全文 | `Ctrl+Alt+Shift+T` |
| 插入注释行 | `Ctrl+Alt+M` |
| 按范围批量添加批注 | `Ctrl+Alt+Shift+M` |
| 折叠/展开当前注释 | `Ctrl+Alt+/` |
| 删除当前注释 | `Ctrl+Alt+Delete` |
| 上一条 / 下一条注释 | `Alt+Shift+Up` / `Alt+Shift+Down` |
| 显示/隐藏 导航 / 信息 / 查找 / 注释 / 翻译 | `Ctrl+1` ~ `Ctrl+5` |
| 专注模式 | `Ctrl+Shift+F` |
| 打开设置 | `Ctrl+,` |

快捷键可在“设置 → 快捷键”中自定义。

## 敏感信息与隐私

- **API Key 加密存储**：通过 `SecureStorage`（机器指纹密钥 + 随机盐 + 校验）加密后写入 `%APPDATA%/translateX/secure.ini`，磁盘上不出现明文。
- **日志**：写入 `%APPDATA%/translateX/` 下的每日日志，不包含文档内容。
- **打包产物**：仅包含可执行文件与运行时依赖，不含任何用户配置。

## Qt AI 开发工具（skills + MCP）

本项目集成了 Qt 官方（The Qt Company RnD）的 AI 开发技能与 Qt 文档 MCP server，来源为 <https://github.com/TheQtCompanyRnD/agent-skills>（BSD-3-Clause 许可证）。

- **Skills**（`.agents/skills/`）：GitHub Copilot 会自动发现这些目录，提供 QML 编码/审查、Qt C++ 审查、Qt CMake 工程搭建、UI 设计、Qt Quick Test、Qt 文档生成等 12 个技能。
- **MCP**（`.mcp.json`）：注册了 Qt 官方托管的文档查询服务 `https://qt-docs-mcp.qt.io/mcp`，AI 助手可直接检索最新版与 LTS 版 Qt API 文档。

> 使用这些技能与 MCP 工具即表示同意 [Qt AI Services 条款](https://www.qt.io/terms-conditions/ai-services-2025-06)。

## 许可证

本项目采用 MIT 许可证，详见 `LICENSE` 文件。
