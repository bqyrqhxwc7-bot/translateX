# Translex 依赖可视化

全链路递归依赖图（源码 → 三方库 → Qt 模块 → 内嵌库 → 工具链 → 测试 → 打包 → 运行时系统依赖）。

## 快速开始

直接双击打开 **`dependency-graph.html`**（单文件、零外部依赖、离线可用）。

## 交付物

| 文件 | 说明 |
| --- | --- |
| `dependency-graph.html` | **可视化成品**（自包含 HTML，数据内嵌） |
| `data/dependencies.json` | 全量依赖数据（178 节点 / 511 边 / 11 实现主体，**唯一数据源**） |
| `data/layers.md` | 人类可读的分层清单 |
| `PLAN.md` | 规划文档（范围/分层模型/选型/验收） |
| `generate.mjs` | 重新生成脚本（JSON → HTML） |
| `augment.mjs` | 数据增强脚本（补充节点 + 分配 owner，可复用） |
| `augment-deep.mjs` | **深度增强脚本 v3**（Qt 内嵌第三方库 + 系统组件细化 + 插件补齐，网络调研数据注入） |
| `complete-leaves.mjs` | 叶子补边脚本（幂等补齐叶子节点真实依赖，85→6 个真叶子） |
| `parse-imports.mjs` | PE 导入表解析工具（验证 DLL 真实依赖，如 FFmpeg→ws2_32/secur32/bcrypt） |
| `template.html` | HTML 模板（生成脚本的源） |

## 交互说明

- **滚轮**：缩放 · **空白拖拽**：平移 · **节点拖拽**：重排
- **点击节点**：查看详情（类型/层/实现主体/版本/许可/描述/依赖跳转）
- **悬停节点**：高亮相连边 + tooltip
- **搜索框**：实时高亮，`Enter` 定位并居中
- **着色模式**（顶栏）：按**层级** / 按**实现主体**（11 个主体：本项目 / Qt / FluentUI / QuaZip / zlib / Kitware / Microsoft / LLVM / GCC / FFmpeg / NSIS）
- **▶ 依赖动画**（顶栏，绿色按钮）：从 `translex.exe` 出发 BFS 逐节点点亮全部 178 个依赖，直观演示「递归查找依赖」过程；底部控制条支持 **播放/暂停、上一步/下一步、进度条拖拽、0.5×–4× 变速、实时步骤说明**（如「第 9/178 步 · 点亮 serviceregistry（L1 源码）｜ 源码引用：translex.exe、src/main_qml.cpp」）
- **右上「图例与过滤」**：按 9 个层级 / 11 个实现主体 / 6 种边类型过滤
- 节点颜色 = 当前着色模式；边样式 = 类型（源码点线 / 编译实线 / 运行虚线 / 工具 / 测试 / 打包）

## 数据维护

依赖有变更时（新增 Qt 模块、改 CMakeLists、加服务）：

1. 编辑 `data/dependencies.json`（节点/边；新增节点可补 `owner` 字段）
2. 运行 `node augment.mjs`（补充遗漏节点 + 按规则补 owner）
3. 运行 `node complete-leaves.mjs`（为叶子节点补齐真实依赖，幂等）
4. 运行 `node generate.mjs`
5. 刷新 `dependency-graph.html`

> 数据采集依据：`CMakeLists.txt`、`tests/CMakeLists.txt`、`src/services/**` 的 `#include`、`qml/**` 的 `import`、Qt 6.11.1 的 `*Dependencies.cmake` 模块依赖矩阵、`build-vs2026-x64/Debug` 的部署 DLL 与运行时插件清单；二进制真实依赖可用 `node parse-imports.mjs <dll>` 验证。
