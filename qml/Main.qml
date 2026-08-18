import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI

// 主窗口（迭代5 布局重构）：Outlook 式导航
// ┌───┬──────────────┬──────────────────────────┐
// │图标栏│ 侧边栏(SplitView) │ 内容区(Loader)        │
// │44px│ 可拖拽 180-400px │ 编辑页 / 设置页         │
// │可收起│ 可收起           │ (NoStack 切换重建)     │
// └───┴──────────────┴──────────────────────────┘
// 图标栏：页面切换（编辑/设置）+ service 侧边栏面板切换（sidebarPanels()）。
// 设计见 docs/services/iteration5-plugin-ui-agent.md §2。
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
        navViewRef: window
    }

    // ---- 页面与面板 ----
    property string editUrl: "qrc:/qt/qml/Translex/qml/TranslateHomePage.qml"
    property string settingsUrl: "qrc:/qt/qml/Translex/qml/TranslateSettingsPage.qml"
    property int currentPage: 0              // 0=编辑 1=设置
    property string currentPanelId: ""       // 当前侧边栏面板 serviceId（空=无面板）
    property var panelList: []               // sidebarPanels() 快照 [{id, displayName, panel}]

    // ---- 可见性（持久化到 ui.* 配置）----
    property bool iconBarVisible: configService.get("ui", "iconBarVisible")
    property bool sidebarVisible: configService.get("ui", "sidebarVisible")
    property int sidebarWidth: configService.get("ui", "leftPanelWidth")

    // ---- 页面切换（NoStack 语义：每次切换重建）----
    function switchPage(index) {
        currentPage = index
        contentLoader.source = index === 0 ? editUrl : settingsUrl
    }

    // 兼容 ui-driver 的 navigate(index) 命令
    function setCurrentIndex(index) {
        switchPage(index)
    }

    // ---- 侧边栏面板切换（图标栏点击 service 图标）----
    function switchPanel(id) {
        currentPanelId = id
        if (id === "") {
            panelLoader.source = ""
            return
        }
        for (let i = 0; i < panelList.length; ++i) {
            if (panelList[i].id === id) {
                panelLoader.source = panelList[i].panel
                if (!sidebarVisible) {
                    sidebarVisible = true
                }
                break
            }
        }
    }

    // ---- 面板点击跳转：转发到编辑器（设置页时先切回编辑页）----
    function focusLine(lineNumber) {
        if (contentLoader.item && typeof contentLoader.item.focusLine === "function") {
            contentLoader.item.focusLine(lineNumber)
            return
        }
        switchPage(0)
        if (contentLoader.item) {
            contentLoader.item.focusLine(lineNumber)
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ==================== 最左图标栏（Outlook 式） ====================
        Rectangle {
            id: iconBar
            width: iconBarVisible ? 44 : 4
            Layout.fillHeight: true
            color: FluTheme.dark ? Qt.rgba(1, 1, 1, 0.04) : Qt.rgba(0, 0, 0, 0.03)
            clip: true

            Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

            // 收起后保留的展开手柄
            Item {
                anchors.fill: parent
                visible: !iconBarVisible
                Rectangle {
                    width: 4
                    height: 40
                    radius: 2
                    color: FluTheme.resAccentColor
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.6
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: iconBarVisible = true
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                // 顶部：收起图标栏
                Rectangle {
                    Layout.preferredWidth: parent.width
                    Layout.preferredHeight: 34
                    color: "transparent"
                    FluIcon {
                        iconSource: FluentIcons.ChevronLeft
                        iconSize: 16
                        anchors.centerIn: parent
                        color: FluTheme.fontSecondaryColor
                    }
                    MouseArea {
                        id: iconBarCollapseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: iconBarVisible = false
                    }
                    ToolTip.visible: iconBarCollapseMouse.hovered
                    ToolTip.text: qsTr("收起图标栏")
                }

                // ---- 页面切换（编辑/设置）----
                IconBarButton {
                    iconSource: FluentIcons.Edit
                    tooltipText: qsTr("编辑")
                    active: window.currentPage === 0
                    onClicked: window.switchPage(0)
                }
                IconBarButton {
                    iconSource: FluentIcons.Settings
                    tooltipText: qsTr("设置")
                    active: window.currentPage === 1
                    onClicked: window.switchPage(1)
                }

                Rectangle {
                    Layout.preferredWidth: parent.width - 12
                    Layout.preferredHeight: 1
                    Layout.alignment: Qt.AlignHCenter
                    color: FluTheme.dividerColor
                    visible: window.panelList.length > 0
                }

                // ---- service 侧边栏面板（sidebarPanels() 动态注册）----
                Repeater {
                    model: window.panelList
                    delegate: IconBarButton {
                        iconSource: window.panelIcon(model.id)
                        tooltipText: model.displayName
                        active: window.currentPanelId === model.id
                        onClicked: window.switchPanel(model.id)
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // ==================== 侧边栏 + 内容区（可拖拽分割） ====================
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 4
                color: SplitHandle.hovered ? FluTheme.resAccentColor
                                           : (FluTheme.dark ? Qt.rgba(1, 1, 1, 0.06)
                                                            : Qt.rgba(0, 0, 0, 0.06))
            }

            // ---- 侧边栏（service 面板容器）----
            Item {
                id: sidebarContainer
                SplitView.preferredWidth: window.sidebarWidth
                SplitView.minimumWidth: 180
                SplitView.maximumWidth: 400
                visible: window.sidebarVisible
                clip: true

                // 拖拽 handle 时持久化宽度（visible=false 时 width 归零，跳过）
                onWidthChanged: {
                    if (visible && width >= 180) {
                        window.sidebarWidth = width
                    }
                }

                Loader {
                    id: panelLoader
                    anchors.fill: parent
                }

                // 顶部条：标题 + 收起按钮（面板自己提供内容，这里只放收起）
                Rectangle {
                    id: sidebarTopBar
                    width: parent.width
                    height: 30
                    color: "transparent"
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: FluTheme.dividerColor
                        anchors.bottom: parent.bottom
                    }
                    FluIcon {
                        iconSource: FluentIcons.ChevronLeft
                        iconSize: 14
                        color: FluTheme.fontSecondaryColor
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        id: sidebarCollapseMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: window.sidebarVisible = false
                    }
                    ToolTip.visible: sidebarCollapseMouse.hovered
                    ToolTip.text: qsTr("收起侧边栏")
                }
            }

            // ---- 内容区（页面 Loader）----
            Item {
                SplitView.fillWidth: true
                clip: true
                Loader {
                    id: contentLoader
                    anchors.fill: parent
                }
            }
        }
    }

    // ---- 面板图标映射（serviceId → FluentIcons）----
    function panelIcon(id) {
        switch (id) {
        case "chapter": return FluentIcons.Bookmarks
        case "comment": return FluentIcons.Message
        case "translationHistory": return FluentIcons.History
        default: return FluentIcons.Document
        }
    }

    // ---- 侧边栏宽度持久化 ----
    onSidebarWidthChanged: {
        configService.set("ui", "leftPanelWidth", sidebarWidth)
    }

    // 可见性持久化
    onIconBarVisibleChanged: configService.set("ui", "iconBarVisible", iconBarVisible)
    onSidebarVisibleChanged: configService.set("ui", "sidebarVisible", sidebarVisible)

    Component.onCompleted: {
        // 注册表提供的面板列表（service sidebarPanel 非空）
        panelList = serviceRegistry.sidebarPanels()
        // 默认进入编辑页 + 第一个面板
        contentLoader.source = editUrl
        if (panelList.length > 0) {
            switchPanel(panelList[0].id)
        }
    }
}
