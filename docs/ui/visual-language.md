# 视觉语言（Visual Language）— Outlook / Fluent 2 对齐

> 状态：**v2 定稿 · 日期：2026-08-18**
> v1（2026-08-15）定稿 token 层但**未落地**（审计：73 处 `font.pixelSize` 硬编码、DesignTokens 仅 3 文件引用、文档声明的 `textPrimary/textSecondary/accentBar` 未实现）。
> v2 目标：**统一 FluentUI 设计语言**——利用 Qt 6.11 单例修复把 token 收敛为全局 singleton，逐文件清除硬编码，全部走 token。
> 决策：UI 底座**继续用 FluentUI 1.7.7**（2026-08-18 评估 FluentWinUI3 官方样式有 SplitView fallback Fusion / 图片资源不可定制 / 实验性等硬伤，不迁移）。
> 参照对象：**新版 Outlook for Windows 主界面**（Fluent 2 设计语言；FluentUI 1.7.7 本身是 Fluent 设计语言实现，两者同源）。

## 1. 参照的 Outlook 视觉特征

| 特征 | Outlook 表现 | 本项目的落点 |
| --- | --- | --- |
| 层级 | 应用背景 → 卡片/面板 → 行/控件 三段式，靠底色区分，不靠粗边框 | FluWindow 背景（FluTheme）→ FluFrame/自定义面板 → 列表行 |
| 分隔 | 1px 低对比细线（divider），极少用边框 | `FluTheme.dividerColor` |
| 圆角 | 小圆角：控件 4px，卡片/菜单 8px（最大 8） | `radiusControl`/`radiusCard` 两档 |
| 字阶 | Segoe UI（Windows 原生）：弱 11-12 / 菜单 13 / 正文 14 / 标题 16 / 面板标题 18 / 页头 22 | 六档 token（见 §2.2） |
| 状态色 | 语义色：成功绿 / 错误红 / 警告黄（浅色底+深色字） | `success`/`error`/`warning` token |
| 选中态 | 主题色 3px 左侧指示条 + 极浅主题色底 | 行内强调条已实现，色值入 token |

## 2. 设计 Token（全部集中在 `qml/DesignTokens.qml`）

> **实现方式（v2 变更）**：v1 因 Qt 6.5 的 QML 单例绑定 bug 用普通组件 + 页面内实例化 + delegate 中转。
> **Qt 6.11 已修复该 bug**（2026-08-18 qmltestrunner 实测 3/3 PASS）→ **改回 `pragma Singleton`**：
> - 全局直接 `DesignTokens.fontBody` 引用，无需每文件 `DesignTokens { id: tokens }` 实例化
> - 无需 delegate/内联 Window 的 `page.rowRadius` 中转
> - 旧页面迁移时**移除**实例化与中转，统一走 singleton

### 2.1 颜色

| Token | Light | Dark | 用途 |
| --- | --- | --- | --- |
| `bgApp` | `#F6F6F6` | `#1B1B1B` | 应用/导航背景（Outlook 主背景） |
| `bgCard` | `#FFFFFF` | `#292929` | 编辑器卡片、面板 |
| `bgFloatWindow` | `rgba(255,255,255,0.98)` | `rgba(13,13,13,0.98)` | 浮窗背景（半透明） |
| `bgCardAlt` | `rgba(0,0,0,0.03)` | `rgba(255,255,255,0.05)` | 次级卡片/分区底 |
| `divider` | `rgba(0,0,0,0.06)` | `rgba(255,255,255,0.08)` | 分隔线（与 FluTheme 一致即可） |
| `textPrimary` | `rgba(0,0,0,0.90)` | `rgba(255,255,255,0.90)` | 正文（**v2 补实现**） |
| `textSecondary` | `rgba(0,0,0,0.60)` | `rgba(255,255,255,0.60)` | 次要信息（**v2 补实现**） |
| `success` | `#0F7B0F` | `#6CCB5F` | 成功状态（图标/文字） |
| `error` | `#C42B1C` | `#FF99A4` | 错误状态 |
| `warning` | `#9D5D00` | `#FFD9A1` | 警告状态 |
| `findHighlight` | `rgba(0.95,0.78,0.25,0.16)` | `rgba(0.95,0.78,0.25,0.30)` | 查找命中底（琥珀色） |
| `accentBar` | `FluTheme.primaryColor` | 同 | 当前行指示条、强调元素（**v2 补实现**） |

