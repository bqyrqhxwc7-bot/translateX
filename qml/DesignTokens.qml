// 视觉语言 token（Outlook / Fluent 2 对齐，见 docs/ui/visual-language.md）
// 规则：新页面一律引用本组件或 FluTheme，禁止硬编码色值/圆角/字号。
// 实现：普通组件 + 页面内实例化（DesignTokens { id: tokens }）。
// 注意：不用 pragma Singleton——Qt 6.11 的 QML singleton 属性绑定依赖其他 singleton
//（如 FluTheme）时失效（undefined，qmltestrunner 实测复现，2026-08-18 踩过，
// 详见 HANDOVER.md §6）；普通组件依赖 C++ singleton（FluTheme）正常。
// delegate/内联组件内不能直接访问实例 id，需经页面属性中转或 delegate 内实例化。
import QtQuick
import FluentUI

QtObject {
    // ---- 背景层级 ----
    // 应用/导航背景（FluTheme.windowBackgroundColor 即应用级背景）
    readonly property color bgApp: FluTheme.windowBackgroundColor
    // 卡片/面板/浮窗背景
    readonly property color bgCard: FluTheme.dark ? "#292929" : "#FFFFFF"
    // 浮窗背景（半透明，与卡片区分层级）
    readonly property color bgFloatWindow: FluTheme.dark
                                            ? Qt.rgba(0.13, 0.13, 0.13, 0.98)
                                            : Qt.rgba(1, 1, 1, 0.98)
    // 模态遮罩（页面内浮层/弹窗）
    readonly property color overlayMask: Qt.rgba(0, 0, 0, 0.35)
    // 次级卡片/分区底
    readonly property color bgCardAlt: FluTheme.dark
                                        ? Qt.rgba(1, 1, 1, 0.05)
                                        : Qt.rgba(0, 0, 0, 0.03)
    // 分隔线
    readonly property color divider: FluTheme.dividerColor
    // 导航/图标栏背景
    readonly property color bgNav: FluTheme.dark
                                    ? Qt.rgba(1, 1, 1, 0.04)
                                    : Qt.rgba(0, 0, 0, 0.03)
    // 交互态背景（hover / 选中）
    readonly property color bgHover: FluTheme.dark
                                      ? Qt.rgba(1, 1, 1, 0.05)
                                      : Qt.rgba(0, 0, 0, 0.04)
    readonly property color bgActive: FluTheme.dark
                                       ? Qt.rgba(1, 1, 1, 0.10)
                                       : Qt.rgba(0, 0, 0, 0.06)

    // ---- 文本色（Fluent 2）----
    readonly property color textPrimary: FluTheme.dark
                                          ? Qt.rgba(1, 1, 1, 0.90)
                                          : Qt.rgba(0, 0, 0, 0.90)
    readonly property color textSecondary: FluTheme.dark
                                            ? Qt.rgba(1, 1, 1, 0.60)
                                            : Qt.rgba(0, 0, 0, 0.60)

    // ---- 语义状态色（Fluent 2）----
    readonly property color success: FluTheme.dark ? "#6CCB5F" : "#0F7B0F"
    readonly property color error: FluTheme.dark ? "#FF99A4" : "#C42B1C"
    readonly property color warning: FluTheme.dark ? "#FFD9A1" : "#9D5D00"
    // 查找命中底（琥珀色，与主题色状态区分）
    readonly property color findHighlight: Qt.rgba(0.95, 0.78, 0.25,
                                                   FluTheme.dark ? 0.30 : 0.16)
    // 当前行指示条/强调元素（主题色）
    readonly property color accentBar: FluTheme.primaryColor

    // ---- 圆角（Fluent 2 小圆角，最大 8）----
    readonly property int radiusCard: 8
    readonly property int radiusControl: 4

    // ---- 字阶（Segoe UI，Windows 原生；六档，见 visual-language.md §2.2）----
    readonly property int fontCaption: 12
    readonly property int fontMenu: 13
    readonly property int fontBody: 14
    readonly property int fontTitle: 16
    readonly property int fontHeading: 18   // 面板/章节标题专用（与正文明显区分）
    readonly property int fontDisplay: 22

    // ---- 间距（4px 基准网格）----
    readonly property int spacing1: 4
    readonly property int spacing2: 8
    readonly property int spacing3: 12
    readonly property int spacing4: 16   // 大间距
    readonly property int spacing5: 24   // 区块间距

    // ---- 尺寸 ----
    readonly property int rowHeight: 36
}
