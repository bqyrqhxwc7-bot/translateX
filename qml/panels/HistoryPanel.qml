// 翻译历史面板（迭代5：由 TranslationHistoryService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 translationHistoryService.entries()（最新在前）；点击条目跳转编辑器对应行。
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

    property var historyModel: ListModel {}

    function refresh() {
        historyModel.clear()
        const entries = translationHistoryService.entries()
        for (let i = 0; i < entries.length; ++i) {
            const e = entries[i]
            historyModel.append({
                line: e.line,
                source: e.source,
                translated: e.translated,
                success: e.success,
                time: e.time
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        FluText {
            text: qsTr("翻译历史")
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
            model: root.historyModel
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
                implicitHeight: 56
                onClicked: root.focusLine(model.line)
                // delegate 内不能访问顶层 tokens，实例化本地 token
                DesignTokens {
                    id: t
                }
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        spacing: 6
                        FluText {
                            text: qsTr("第 %1 行").arg(model.line + 1)
                            font.pixelSize: t.fontCaption
                            color: model.success ? FluTheme.fontSecondaryColor
                                                 : t.error
                        }
                        FluText {
                            text: model.time
                            font.pixelSize: t.fontCaption
                            color: FluTheme.fontSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                    }
                    FluText {
                        text: model.source
                        maximumLineCount: 1   // 防换行溢出固定高 delegate（2026-08-19 批注面板同坑）
                        elide: Text.ElideRight
                        font.pixelSize: t.fontCaption
                        color: FluTheme.fontSecondaryColor
                        Layout.fillWidth: true
                    }
                    FluText {
                        text: model.translated
                        maximumLineCount: 1   // 防换行溢出固定高 delegate（2026-08-19 批注面板同坑）
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        FluText {
            text: qsTr("共 %1 条（会话级，重启清空）").arg(historyModel.count)
            font.pixelSize: tokens.fontCaption
            color: FluTheme.fontSecondaryColor
            leftPadding: 12
            bottomPadding: 8
            Layout.fillWidth: true
        }
    }

    Connections {
        target: translationHistoryService
        function onEntryAdded() { root.refresh() }
    }

    Component.onCompleted: refresh()
}