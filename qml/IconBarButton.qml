// 图标栏按钮（迭代5 Outlook 式导航）：44px 窄条内的图标按钮。
// 选中态：左侧主题色指示条 + 背景；hover：背景加深；ToolTip 显示名称。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import Translex

Rectangle {
    id: control

    // 视觉 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

    property int iconSource: 0
    property string tooltipText: ""
    property bool active: false
    signal clicked()

    width: parent ? parent.width : 44
    // 必须声明 Layout.fillWidth：ColumnLayout 布局按 implicitWidth（Rectangle 默认 0）覆盖
    // 宽度，按钮被压到 0 宽、图标挤在左缘（收起状态启动后首次展开图标被挤压——2026-08-19）
    Layout.fillWidth: true
    height: 40
    radius: tokens.radiusControl
    color: active
           ? tokens.bgActive
           : (mouse.hovered ? tokens.bgHover : "transparent")
    // 交互动画（ui-improvement-plan P0）：hover/选中颜色过渡 + 微缩放
    Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }
    scale: mouse.hovered ? 1.08 : 1.0
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    // 选中指示条（左侧，宽度 0→3 动画展开）
    Rectangle {
        width: control.active ? 3 : 0
        height: 18
        radius: 1.5   // 指示条专用圆角（细条，非标准控件圆角，不走 token）
        color: FluTheme.primaryColor
        visible: control.active || width > 0
        anchors.left: parent.left
        anchors.leftMargin: 2
        anchors.verticalCenter: parent.verticalCenter
        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
    }

    FluIcon {
        iconSource: control.iconSource
        iconSize: 18
        anchors.centerIn: parent
        color: control.active ? FluTheme.primaryColor : FluTheme.fontPrimaryColor
    }

    // 键盘导航（ui-improvement-plan P1）：Tab 聚焦 + Enter/Space 触发 + 焦点环
    activeFocusOnTab: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Space) {
            control.clicked()
            event.accepted = true
        }
    }

    // 焦点环（2px 主题色，不超出按钮——iconBar clip 会裁掉外扩部分）
    Rectangle {
        visible: control.activeFocus
        anchors.fill: parent
        radius: tokens.radiusControl
        color: "transparent"
        border.color: FluTheme.primaryColor
        border.width: 2
        opacity: 0.4
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
