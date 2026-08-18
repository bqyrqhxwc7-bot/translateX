// 章节导航面板（迭代5：由 ChapterService.sidebarPanel 提供，经图标栏切换加载）。
// 数据来自 chapterService；点击章节跳转编辑器对应行（经 Main.qml 的 focusLine 转发）。
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

Item {
    id: root

    property var chapterModel: ListModel {}

    function refresh() {
        chapterModel.clear()
        const titles = chapterService.chapterTitles()
        for (let i = 0; i < titles.length; ++i) {
            chapterModel.append({
                index: i,
                title: titles[i],
                startLine: chapterService.chapterStartLine(i)
            })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            text: qsTr("章节导航")
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
            model: root.chapterModel
            delegate: ItemDelegate {
                width: list.width
                height: 32
                text: model.title
                onClicked: root.focusLine(model.startLine)
            }
        }

        Label {
            text: qsTr("共 %1 个章节").arg(chapterModel.count)
            font.pixelSize: 12
            color: FluTheme.fontSecondaryColor
            leftPadding: 12
            bottomPadding: 8
            Layout.fillWidth: true
        }
    }

    Connections {
        target: chapterService
        function onChaptersChanged() { root.refresh() }
    }

    Component.onCompleted: refresh()
}