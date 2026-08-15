# 视觉语言（Visual Language）— Outlook / Fluent 2 对齐

> 状态：v1 定稿 · 日期：2026-08-15
> 决策：布局**不动**（用户认可当前三区结构），本次只统一**视觉语言**；核心产出 = 集中 token 层，为后续功能（pdf 导入、大文件降级、术语表 UI）留出样式入口。
> 参照对象：**新版 Outlook for Windows 主界面**（Fluent 2 设计语言；FluentUI 1.7.7 本身是 Fluent 设计语言实现，两者同源，只需微调对齐）。

## 1. 参照的 Outlook 视觉特征

| 特征 | Outlook 表现 | 本项目的落点 |
| --- | --- | --- |
| 层级 | 应用背景 → 卡片/面板 → 行/控件 三段式，靠底色区分，不靠粗边框 | FluWindow 背景（FluTheme）→ FluFrame/自定义面板 → 列表行 |
| 分隔 | 1px 低对比细线（divider），极少用边框 | `FluTheme.dividerColor` |
| 圆角 | 小圆角：控件 4px，卡片/菜单 8px（最大 8） | 自定义 Rectangle 的 `radius` 统一到 4/8 两档 |
| 字阶 | Segoe UI（Windows 原生）：正文 14、次要 12、弱 10-12 | `font.pixelSize` 只用 12/14/16 三档 |
| 状态色 | 语义色：成功绿 / 错误红 / 警告黄（浅色底+深色字） | 现有 `Qt.rgba(0.10,0.55,0.34)` 等硬编码迁移到 token |
| 选中态 | 主题色 3px 左侧指示条 + 极浅主题色底 | 行内强调条已实现（radius 1.5），色值入 token |

## 2. 设计 Token（全部集中在 `qml/DesignTokens.qml` 单例）

### 2.1 颜色

| Token | Light | Dark | 用途 |
| --- | --- | --- | --- |
| `bgApp` | `#F6F6F6` | `#1B1B1B` | 应用/导航背景（Outlook 主背景） |
| `bgCard` | `#FFFFFF` | `#292929` | 编辑器卡片、面板、浮窗 |
| `bgCardAlt` | `rgba(0,0,0,0.03)` | `rgba(255,255,255,0.05)` | 次级卡片/分区底 |
| `divider` | `rgba(0,0,0,0.06)` | `rgba(255,255,255,0.08)` | 分隔线（与 FluTheme 一致即可） |
| `textPrimary` | `rgba(0,0,0,0.90)` | `rgba(255,255,255,0.90)` | 正文 |
| `textSecondary` | `rgba(0,0,0,0.60)` | `rgba(255,255,255,0.60)` | 次要信息 |
| `success` | `#0F7B0F` | `#6CCB5F` | 成功状态（图标/文字） |
| `error` | `#C42B1C` | `#FF99A4` | 错误状态 |
| `warning` | `#9D5D00` | `#FFD9A1` | 警告状态 |
| `findHighlight` | `rgba(0.95,0.78,0.25,0.16)` | `rgba(0.95,0.78,0.25,0.30)` | 查找命中底（琥珀色） |
| `accentBar` | `FluTheme.primaryColor` | 同 | 当前行指示条、强调元素 |

> 说明：`bgApp`/`text*` 与 FluentUI 默认接近，token 先落自定义元素；FluTheme 提供的（`fontPrimaryColor` 等）继续直接用 FluTheme，**不重复造**。

### 2.2 圆角 / 间距 / 字阶

| Token | 值 | 用途 |
| --- | --- | --- |
| `radiusCard` | 8 | 面板、浮窗、大卡片 |
| `radiusControl` | 4 | 行、按钮、输入区 |
| `spacing1/2/3` | 4 / 8 / 12 | 4px 基准网格 |
| `fontBody` | 14 | 正文（编辑器原文默认） |
| `fontCaption` | 12 | 次要/状态栏/批注 |
| `rowHeight` | 36 | 列表行基准高 |

## 3. 实现方式

1. 新增 `qml/DesignTokens.qml`（`pragma Singleton`，随 `qt_add_qml_module` 自动注册为 `Translex` 模块单例）。
2. 页面里**自定义**的硬编码值全部改为引用 token：`TranslateHomePage.qml` 的状态色（成功/错误）、查找琥珀、层底色、`radius` 6→`radiusCard` 8、4→`radiusControl`、编辑器卡片/浮窗背景。
3. FluentUI 控件（FluFrame/FluButton 等）继续走 `FluTheme` 默认，不改第三方源码——升级 FluentUI 时零成本。
4. 颜色尽量动态取 `FluTheme.dark` 分支（深浅色模式跟随现有机制）。

## 4. 本次改动范围（v1）

| 文件 | 改动 |
| --- | --- |
| `qml/DesignTokens.qml` | 新增 token 单例 |
| `CMakeLists.txt` | QML_FILES 注册 DesignTokens.qml |
| `qml/TranslateHomePage.qml` | 状态色/层底/radius/查找色 迁移到 token |
| `docs/ui/visual-language.md` | 本文档 |

**明确不做**（留给后续）：导航栏图标密度调整、列表行高重排、深浅色微调、Ribbon 结构重排——均为独立小任务，届时直接消费 token 即可。

## 5. 后续改动必须走 token

新页面/新控件一律：颜色取 `DesignTokens.*`（或 FluTheme），圆角用 `radiusControl/radiusCard`，间距用 4px 网格；**禁止新增硬编码色值/圆角**（code review 项）。
