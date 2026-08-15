pragma Singleton
import QtQuick
import FluentUI

// 视觉语言 token（Outlook / Fluent 2 对齐，见 docs/ui/visual-language.md）
// 规则：新页面一律引用本单例或 FluTheme，禁止硬编码色值/圆角。
QtObject {
    // ---- 背景层级 ----
    // 应用/导航背景（FluTheme.windowBackgroundColor 即应用级背景）
    readonly property color bgApp: FluTheme.windowBackgroundColor
    // 卡片/面板/浮窗背景
    readonly property color bgCard: FluTheme.dark ? "#292929" : "#FFFFFF"
    // 次级卡片/分区底
    readonly property color bgCardAlt: FluTheme.dark
                                        ? Qt.rgba(1, 1, 1, 0.05)
                                        : Qt.rgba(0, 0, 0, 0.03)
    // 分隔线
    readonly property color divider: FluTheme.dividerColor

    // ---- 语义状态色（Fluent 2）----
    readonly property color success: FluTheme.dark ? "#6CCB5F" : "#0F7B0F"
    readonly property color error: FluTheme.dark ? "#FF99A4" : "#C42B1C"
    readonly property color warning: FluTheme.dark ? "#FFD9A1" : "#9D5D00"
    // 查找命中底（琥珀色，与主题色状态区分）
    readonly property color findHighlight: Qt.rgba(0.95, 0.78, 0.25,
                                                   FluTheme.dark ? 0.30 : 0.16)

    // ---- 圆角（Fluent 2 小圆角，最大 8）----
    readonly property int radiusCard: 8
    readonly property int radiusControl: 4

    // ---- 间距（4px 基准网格）----
    readonly property int spacing1: 4
    readonly property int spacing2: 8
    readonly property int spacing3: 12

    // ---- 字阶（Segoe UI，Windows 原生）----
    readonly property int fontBody: 14
    readonly property int fontCaption: 12

    // ---- 尺寸 ----
    readonly property int rowHeight: 36
}
