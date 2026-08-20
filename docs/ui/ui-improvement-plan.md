# UI 美术与动画全面改进计划

> 状态：**v3 修订**（2026-08-19 修正与代码现状不符处，开始实施）
> 基于：`translex_ui_v2.png` 实际截图（v1 截图后代码有改动；截图文件在工作区外，见 `%TEMP%\opencode\`）
> 目标：对齐 Outlook for Windows / Microsoft 365 FluentUI 设计语言

---

## 1. 现状诊断（基于 v2 截图）

### 1.1 已完成的改进 ✅（v1→v2 变化）

| 项目 | v1 状态 | v2 状态 | 评价 |
|------|---------|---------|------|
| 三栏背景色分层 | ❌ 几乎无色差 | ✅ 浅灰→中灰→白 三段灰阶 | **已解决** |
| 图标栏选中态 | ⚠️ 蓝色覆盖图标 | ✅ 左缘纯蓝竖条（accent bar） | **已正确** |
| 状态栏 | ❌ 不存在 | ✅ 含状态文字+文档名+统计 | **已存在** |
| Ribbon 分组 | ⚠️ 无分隔线 | ✅ 有分组分隔线 | **已存在** |
| 图标栏收起展开 | ❌ 无 | ✅ 胶囊形悬浮展开按钮 | **已实现** |
| 窗口几何持久化 | ❌ 无 | ✅ 位置/大小/最大化状态记忆 | **已实现** |
| 侧边栏宽度持久化 | ⚠️ 有 bug | ✅ 拖动释放时保存，启动时恢复 | **已修复** |

### 1.2 仍然需要改进的部分

| 问题 | 严重度 | 现状 | 目标 |
|------|--------|------|------|
| **无阴影层级** | ~~🔴~~ | 曾计划 MultiEffect 阴影，**实测否决**（非整数 DPI 下 layer 离屏渲染文字模糊，2026-08-19）；三栏靠背景色分层 + 分隔线已够 | 不做阴影 |
| **暗色模式未全组件验证** | 🟡 | token 已全部带 dark 分支（DesignTokens.qml），但未逐组件截图验证 | 全组件暗色验证 + 对比度检查 |
| **章节标题层级弱** | 🟡 | 加粗但字号与正文接近（~14px vs 14px） | 应为 18px + 加粗 + 颜色区分 |
| **列表项无动画** | 🟡 | 历史/批注/章节列表无 add/move/displaced 动画 | 加入 200ms 淡入+滑入 |
| **交互动画缺失** | 🟡 | hover/选中态无过渡动画（瞬间切换） | 加 Behavior on color (120ms) |
| **卡片圆角不统一** | 🟡 | 编辑器 FluFrame 有圆角，侧边栏/状态栏部分区域未走 token | 全局 4px/8px 两档（radiusControl/radiusCard） |
| **无焦点环** | 🟡 | 键盘导航不可见 | 2px 外发光 |
| **Ribbon 按钮缺图标** | 🟡 | 纯文字按钮 | 应配图标增强扫描性 |
| **侧边栏空状态** | 🟡 | 翻译历史为空时可能无引导 | 应显示"暂无翻译历史" |

### 1.3 不再需要改进的项（已解决）

- ~~三栏背景色分层~~ — 已有灰阶差异
- ~~图标栏选中态~~ — 已是 accent bar
- ~~状态栏~~ — 已存在且内容完整
- ~~Ribbon 分组分隔线~~ — 已有
- ~~图标栏收起展开~~ — 已有悬浮按钮
- ~~Mica 材质~~ — 不做（Qt 6 实现复杂，收益有限）

---

## 2. 改进方案（4 大类，20+ 子项）

---

### 2.1 阴影与材质层级

**目标**：靠阴影区分面板层级，营造 FluentUI 的深度感。

#### 2.1.1 卡片阴影
- **结论（2026-08-19 实测否决）**：`MultiEffect`（`QtQuick.Effects`，Qt 6.7+ 原生）技术上可行，但 `layer.enabled` 离屏渲染在**非整数 DPI 缩放（1.25）下编辑器/浮窗文字明显模糊**（用户实测），且含滚动 ListView 的卡片每帧重绘开销大。
- 维持现状：三栏靠背景色分层 + 1px 分隔线区分层级（已够），**不做卡片阴影**。

#### 2.1.2 分隔线增强
- 图标栏右侧：1px `divider`（已有，确认）
- 侧边栏与编辑器之间：靠背景色差区分（已有，确认）
- Ribbon 标签栏与功能区之间：1px `divider`（已有）
- 编辑器与状态栏之间：1px `divider`（已有）

#### 2.1.3 浮窗
- 同 2.1.1：不做阴影（layer 在非整数 DPI 下文字模糊，实测否决）。浮窗已有半透明背景 + 圆角 + 边框区分层级。

---

### 2.2 暗色模式验证

**目标**：确保 `FluTheme.dark` 切换后所有组件正常显示。

#### 2.2.1 需要验证的文件
- `Main.qml`：三栏背景色（已走 token，确认 dark 分支）
- `TranslateHomePage.qml`：编辑器背景、行选中色、批注色
- `TranslateSettingsPage.qml`：设置卡片背景
- 各 Panel 文件：列表项背景色
- `IconBarButton.qml`：hover/active 颜色（已走 token）

#### 2.2.2 验证清单
- [ ] 切换 FluTheme.dark 后，所有区域无纯白/纯黑残留
- [ ] 文字对比度 ≥ 4.5:1（WCAG AA）
- [ ] 主题色在深色下自动变亮（FluTheme.primaryColor 已处理）
- [ ] 状态栏在深色下可读
- [ ] 浮窗在深色下正常

#### 2.2.3 可能需要修改的地方
- 编辑器背景色：`bgCard` token 已有 dark 分支（`#292929`），确认
- 行选中色：已有 dark 分支（`0.22` opacity），确认
- 批注文字色：使用 `FluTheme.primaryColor`，dark 下自动变亮，确认

