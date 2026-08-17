import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

FluWindow {
    id: window
    title: qsTr("Translex (QML)")
    width: 1200
    height: 780
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    launchMode: FluWindowType.SingleTask
    fitsAppBarWindows: true

    appBar: FluAppBar {
        height: 30
        showDark: true
        z: 7
    }

    // UI 驱动操作（测试钩子）：仅 uiDriverBridge 存在（TRANSLEX_UI_DRIVER=1）时生效
    UiDriverActions {
        id: uiDriverActions
    }

    FluNavigationView {
        id: navView
        anchors.fill: parent
        pageMode: FluNavigationViewType.NoStack
        displayMode: FluNavigationViewType.Auto
        title: qsTr("Translex")

        items: FluObject {
            FluPaneItem {
                title: qsTr("编辑")
                icon: FluentIcons.Edit
                url: "qrc:/qt/qml/Translex/qml/TranslateHomePage.qml"
                onTap: navView.push(url)
            }
            FluPaneItem {
                title: qsTr("设置")
                icon: FluentIcons.Settings
                url: "qrc:/qt/qml/Translex/qml/TranslateSettingsPage.qml"
                onTap: navView.push(url)
            }
        }

        Component.onCompleted: {
            // 首页默认进入翻译编辑页（NoStack 模式每次导航重建页面）
            setCurrentIndex(0)
        }
    }
}
