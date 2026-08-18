// 翻译历史面板（迭代5：由 TranslationHistoryService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 translationHistoryService.entries()（最新在前）；点击条目跳转编辑器对应行。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

Item {
    id: root

    // 视觉 token（delegate 内不能直接访问实例 id，经 root 中转）
    property var tokens: DesignTokens {}
    readonly property color errorColor: tokens.error

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

        Label {
            text: qsTr("翻译历史")
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
            delegate: ItemDelegate {
                width: list.width
                implicitHeight: 56
                onClicked: root.focusLine(model.line)
                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        spacing: 6
                        Label {
                            text: qsTr("第 %1 行").arg(model.line + 1)
                            font.pixelSize: 12
                            color: model.success ? FluTheme.fontSecondaryColor
                                                 : root.errorColor
                        }
                        Label {
                            text: model.time
                            font.pixelSize: 12
                            color: FluTheme.fontSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Label {
                        text: model.source
                        elide: Text.ElideRight
                        font.pixelSize: 12
                        color: FluTheme.fontSecondaryColor
                        Layout.fillWidth: true
                    }
                    Label {
                        text: model.translated
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Label {
            text: qsTr("共 %1 条（会话级，重启清空）").arg(historyModel.count)
            font.pixelSize: 12
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