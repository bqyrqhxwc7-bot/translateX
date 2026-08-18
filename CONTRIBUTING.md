# 贡献指南（Translex）

欢迎贡献！本文档是贡献流程与工程约定的入口。上手前请务必先读：

1. [`docs/HANDOVER.md`](docs/HANDOVER.md) —— 功能状态 / 路线图 / 架构铁律 / 踩坑总纲（**权威汇总，先读它**）
2. [`AGENTS.md`](AGENTS.md) —— 协作规范（本文档是其精简操作版）
3. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) —— 分层架构
4. 涉及的功能对应 `docs/services/*.md` 设计文档

## 1. 沟通与文档约定

- **全程中文交流**（维护者为中文母语）；文档、注释、回复一律中文，**代码标识符用英文**。
- 外国贡献者可用英文交流；代码与提交信息用英文亦可，但文档保持中文（`AGENTS.md` 顶部有 English Overview）。
- **文档先行**：设计 / 重构 / 新功能先在 `docs/` 写设计文档，确认后再写代码；改实现必须同步更新对应 `.md`。
- **重大决策先问维护者**：架构方向、接口变更、破坏性重构、依赖引入、许可协议——先 Ask 再动手。

## 2. 贡献流程

1. 读 `docs/HANDOVER.md` → `AGENTS.md` → 相关 `docs/services/` 文档
2. 重大改动先与维护者确认设计（文档先行）
3. 建 Todo 清单，**小步实现，每步可编译可测试**（不一次性做超大改动）
4. 每完成一项更新 Todo 并保持透明汇报
5. 本地构建 + 测试全绿后提交；代码与文档同步
6. 提交前运行审查（可选）：`.agents/skills/` 提供 Qt C++ / QML 审查技能

## 3. 构建与测试命令

```powershell
# 配置（Qt 6.5.3 已内置探测；重新配置时 BUILD_TESTING 可能被缓存成 OFF，务必显式 ON）
cmake -S . -B build-vs2026-x64 -DCMAKE_PREFIX_PATH="D:/Software/Qt/6.5.3/msvc2019_64" -DBUILD_TESTING=ON

# 构建（构建前停掉运行中的 translex.exe，避免 LNK1168 文件占用）
cmake --build build-vs2026-x64 --config Debug

# 测试（必须先加 Qt bin 到 PATH，否则 0xc0000135）
$env:PATH = "D:/Software/Qt/6.5.3/msvc2019_64/bin;" + $env:PATH
ctest --test-dir build-vs2026-x64 -C Debug --output-on-failure

# 单测 / 性能基准
ctest --test-dir build-vs2026-x64 -C Debug -R tst_registry --output-on-failure
ctest --test-dir build-vs2026-x64 -C Debug -L perf
```

- 测试覆盖 16 个目标（文档模型 / 安全存储 / 翻译 / 质量 / 配置 / 批注 / 文档管理 / 章节 / 查找 / TTS / 历史 / 插件注册表 / .trx / .docx / .pdf / 性能基准）。
- 火绒等国内杀软可能挂起新构建的测试 exe——测试突然卡死先怀疑它（把项目目录加入信任区）。
- 完成标准：构建通过 + 相关测试通过 + 文档与代码同步 + Todo 更新并汇报。

## 4. 插件开发指引

项目支持 **L3 动态插件**（自定义翻译后端 + 侧边栏面板），不修改核心代码即可扩展。开发指南见
**[`docs/services/plugin-development.md`](docs/services/plugin-development.md)**，参考实现为
`plugins/example_translation_plugin/`（回显后端 `translation.echo` + 面板）。

要点：

- 插件实现 `ITranslationPlugin`（`Q_PLUGIN_METADATA` + `Q_INTERFACES`），链接 `Translex_sdk`（INTERFACE 目标）
- 后端实现 `ITranslationBackend`；面板经 `sidebarPanel()` 返回 QML 路径
- DLL + 面板 QML 部署到 `<exe>/plugins/`，启动时 `scanPluginDirectory` 自动加载
- 调试：设置页「调试」卡片（健康度 / 插件加载诊断）+ `tst_registry`

## 5. 提交信息风格

遵循 Conventional Commits + 中文描述（scope 常用 `qml` / `cmake` / `trx` / `docx` / `pdf` / `translation` /
`plugins` / `iterationN` 等）。参考现有 git log：

```text
feat(plugins): 迭代5阶段1——IService 健康度 + 注册表服务注册 + Translex_sdk + tst_registry
feat(iteration5): 阶段2——Outlook 式主窗口布局 + 设置页调试卡片 + 窗口按钮修复
feat(tts): 迭代3——TTS 朗读（独立 service，跨平台优雅降级）
fix(translation): 后端连接测试合并用户配置并携带 extra 参数
docs(ui): 同步浮窗实现文档 - 真独立Window + 桌面钳制 + 启动延迟显示 + NaN防御
test(docx): samples/demo.docx 示例 + tst_docx 回归用例
build(cmake): 修复FluentUI静态构建空PLUGIN_TARGET的CMP0174警告
chore: 移除调试残留文件, 加入 .gitignore
```

规则：

- 类型前缀：`feat`（功能）/ `fix`（修复）/ `docs`（文档）/ `test`（测试）/ `build`（构建）/ `chore`（杂项）/ `refactor`（重构）
- 中文描述，一句概括「做了什么 + 为什么」，必要时括号补充
- 不要包含与改动无关的内容；一个提交聚焦一件事

## 6. 红线提醒（详情见 HANDOVER.md）

- **绝不** `git submodule update`、绝不提交/还原子模块改动：`third_party/FluentUI`、`zlib` 携带**有意本地补丁**（`git status` 显示 `m`/`M` 属正常）。
- 新增 service 文件必须同时注册 `CMakeLists.txt` 的 `translex` 目标 + `tests/CMakeLists.txt` 的 `translex_services` 静态库（漏第二处 = 新测试无法链接）。
- 新配置 key 必须加进 `src/services/config/ui.json` schema（ConfigService 只认 schema 内 key）。
- 状态放应用级 context property 单例，勿放 NoStack 页面属性；`Qt.callLater` 不可靠，用 `Timer`。
- 终端改文件经 Git 工具/编辑器，勿用控制台管道改写源码（会破坏中文注释编码）。
