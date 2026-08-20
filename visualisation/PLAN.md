# Translex 依赖可视化 · 规划文档

> 状态：规划定稿（v2 增补实现主体维度）· 日期：2026-08-20
> 范围：仅限 `visualisation/` 文件夹
> 参考：Graph Explorer | Language Lineage（https://www.languagelineage.org/explore）、Open Source Insights（https://deps.dev/）

## 1. 目标

为 Translex（Qt 6 桌面翻译写作工具）产出一份**递归、全链路**的依赖可视化：

- **广度**：从源码 → 第三方库 → 编译工具链 → 部署/打包工具 → 运行时系统依赖，全部覆盖（用到的都算）。
- **深度**：递归展开每一层的子依赖（如 QtPdf → QtMultimedia → …；FluentUI → qhotkey/qrcode/qcustomplot 内嵌子库 → Windows API）。
- **形态**：参考 Language Lineage 的交互式依赖图——节点按类型分层/着色，可缩放、拖拽、搜索、点击查看详情，离线可打开。

## 2. 范围界定

### 2.1 纳入范围（广度）

| 层 | 内容 | 说明 |
| --- | --- | --- |
| L0 应用 | `translex.exe`（QML 主程序）、`example_translation_plugin.dll` | 顶层产物 |
| L1 源码 | `src/`（main + services 服务层）、`qml/`（QML 界面）、`plugins/`、`tests/` | 源码模块间引用关系 |
| L2 三方库 | FluentUI 1.7.7、QuaZip 1.7.2、zlib 1.3.2.1、Translex_sdk（自研接口库） | git 子模块 + 本地补丁 |
| L3 Qt 模块 | Widgets / Concurrent / Network / Qml / Quick / QuickWidgets / Svg / ShaderTools / PrintSupport / Pdf / Gui / Core / TextToSpeech(可选) / Test(测试) / Multimedia(传递) / Core5Compat(传递) + **Qt 运行时插件**（qwindows 平台 / Schannel TLS / FFmpeg & WMF 多媒体 / 图片格式） | 主 CMakeLists + tests/CMakeLists 实际 find_package 的组件 + windeployqt 部署 |
| L4 内嵌子库 | FluentUI 内嵌：qhotkey（快捷键）、qrcode/qrencode（二维码）、qmlcustomplot/qcustomplot（图表）+ FluentIcons 字体 / i18n 资源 | 源码内嵌，非独立依赖 |
| L5 编译工具链 | CMake 3.21+、MSVC（VS 2026，x64）、clang-cl（Ninja）、MinGW g++（Ninja）、windeployqt、vswhere、lupdate/lrelease（FluentUI i18n） | CMakePresets.json 三个 preset |
| L6 测试/质量 | Qt Test、CTest、16 个测试目标、ripgrep（AI 工具链） | 测试链路 |
| L7 部署/打包 | windeployqt 部署、NSIS（makensis）安装包、ZIP | CPack |
| L8 运行时系统依赖 | Windows 系统 DLL：user32 / gdi32 / kernel32 / advapi32 / shell32 / ole32 / crypt32 / dwmapi / winmm 等（Qt + FluentUI 无边框窗口/快捷键所需）+ DirectX / MSVC CRT / FFmpeg | 运行时 |

### 2.2 额外维度：实现主体（owner，v2 新增）

每个节点归属一个**实现主体**（11 个）：本项目 (Translex) / Qt (The Qt Company) / FluentUI (zhuzichu520) / QuaZip (S. Tachenov) / zlib (madler) / Kitware / Microsoft / LLVM / GCC (MinGW) / FFmpeg 项目 / NSIS 项目。
可视化支持按主体**着色**与**过滤**，与按层级着色并列可选。

### 2.2 排除范围

- `widgets` 分支的旧 QtWidgets 版（本分支仅 QML 版）。
- AI 工具链（opencode、Qt skills/MCP、火绒等）**不作为依赖节点**，仅当「开发环境」备注；ripgrep 同理仅作备注。
- `.opencode/`、`.vscode/` 内的编辑器配置。

### 2.3 深度（递归规则）

对每个节点递归展开其直接依赖，直到叶子节点（系统 DLL / 无子依赖的 Qt 基础模块 / 内嵌库）为止，并**去重合并**（同一依赖被多处引用只画一条边，标注引用来源）。

## 3. 数据采集方法与来源