> 说明：`bgApp`/`text*` 与 FluentUI 默认接近；FluTheme 提供的（`fontPrimaryColor` 等）继续直接用 FluTheme，**不重复造**。

### 2.2 圆角 / 间距 / 字阶

| Token | 值 | 用途 |
| --- | --- | --- |
| `radiusCard` | 8 | 面板、浮窗、大卡片 |
| `radiusControl` | 4 | 行、按钮、输入区 |
| `spacing1/2/3/4/5` | 4 / 8 / 12 / 16 / 24 | 4px 基准网格（16/24 为 v3 新增） |
| `fontCaption` | 12 | 次要/状态栏/批注/时间戳（原 11/12 归此档） |
| `fontMenu` | 13 | 菜单/列表项（Windows 菜单标准 13px，**v2 新增**） |
| `fontBody` | 14 | 正文（编辑器原文默认；原 13/14 归此档） |
| `fontTitle` | 16 | 区块标题（原 15/16 归此档） |
| `fontHeading` | 18 | 面板/章节标题（**v3 新增**，与正文明显区分） |
| `fontDisplay` | 22 | 页面大标题（**v2 新增**） |
| `rowHeight` | 36 | 列表行基准高 |

> 字阶归档（2026-08-18 审计 73 处硬编码）：11→`fontCaption`、12→`fontCaption`、13→`fontMenu`、14→`fontBody`、15→`fontTitle`、16→`fontTitle`、22→`fontDisplay`。
> 例外：`TranslateHomePage.qml:141`/`TranslateSettingsPage.qml:30` 的 `originalFontSize: 14`、`commentFontSize: 12` 是**用户可调编辑器字号（功能配置）**，不走 token。

## 3. 实现方式（v2）

1. `qml/DesignTokens.qml` 改 `pragma Singleton` + 补 `textPrimary`/`textSecondary`/`accentBar`/`fontMenu`/`fontDisplay`。
2. 逐文件清除硬编码（全部走 singleton token）：
   - 字体：73 处 `font.pixelSize` → 对应字阶 token
   - 圆角：6 处 `radius` → `radiusCard`/`radiusControl`
   - 色值：少量 `#hex`/`Qt.rgba` → 对应 token（IconBarButton 4、Main 4、TranslateHomePage 5）
3. 移除旧页面里的 `DesignTokens { id: tokens }` 实例化与 `page.*` 中转（改 singleton 后不再需要）。
4. FluentUI 控件（FluFrame/FluButton 等）继续走 `FluTheme` 默认，不改第三方源码。
5. 颜色尽量动态取 `FluTheme.dark` 分支（深浅色模式跟随现有机制）。

## 4. 本次改动范围（v2）

| 文件 | 改动 |
| --- | --- |
| `qml/DesignTokens.qml` | 改 singleton + 补 5 个 token |
| `qml/TranslateHomePage.qml` | 32 字体 + 2 radius + 5 色值 + 去实例化/中转 |
| `qml/TranslateSettingsPage.qml` | 29 字体 + 1 radius + 去实例化 |
| `qml/TranslatePanelContent.qml` | 4 字体 |
| `qml/HistoryPanel.qml` | 4 字体 + 去实例化 |
| `qml/CommentPanel.qml` | 2 字体 |
| `qml/ChapterPanel.qml` | 1 字体 |
| `qml/ConfigSectionCard.qml` | 1 字体 |
| `qml/IconBarButton.qml` | 2 radius + 4 色值 |
| `qml/Main.qml` | 1 radius + 4 色值 |
| `docs/ui/visual-language.md` | 本文档（v2） |

**明确不做**（留给后续）：导航栏图标密度调整、列表行高重排、深浅色微调、Ribbon 结构重排——均为独立小任务，届时直接消费 token 即可。

## 5. 后续改动必须走 token

新页面/新控件一律：颜色取 `DesignTokens.*`（或 FluTheme），圆角用 `radiusControl/radiusCard`，间距用 4px 网格，字号用六档 token；**禁止新增硬编码色值/圆角/字号**（code review 项）。
