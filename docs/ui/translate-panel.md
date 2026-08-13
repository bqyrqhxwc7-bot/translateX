# 翻译面板（Ribbon 功能区 + 浮窗双呈现）设计文档

> 状态：已实现（2026-08-13 修订：浮窗为真独立 Window，位置钳制到桌面 + 启动延迟显示 + NaN 防御）
> 关联：`qml/TranslateHomePage.qml`、`qml/TranslatePanelContent.qml`、`qml/TranslateSettingsPage.qml`、`src/services/config/ui.json`

## 1. 定位（最终版）

翻译服务同时提供两种呈现，在「翻译」Ribbon 标签上用「浮窗」开关切换：

| 模式 | 表现 | 配置值 |
| --- | --- | --- |
| `ppt`（默认） | Ribbon「翻译」标签功能区（PPT 式） | `ui.translatePanelMode = ppt` |
| `floating` | 独立悬浮窗，可自由拖动/跨屏 | `ui.translatePanelMode = floating` |

旧版 `docked`（停靠面板）已移除，历史配置自动迁移为 `ppt`。

## 2. 浮窗实现：真独立 Window（Qt.Tool）

NoStack 页面模式下 `Popup.window` 为 null，Popup 的 background/contentItem 会错位渲染；且页面内浮层会被导航视图/窗口边界裁剪，拖出页面即失效（用户确认）。浮窗因此改为**真独立 Window**，可拖到屏幕任意位置/跨屏：

```
Window#floatWindow (Qt.Tool | FramelessWindowHint, transientParent: mainWindow)
├── Rectangle（自绘卡片：圆角 8 + 边框 + 背景，保持 Fluent 观感）
│   ├── 标题栏 Rectangle（30px）：「翻译工具」标题 + dragArea
│   │   └── dragArea (MouseArea) → onPressed: floatWindow.startSystemMove()
│   └── TranslatePanelContent（复用组件）
```

- `flags: Qt.Tool | Qt.FramelessWindowHint`：无任务栏、经 `transientParent` 跟随主窗口；去掉系统原生标题栏，自绘卡片保持 Fluent 观感。
- `width: 300`；`height: Math.max(220, floatCol.implicitHeight + 4)`。
- **无 ✕ 按钮**：关闭/显示统一经 Ribbon「翻译」标签的浮窗开关（用户确认）。
- `transientParent: mainWindow`（`src/main_qml.cpp` 暴露占位属性，QML 装配时绑定）。

## 3. 位置策略（钳制到桌面 + NaN 防御 + 节流保存）

- **钳制到桌面内**：浮窗无任务栏（Qt.Tool），若拖出屏幕会找不到 → 恢复时 `Math.max(sx, Math.min(px, sx + sw - 300))` 钳制；非法值回退默认右上角（`sx+sw-340, sy+80`）。
- **NaN 防御（位置失效根因）**：窗口未完全映射时 `screen.virtualX/virtualWidth` 返回 `undefined`，直接参与钳制运算产生 NaN 导致位置不生效 → 一律 `Number(scr && scr.virtualX) || 0` 兜底。
- **实时保存（节流）**：`onXChanged/onYChanged` → `floatPosSaveTimer`（350ms）→ `saveFloatWindowPos()`；保存前 `isFinite(x) && isFinite(y) && x > -10000 && y > -10000` 守卫，坏值不落盘。
- **恢复抑制保存**：`restoringPos` 标志在恢复期间置位，避免把未映射时的无效坐标存回 config 覆盖好值。
- **恢复时机**：`onVisibleChanged(true)` 时先 `show()` 确认（Qt.Tool show 竞态兜底），再 `floatPosRestoreTimer`（120ms，**Timer 而非 `Qt.callLater`**——NoStack 下 callLater 不保证执行）恢复位置；`Component.onCompleted` 也重启该 Timer 兜底。
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
- 关闭：`onClosing` 隐藏而非销毁（`visibility = Window.Hidden` + `close.accepted = false`），保留对象供开关再次显示。
- **启动显示（竞态修复）**：主窗口未完全显示时，Qt.Tool + transientParent 立即 show 会竞态失败（症状：设置了启动显示但浮窗不出现，切一下页面才出现）→ 页面 `onCompleted` 先 `panelShown = false`，若 floating 模式则启动 `floatShowTimer`（350ms）后再置 `true`。

## 7. 设置页

「翻译面板」折叠区只保留「启动时显示」（bool）；模式与位置不出现在设置页（`ConfigSectionCard` 的 `excludeKeys` 隐藏内部项）。

## 8. 验证记录

- 拖动标题栏（startSystemMove）、Ribbon 开关显示/隐藏均生效。
- 位置记忆：拖动 → 重启/切页均正确恢复（日志确认 restore 读到正确坐标）；坏值/首次启动回退默认右上角。
- 启动显示：延迟 350ms + show 确认后，多次重启均能自动出现（2026-08-13 修复前偶发不显示）。
- 已知无害警告：Qt 6.5.3 frameless 警告（AppGuard 已过滤）；`FluMenuItem` "Created graphical object was not placed in the graphics scene"（最近文件菜单，功能正常）。
