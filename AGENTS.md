# AGENTS.md — Translex 协作规范

> **接手必读**：`docs/HANDOVER.md`（功能状态 / 路线图 / 架构铁律 / 踩坑总纲——任何新会话先读它）→ 本文 → `docs/ARCHITECTURE.md`。

## 0. 项目是什么

**Translex**（曾用名 translateX）：Qt 6 桌面翻译写作工具。编辑器 + 批注 + 三翻译后端（Ollama/云端/OpenAI 风格 API），`.txt`/`.trx`/`.docx` 三格式。UI 为 QML + FluentUI，业务逻辑在 `src/services/` 的 C++ 服务层。

## 1. 核心原则（按优先级）

0. **全程用中文交流**（用户为中文母语，国内网络环境）；文档、注释、回复一律中文，代码标识符用英文。
1. **文档先行**：设计/重构/新功能先写 `docs/` 设计文档，确认后再写代码；代码与文档必须同步更新。
2. **重大决策先问用户**：架构方向、接口变更、破坏性重构、依赖引入、许可协议选择——先 Ask 再动手。
3. **多用 Todo 跟踪**：多步骤任务建 Todo 清单，每完成一项立即更新。
4. **及时记录设计**：重要决策/踩坑写入 `docs/` 或仓库记忆，供后续会话延续。
5. **小而可验证**：每次改动可编译、可测试；不一次性做超大改动。

## 2. 架构约定

- **前后端分离**：QML 界面（`qml/`），业务在 `src/services/` C++ 服务层（`Q_INVOKABLE`，经 `main_qml.cpp` 的 `setContextProperty` 暴露给 QML）。
- **可插拔**：新能力优先实现为 service（见 `docs/services/SERVICE-ARCHITECTURE.md`），不往 mainwindow/主页堆代码。
- **接口稳定**：`IService` / `ITranslationBackend` 等公共接口定稿后不轻易改；扩展用新增方法（带默认实现）。
- **翻译服务定位（不可偏离）**：更好的质量（上下文感知/术语一致/质量自检）+ 为用户减成本（缓存/模型分级/智能分块/失败降级）；改动对照 `docs/services/translation-service.md` 的度量指标。
- **NoStack 页面模式**（`FluNavigationView pageMode: NoStack`）：每次导航重建页面 → 状态必须放应用级 context property 单例，禁止放页面属性；**Popup 控件不可用**（错位/失效）；`Qt.callLater` 不可靠，用 `Timer`。详见 HANDOVER.md §4。
- **大文件性能**：虚拟化渲染（ListView + 懒加载模型），禁止全量刷新。
- **敏感信息**：一律走 `SecureStorage`（`%APPDATA%/Translex/secure.ini`），禁止明文落盘。
- **新配置 key** 必须加进 `src/services/config/ui.json` schema（ConfigService 只认 schema 内 key）。

## 3. 技术约束与构建命令

- Qt 6.5.3（`D:/Software/Qt/6.5.3/msvc2019_64`，CMakeLists 已内置该探测路径），C++17，生成器 VS 2026 x64，构建目录 `build-vs2026-x64/`。
- FluentUI 1.7.7 + QuaZip + zlib（git 子模块，静态编译；docx 导入依赖）。
- 应用目标 **`translex`**（exe：`build-vs2026-x64\Debug\translex.exe`）；QML 模块 URI `Translex`，`qrc:/qt/qml/Translex/qml/*.qml`。**改名前的 translateX 仅残留在 .trx 格式的兼容标记**（读旧文档用）。

```powershell
# 构建
cmake --build build-vs2026-x64 --config Debug

# 重新配置（改了 CMakeLists/子模块后）——BUILD_TESTING 可能被缓存成 OFF，务必显式 ON
cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.5.3/msvc2019_64" -DBUILD_TESTING=ON

# 测试（必须先加 Qt bin 到 PATH，否则 0xc0000135；测试 exe 在 build-vs2026-x64\tests\Debug\）
$env:PATH = "D:/Software/Qt/6.5.3/msvc2019_64/bin;" + $env:PATH
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 单测 / 性能基准
ctest --test-dir build-vs2026-x64 -C Debug -R tst_docx --output-on-failure
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

## 4. 关键陷阱（错过必踩）

- **子模块补丁**：`git status` 中 `third_party/FluentUI`、`zlib` 显示 `m`/`M` 是**有意补丁**（详见 HANDOVER.md §7）——**绝不** `git submodule update`，绝不提交/还原子模块改动。
- **新增 service 文件**必须同时注册**两处**：`CMakeLists.txt` 的 `translex` 目标 + `tests/CMakeLists.txt` 的 `translex_services` 静态库 `SERVICE_SOURCES`；漏掉第二处 = 新测试无法链接。
- **QML 内嵌 qrc**（`qt_add_qml_module`）：改 `.qml` 必须重新构建才能生效。
- **构建前停掉运行中的 `translex.exe`**（否则 LNK1168 文件占用）。
- 新配置/设置项 → 改 `src/services/config/ui.json`（见 §2）。
- 改实现必须同步更新对应 `docs/` 文档（HANDOVER.md 是权威汇总）。

## 5. AI 工具环境

- 本仓库带 Qt 官方 skills（`.agents/skills/`：QML/C++ 审查、Qt Quick Test、CMake、UI 设计等）与 Qt 文档 MCP（`.mcp.json`）。
- opencode 多模型分工已配置在 `opencode.json`（build=deepseek-v4-flash，plan/review=deepseek-v4-pro），改动前先读该文件。
- **国内网络环境**：`git push` 可能失败（网络不稳），失败直接重试；winget/gh 安装类命令易卡死（曾超时），不要主动执行系统级安装；Qt 文档 MCP（`qt-docs-mcp.qt.io`，海外）可能超时——超时则改查本地 Qt 头文件/`D:/Software/Qt/6.5.3/` 文档，勿反复重试。
- **终端编码**：控制台中文乱码是 GBK/UTF-8 不匹配（PowerShell 先 `chcp 65001` 或设置 `[Console]::OutputEncoding`）；用 `Read`/`Edit` 工具读写源码，勿经控制台管道改写（会破坏中文注释编码）。

## 6. opencode 操作提示

- **agent 切换**：`Tab` / `Shift+Tab` 循环主 agent（build → plan → review）；`Ctrl+X 然后 A` 打开 agent 列表；subagent（`@explore`/`@general`/`@doc-writer`）在输入框 `@` 触发。Tab 与输入补全的冲突已在全局 `tui.json` 解决（补全改 `Ctrl+Space`）。
- **模型切换**：`Ctrl+X 然后 M` 打开模型列表（Go 套餐内 DeepSeek V4 Flash/Pro、GLM 等）。

## 7. 工作流程

```
1. 读 HANDOVER.md → AGENTS.md → 相关 docs/
2. 重大决策 → ask 用户
3. 建 Todo 清单
4. 小步实现 + 构建 + 测试
5. 记录决策/踩坑到 docs/ 或记忆
6. 每步汇报，保持透明
```

## 8. 完成标准

- 构建通过（`cmake --build build-vs2026-x64 --config Debug`）
- 相关测试通过（`ctest --test-dir build-vs2026-x64 -C Debug`）
- 文档与代码同步（改了实现必须改对应 `.md`）
- Todo 全部完成并更新；向用户汇报变更 + 影响 + 下一步
