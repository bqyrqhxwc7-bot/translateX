// 批注列表面板（迭代5：由 CommentService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 commentService.allComments()；点击批注跳转编辑器对应行。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import Translex

Item {
    id: root

    // 视觉 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

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

        FluText {
            text: qsTr("批注列表")
            font.pixelSize: tokens.fontHeading   // 面板标题（ui-improvement-plan P1）
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
            // 列表动画（ui-improvement-plan P0）；受限模式禁用（性能铁律，见 large-file.md）
            add: Transition {
                enabled: !documentModel.limitedMode
                ParallelAnimation {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 }
                    NumberAnimation { property: "y"; from: 10; to: 0; duration: 200; easing.type: Easing.OutCubic }
                }
            }
            move: Transition {
                enabled: !documentModel.limitedMode
                NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
            }
            displaced: Transition {
                enabled: !documentModel.limitedMode
                NumberAnimation { property: "y"; duration: 150; easing.type: Easing.OutCubic }
            }
            delegate: FluItemDelegate {
                width: list.width
                implicitHeight: 44
                onClicked: root.focusLine(model.line)
                // delegate 内不能访问顶层 tokens，实例化本地 token
                DesignTokens {
                    id: t
                }
                contentItem: ColumnLayout {
                    spacing: 2
                    FluText {
                        text: qsTr("第 %1 行").arg(model.line + 1)
                        font.pixelSize: t.fontCaption
                        color: FluTheme.fontSecondaryColor
                    }
                    FluText {
                        text: model.text
                        // 必须限制行数：delegate 固定高 44，多行文本会溢出画到相邻列表项上
                        //（elide 只对单行生效，无 maximumLineCount 时长文本仍换行——2026-08-19 实测重叠）
                        maximumLineCount: 1
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        FluText {
            text: qsTr("共 %1 条批注").arg(commentModel.count)
            font.pixelSize: tokens.fontCaption
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