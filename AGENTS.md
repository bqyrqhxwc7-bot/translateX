# AGENTS.md — Copilot 协作规范

本文件是 GitHub Copilot 在本仓库工作时的行为准则。**每次会话开始先读本文件。**
> **接手必读**：`docs/HANDOVER.md`（当前功能状态 / 路线图 / 架构铁律 / 踩坑总纲——opencode 或任何新会话先读它，再读 `docs/ARCHITECTURE.md`）。

## 1. 核心原则（按优先级）

1. **文档先行**：任何设计/重构/新功能，先写 `docs/` 下的设计文档，确认后再写实现代码。代码与文档必须同步更新。
2. **重大决策先 Ask 用户**：涉及架构方向、接口变更、破坏性重构、依赖引入、许可协议的选择，**必须先问用户**再动手，不要自作主张。
3. **多用 Todo 跟踪**：多步骤任务必须用 `manage_todo_list` 建立清单，每完成一项立即更新状态，保持可见。
4. **及时记录设计**：重要的架构决策、踩坑、约定写入 `docs/` 或仓库记忆（`/memories/repo/`），供后续会话延续。
5. **小而可验证**：每次改动可编译、可测试；不一次性做超大改动。

## 2. 架构约定

- **前后端分离**：UI 是 QML（FluentUI），业务逻辑在 `src/services/` 的 C++ 服务层。
- **可插拔**：新能力优先实现为 service（见 `docs/services/SERVICE-ARCHITECTURE.md`），不往 `mainwindow` 里堆代码。
- **接口稳定**：`IService` / `ITranslationBackend` 等公共接口定稿后不轻易改；新能力用新增方法（带默认实现）。
- **先功能后优化**：优先打通功能，性能/质量优化在功能可用后迭代。

## 3. 翻译服务定位（不可偏离）

- **更好的翻译质量**：上下文感知、术语一致、质量自检。
- **为用户减少成本**：缓存、模型分级、智能分块、失败降级。
- 任何翻译相关改动必须对照 `docs/services/translation-service.md` 的度量指标。

## 4. 技术约束

- Qt 6.5.3，C++17，CMake（`qt_add_*` 系列命令，不用 qt5 宏）
- FluentUI 1.7.7（`third_party/FluentUI`，BSD-3-Clause，静态库）
- 大文件性能：虚拟化渲染（ListView + 懒加载模型），禁止全量刷新
- 敏感信息：一律走 `SecureStorage`，禁止明文落盘
- 新代码必须可被 `tests/` 下的单元测试覆盖

## 5. 工作流程

```
1. 读 AGENTS.md（本文件）→ 了解约束
2. 读相关 docs/ 文档 → 理解设计
3. 重大决策 → ask 用户
4. 建 Todo 清单
5. 小步实现 + 构建 + 测试
6. 记录决策/踩坑到 docs/ 或记忆
7. 每步汇报，保持透明
```

## 6. 完成标准

- 构建通过（`cmake --build build-vs2026-x64 --config Debug`）
- 相关测试通过（`ctest --test-dir build-vs2026-x64 -C Debug`）
- 文档与代码同步（改了实现必须改对应 `.md`）
- Todo 全部完成并更新
- 向用户汇报变更 + 影响 + 下一步
