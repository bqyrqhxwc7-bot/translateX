import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

// 示例插件侧边栏面板（迭代5：验证插件 UI 扩展点）。
// 由 Main.qml 图标栏/侧边栏在选中本插件时加载（sidebarPanel() 返回本文件路径）。
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        Label {
            text: "示例插件面板"
            font.bold: true
            Layout.fillWidth: true
        }

        Label {
            text: "这是插件提供的侧边栏面板（qml 随插件 DLL 分发）。\n"
                + "插件后端：translation.echo（回显）。\n"
                + "在设置页切换后端到「示例回显后端」即可验证。"
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
