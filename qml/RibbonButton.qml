// Ribbon 功能区按钮（ui-improvement-plan P3）：FluButton + 前置图标，增强扫描性。
// filled: true 时为主题色实心（主操作按钮，对应 FluFilledButton 视觉）。
// 用法：RibbonButton { iconSource: FluentIcons.Save; text: qsTr("保存"); onClicked: ... }
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

FluButton {
    id: control

    property int iconSource: 0
    property int iconSize: 14
    property bool filled: false

    // filled 时覆盖为主题色系（背景/文字/图标色），否则保持 FluButton 默认
    normalColor: filled ? FluTheme.primaryColor
                        : (FluTheme.dark ? Qt.rgba(62/255, 62/255, 62/255, 1)
                                         : Qt.rgba(254/255, 254/255, 254/255, 1))
    hoverColor: filled ? (FluTheme.dark ? Qt.darker(FluTheme.primaryColor, 1.1)
                                        : Qt.lighter(FluTheme.primaryColor, 1.1))
                       : (FluTheme.dark ? Qt.rgba(68/255, 68/255, 68/255, 1)
                                        : Qt.rgba(246/255, 246/255, 246/255, 1))
    textColor: {
        if (filled) {
            if (!enabled) return Qt.rgba(173/255, 173/255, 173/255, 1)
            return FluTheme.dark ? Qt.rgba(0, 0, 0, 1) : Qt.rgba(1, 1, 1, 1)
        }
        if (FluTheme.dark) {
            if (!enabled) return Qt.rgba(131/255, 131/255, 131/255, 1)
            if (pressed) return Qt.rgba(162/255, 162/255, 162/255, 1)
            return Qt.rgba(1, 1, 1, 1)
        }
        if (!enabled) return Qt.rgba(160/255, 160/255, 160/255, 1)
        if (pressed) return Qt.rgba(96/255, 96/255, 96/255, 1)
        return Qt.rgba(0, 0, 0, 1)
    }

    contentItem: RowLayout {
        spacing: 6
        FluIcon {
            visible: control.iconSource !== 0
            iconSource: control.iconSource
            iconSize: control.iconSize
            color: control.textColor
        }
        FluText {
            text: control.text
            font: control.font
            color: control.textColor
        }
    }
}