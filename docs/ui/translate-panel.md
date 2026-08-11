# 翻译面板（Ribbon 功能区 + 浮窗双呈现）设计文档

> 状态：已实现（2026-08-11 修订：移除 docked、浮窗交互修复、位置不钳制）
> 关联：`qml/TranslateHomePage.qml`、`qml/TranslatePanelContent.qml`、`qml/TranslateSettingsPage.qml`、`src/services/config/ui.json`

## 1. 定位（最终版）

翻译服务同时提供两种呈现，在「翻译」Ribbon 标签上用「浮窗」开关切换：

| 模式 | 表现 | 配置值 |
| --- | --- | --- |
| `ppt`（默认） | Ribbon「翻译」标签功能区（PPT 式） | `ui.translatePanelMode = ppt` |
| `floating` | 页面内浮动面板，可自由拖动 | `ui.translatePanelMode = floating` |

旧版 `docked`（停靠面板）已移除，历史配置自动迁移为 `ppt`。

## 2. 浮窗实现：页面内自绘浮层（不用 Popup）

NoStack 页面模式下 `Popup.window` 为 null，Popup 的 background/contentItem 会错位渲染、按钮凸出、✕ 点不到。
因此浮窗改用页面内浮层：

```
FluContentPage#page
└── panelOverlay (Item, z:1000, anchors.fill: parent, visible: floating && panelShown)
    └── panelCard (Rectangle, x/y = panelX/panelY, width 300)
        ├── 标题栏 Rectangle（「翻译工具」+ ✕ + dragArea）
        │   ├── closeBtn (FluButton, z:2 高于 dragArea)
        │   └── dragArea (MouseArea, anchors.fill: parent)
        ├── FluDivider
        └── TranslatePanelContent（复用组件）
```

- 标题栏 `dragArea`：`onPressed` 记录起点，`onPositionChanged` 累加位移到 `page.panelX/Y`，`onReleased` 持久化。
- ✕ 必须是标题栏**直接子项**且 `z:2 > dragArea z:0`（同父级比较才生效），否则被 dragArea 拦截。

## 3. 位置策略（不钳制）

- **不限制范围**：用户可自由把浮窗拖出页面/屏幕任意位置（无 clamp）。
- 启动恢复：仅当数值 `isFinite` 时恢复；非法值（NaN/历史坏值）回退默认 `16,170`；合法负坐标原样恢复。
- 持久化到 `ui.translatePanelX/Y`（内部项，设置页已 `excludeKeys` 隐藏）。

## 4. 组件 TranslatePanelContent.qml（复用）

- 输入：`translating / progressDone / progressTotal / hasSelection / titleText`
- 信号：`translateCurrentRequested / translateAllRequested / translateSelectedRequested / cancelRequested`
- **关键坑**：
  - `implicitWidth` 固定 268（不绑定内部列 implicitWidth，否则被文本撑到 ~378 凸出卡片）。
  - 按钮/文本 `Layout.minimumWidth: 0` + `Layout.maximumWidth: root.width - 32`，避免 implicitWidth 撑破布局。
  - 快捷键提示用单行 `elide`（`wrapMode: WordWrap` 的 implicitHeight 按单行算但渲染两行，会溢出浮窗底部）。

## 5. 配置项（ui.json）

| key | 类型 | 默认 | 说明 |
| --- | --- | --- | --- |
| `translatePanelMode` | enum | `ppt` | `ppt`=Ribbon 标签（默认）；`floating`=浮窗 |
| `translatePanelVisible` | bool | `false` | 启动时是否显示浮窗 |
| `translatePanelX/Y` | number | `16/170` | 内部记忆拖动位置（internal，设置页隐藏） |

## 6. 模式切换与持久化

- 「翻译」标签 `FluToggleSwitch` 浮窗：`checked = floating && panelShown`；用 `clickListener` 直接切换状态（若用 toggled 会被 checked 赋值干扰，出现"点了没反应"）。
- 切换时写 `translatePanelMode` + `translatePanelVisible`。
- ✕ 关闭：`panelShown=false` + 持久化 `translatePanelVisible=false`。
- 启动：`panelMode` 读配置（`docked` 旧值迁移为 `ppt`），`panelShown` 读 `translatePanelVisible`。

## 7. 设置页

「翻译面板」折叠区只保留「启动时显示」（bool）；模式与位置不出现在设置页（`ConfigSectionCard` 的 `excludeKeys` 隐藏内部项）。

## 8. 验证记录

- 拖动标题栏、✕ 关闭、三个翻译按钮均生效（日志逐项确认）。
- 位置完全自由：可拖出屏幕并原样恢复，无钳制。
- 启动仅剩 Qt 6.5.3 已知的 frameless 警告；`FluWindow addWindow/removeWindow` TypeError 已通过 `FluWindow.qml` 防御性调用消除。