---

### 2.3 交互动画

**目标**：所有状态切换有平滑过渡，消除瞬切感。

#### 2.3.1 背景色过渡（全局）
```qml
// 所有交互态 Rectangle 加 Behavior
Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
```
**影响文件**：
- `IconBarButton.qml`：hover/active 颜色
- `TranslateHomePage.qml` 行 delegate：hover/选中/当前行颜色
- `Main.qml` SplitView handle：hover 颜色

#### 2.3.2 图标栏 hover 微缩放
```qml
// IconBarButton.qml
scale: mouse.containsMouse ? 1.08 : 1.0
Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
```

#### 2.3.3 选中指示条动画
```qml
// IconBarButton.qml 的 accent bar
Rectangle {
    width: control.active ? 3 : 0
    Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
}
```

#### 2.3.4 编辑器当前行指示条动画
```qml
// TranslateHomePage.qml 行 delegate 的 accent bar
Rectangle {
    visible: index === page.currentLine
    width: index === page.currentLine ? 3 : 0
    Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
}
```

#### 2.3.5 列表项动画
```qml
// HistoryPanel.qml, CommentPanel.qml, ChapterPanel.qml 的 ListView
ListView {
    add: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
            NumberAnimation { property: "y"; from: 10; to: 0; duration: 200; easing.type: Easing.OutCubic }
        }
    }
    move: Transition {
        NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
    }
    displaced: Transition {
        NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
    }
}
```
> **性能铁律**：受限模式（超 5 万行/200MB，见 docs/services/large-file.md）下
> 必须禁用这些动画——批量导入/删除时大量 delegate 同时动画会卡死 UI。
> 实现：`add/move/displaced` 的 `enabled` 绑定到非受限模式标志。

#### 2.3.6 右键菜单弹出
```qml
// TranslateHomePage.qml lineMenu
Rectangle {
    id: menuCard
    scale: lineMenu.visible ? 1.0 : 0.95
    opacity: lineMenu.visible ? 1.0 : 0.0
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    Behavior on opacity { NumberAnimation { duration: 120 } }
}
```

#### 2.3.7 翻译进度过渡
```qml
// TranslateHomePage.qml 进度条区域
RowLayout {
    visible: page.translating
    opacity: page.translating ? 1 : 0
    y: page.translating ? 0 : 8
    Behavior on opacity { NumberAnimation { duration: 150 } }
    Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
}
```

#### 2.3.8 状态栏状态切换
```qml
// 状态文字变化时 opacity 过渡
FluText {
    id: statusLabel
    opacity: text !== "" ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 150 } }
}
```

---

### 2.4 Token 优化与视觉细节

**目标**：配色更细腻，间距/圆角/字体层级更统一。

