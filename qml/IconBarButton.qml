// 图标栏按钮（迭代5 Outlook 式导航）：44px 窄条内的图标按钮。
// 选中态：左侧主题色指示条 + 背景；hover：背景加深；ToolTip 显示名称。
import QtQuick
import QtQuick.Controls
import FluentUI

Rectangle {
    id: control

    property int iconSource: 0
    property string tooltipText: ""
    property bool active: false
    signal clicked()

    width: parent ? parent.width : 44
    height: 40
    radius: 4
    color: active
           ? (FluTheme.dark ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(0, 0, 0, 0.06))
           : (mouse.hovered ? (FluTheme.dark ? Qt.rgba(1, 1, 1, 0.05) : Qt.rgba(0, 0, 0, 0.04))
                            : "transparent")

    // 选中指示条（左侧）
    Rectangle {
        width: 3
        height: 18
        radius: 1.5
        color: FluTheme.resAccentColor
        visible: control.active
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
    }

    FluIcon {
        iconSource: control.iconSource
        iconSize: 18
        anchors.centerIn: parent
        color: control.active ? FluTheme.resAccentColor : FluTheme.fontPrimaryColor
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: control.clicked()
    }

    ToolTip.visible: mouse.hovered && control.tooltipText !== ""
    ToolTip.text: control.tooltipText
    ToolTip.delay: 400
}
