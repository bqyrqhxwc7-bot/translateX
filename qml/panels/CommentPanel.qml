// 批注列表面板（迭代5：由 CommentService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 commentService.allComments()；点击批注跳转编辑器对应行。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

Item {
    id: root

    property var commentModel: ListModel {}

    function refresh() {
        commentModel.clear()
        const all = commentService.allComments()
        const lines = Object.keys(all).map(Number).sort((a, b) => a - b)
        for (let i = 0; i < lines.length; ++i) {
            const line = lines[i]
            commentModel.append({
                line: line,
                text: all[line]
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: qsTr("批注列表")
            font.bold: true
            leftPadding: 12
            topPadding: 10
            bottomPadding: 6
            Layout.fillWidth: true
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.commentModel
            delegate: ItemDelegate {
                width: list.width
                implicitHeight: 44
                onClicked: root.focusLine(model.line)
                contentItem: ColumnLayout {
                    spacing: 2
                    Label {
                        text: qsTr("第 %1 行").arg(model.line + 1)
                        font.pixelSize: 12
                        color: FluTheme.fontSecondaryColor
                    }
                    Label {
                        text: model.text
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Label {
            text: qsTr("共 %1 条批注").arg(commentModel.count)
            font.pixelSize: 12
            color: FluTheme.fontSecondaryColor
            leftPadding: 12
            bottomPadding: 8
            Layout.fillWidth: true
        }
    }

    Connections {
        target: commentService
        function onCommentsReset() { root.refresh() }
        function onCommentChanged() { root.refresh() }
    }

    Component.onCompleted: refresh()
}