| 数据 | 来源 | 方法 |
| --- | --- | --- |
| Qt 模块清单 | `CMakeLists.txt`、`tests/CMakeLists.txt`、`third_party/FluentUI/src/CMakeLists.txt` | find_package 组件直读 |
| Qt 传递依赖 | `D:/Software/Qt/6.11.1/msvc2022_64/lib/cmake/*/*Config.cmake`（lib 依赖）、tests 注释（QtPdf→QtMultimedia） | 递归读 .cmake 的 INTERFACE_LINK_LIBRARIES；deps.dev 佐证 |
| 源码级引用 | `src/services/*.cpp/h` 的 `#include` | grep 统计 |
| QML 依赖 | `qml/*.qml` 的 `import` | grep 统计 |
| 三方库版本/许可 | `.gitmodules`、`third_party/*/CMakeLists.txt`、README | 直读 |
| FluentUI 内嵌库 | `third_party/FluentUI/src/` 目录结构（qhotkey / qrcode / qmlcustomplot） | 目录直读 |
| 工具链 | `CMakePresets.json`、README、AGENTS.md | 直读 |
| 运行时 DLL | Qt 部署产物 `build-vs2026-x64/Debug/*.dll`（windeployqt 已部署） | 列目录 |
| 系统 API | FluentUI `qhotkey_win.cpp` / `FluFrameless.cpp` 的 `#include <windows.h>` 及调用 | 源码直读 |

> 网络工具（deps.dev）仅用于**佐证** Qt/zlib/quazip 的已知依赖关系；本项目网络环境不稳，不依赖在线查询，以本地源码为准。

## 4. 依赖分层模型

```
L0 应用产物
 └─ L1 源码（src / qml / plugins / tests，含 services 内部引用）
     └─ L2 三方库（FluentUI / QuaZip / zlib / Translex_sdk）
         └─ L3 Qt 模块（直依赖 + 传递依赖）
             └─ L4 内嵌子库（qhotkey / qrencode / qcustomplot）
                 └─ L5 编译工具链（CMake / MSVC / clang-cl / Ninja …）
                     └─ L6 测试 / L7 打包（CTest / NSIS / windeployqt）
                         └─ L8 运行时系统依赖（user32 / gdi32 / …）
```

每层内部再按「构建时（编译链接）」与「运行时（DLL 加载）」区分边类型。

## 5. 可视化方案选型

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| A. D3.js 力导向图（CDN） | 成熟、效果好 | 国内网络可能加载不了 CDN | 备选 |
| B. 自包含 HTML + 纯 JS 力导向（SVG） | **零依赖、离线双击即开**、完全可控 | 需自实现布局算法 | ✅ 采用 |
| C. ECharts graph（CDN） | 上手快 | 同 A 的网络问题 | 备选 |
| D. Mermaid | 简单 | 节点多时无法交互/性能差 | 仅做文档内示意 |

**选定方案 B**：单文件自包含 HTML，内嵌依赖数据（JS 对象）+ 自研轻量力导向布局（模拟退火 + 弹簧模型）+ SVG 渲染，支持：

1. **分层着色**：按 L0–L8 层上色（配色参考 Fluent/Outlook 视觉语言）
2. **边类型**：编译期（实线）/ 运行期（虚线）/ 源码引用（点线）区分
3. **交互**：拖拽节点、滚轮缩放、平移、搜索/高亮、点击显示详情（版本/许可/说明）
4. **过滤**：按层/按类型开关
5. **递归深度展示**：节点尺寸按「被依赖数/依赖深度」缩放，体现递归展开结果

辅助交付：`docs` 形式的分层清单（Markdown 表格）+ 原始数据 JSON（便于后续维护）。

## 6. 交付物结构（均在 `visualisation/` 内）

```text
visualisation/
├── PLAN.md              # 本文档（规划）
├── data/                # 内容整理（第二步产出）
│   ├── dependencies.json   # 全量依赖数据（节点 + 边，递归展开后）
│   └── layers.md           # 分层清单（Markdown 表格，人类可读）
└── dependency-graph.html  # 可视化（第三步产出，自包含单文件）
```

## 7. 实施步骤与验收

| 步骤 | 产出 | 验收 |
| --- | --- | --- |
| 1 规划（本文档） | PLAN.md | 范围/分层/选型明确 |
| 2 内容整理 | data/dependencies.json + layers.md | 广度覆盖 8 层；深度递归到系统 DLL；JSON 可被可视化直接消费 |
| 3 可视化 | dependency-graph.html | 浏览器打开无报错；节点/边数量与 JSON 一致；交互（缩放/搜索/过滤）可用 |
| 4 校验 | 交叉核对 | 随机抽 10 条边与 CMakeLists/源码 `#include` 一一对应 |

## 8. 备注

- 全部产出为**只读交付物**（文档 + 数据 + HTML），不改动项目源码。
- 数据维护原则：`dependencies.json` 是唯一数据源，HTML 内嵌由它生成；后续依赖变更只需改 JSON 后重新生成 HTML。