#### 2.4.1 圆角统一
```qml
// DesignTokens.qml 新增
readonly property int radiusSmall: 2    // 指示条、分隔线端点
readonly property int radiusControl: 4  // 按钮、输入框（已有）
readonly property int radiusCard: 8     // 卡片、面板、菜单（已有）
```
- 状态栏 Rectangle：加 `radius: tokens.radiusCard`（已有，确认）

#### 2.4.2 字体层级强化
```qml
// DesignTokens.qml 新增
readonly property int fontHeading: 18  // 章节标题专用
```
- 章节标题从 `fontTitle(16)` → `fontHeading(18)` + 加粗

#### 2.4.3 间距微调
```qml
// DesignTokens.qml 新增
readonly property int spacing4: 16   // 大间距
readonly property int spacing5: 24   // 区块间距
```

#### 2.4.4 焦点环
```qml
// 全局键盘导航焦点环
Rectangle {
    visible: control.activeFocus
    anchors.fill: parent
    anchors.margins: -2
    radius: 4
    color: "transparent"
    border.color: FluTheme.primaryColor
    border.width: 2
    opacity: 0.4
}
```

---

## 3. 实施分组

### 第一批：交互动画（P0，1-2 天）
| 改进项 | 文件 | 复杂度 |
|--------|------|--------|
| 背景色 Behavior 过渡 | IconBarButton, TranslateHomePage, Main (~6 文件) | 低 |
| IconBarButton hover 微缩放 | IconBarButton.qml | 低 |
| accent bar 宽度动画 | IconBarButton.qml, TranslateHomePage.qml | 低 |
| 列表项 add/move/displaced 动画 | 3 个 Panel 文件 | 低 |
| 右键菜单弹出动画 | TranslateHomePage.qml | 低 |
| 翻译进度过渡 | TranslateHomePage.qml | 低 |

### 第二批：暗色模式验证（P1，1 天）
| 改进项 | 文件 | 复杂度 |
|--------|------|--------|
| 全组件暗色验证 | 全部 QML 文件 | 中 |
| 文字对比度检查 | 全局 | 低 |
| 深色下主题色变亮验证 | FluTheme 已处理 | 验证 |

### 第三批：Token 与视觉细节（P1，1 天）
| 改进项 | 文件 | 复杂度 |
|--------|------|--------|
| 章节标题字号强化 | TranslateHomePage.qml | 低 |
| 圆角统一验证 | 全局 | 低 |
| 焦点环 | 全局 | 中 |

### 第四批：~~阴影层级~~（P2，已否决）
- 2026-08-19 实测：MultiEffect 阴影在非整数 DPI 下文字模糊，取消本批（见 §2.1.1）

### 第五批：Ribbon 美化（P3，1 天）
| 改进项 | 文件 | 复杂度 |
|--------|------|--------|
| 按钮加图标 | TranslateHomePage.qml | 低 |
| 标签选中态加粗 | TranslateHomePage.qml | 低 |

---

## 4. 验证清单

- [ ] 构建通过（`cmake --build build-vs2026-x64 --config Debug`）
- [ ] 测试通过（`ctest --test-dir build-vs2026-x64 -C Debug`）
- [ ] 浅色模式：三栏背景色有明显色差（已有）
- [ ] 深色模式：所有区域无纯白/纯黑残留
- [ ] 图标栏：选中态为左侧 accent bar（已有）
- [ ] 编辑器：当前行指示条有宽度动画
- [ ] 列表：添加/删除有淡入淡出动画
- [ ] 受限模式（≥5 万行）：列表动画自动禁用，操作无卡顿
- [ ] 右键菜单：弹出有缩放+淡入动画
- [ ] 状态栏：存在且显示正确信息（已有）
- [ ] 章节标题：字号 18px + 加粗，与正文有明显区分
- [ ] 卡片：有微阴影层级（❌ 已否决：非整数 DPI 下 layer 文字模糊）

---

## 5. 不做的事（明确排除）

- ❌ 不引入 Mica/Acrylic 材质（Qt 6 实现复杂，收益有限）
- ❌ 不改 FluentUI 第三方源码
- ❌ 不改子模块
- ❌ 不引入新依赖（`QtQuick.Effects.MultiEffect` 是 Qt 6.7+ 原生模块，允许；不引入 Qt5Compat）
- ❌ 不做过度动画（每个 Behavior 控制在 120-200ms）
- ❌ 三栏分层已解决，不再重复
- ❌ 状态栏已存在，不再重复
- ❌ 列表动画不用于受限模式/大列表 delegate（性能铁律）
