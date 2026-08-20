import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FluentUI
import Translex

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

    // 视觉 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
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
    property int sidebarWidth: 240

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
            // 收起后完全隐藏（width 0）：不留细缝，展开入口改用悬浮按钮（见下方）。
            // 注意：不能加 Behavior on width 动画——收起状态下首次展开（0→44 动画期间）
            // ColumnLayout 布局会错乱（2026-08-19 实测：收起状态退出再启动，首次展开错乱）
            width: iconBarVisible ? 44 : 0
            // 必须显式声明 Layout.preferredWidth：RowLayout 布局按 implicitWidth（Rectangle 默认 0）
            // 分配宽度，只改 width 绑定不会触发 SplitView 右移——收起状态启动后展开时侧边栏
            // 挤占图标栏位置（2026-08-19 根因）
            Layout.preferredWidth: iconBarVisible ? 44 : 0
            Layout.fillHeight: true
            color: tokens.bgNav
            clip: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 2

                // 顶部：收起图标栏
                // 注意：appBar 区域（y 0-30）是窗口拖拽区（FluFrameless HTCAPTION），
                // 点击被窗口系统拦截、不会到达 QML——交互元素必须下移 appBar.height
                Rectangle {
                    Layout.preferredWidth: parent.width
                    Layout.preferredHeight: 34
                    Layout.topMargin: appBar.height
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
                // 注意：Repeater 对 QVariantList model 时 model.id 访问失败（undefined，
                // Qt 6.11 实测），须用 modelData.id（2026-08-19 踩过）
                Repeater {
                    model: window.panelList
                    delegate: IconBarButton {
                        iconSource: window.panelIcon(modelData.id)
                        tooltipText: modelData.displayName
                        active: window.currentPanelId === modelData.id
                        onClicked: window.switchPanel(modelData.id)
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
                color: SplitHandle.hovered ? tokens.accentBar
                                           : tokens.divider
                Behavior on color { ColorAnimation { duration: 120 } }
                // 拖动状态中转：SplitHandle.pressed 是附加属性，不能直接写 onSplitHandlePressedChanged
                //（QML 解析报「不存在的属性」）；转成普通属性后用 onDraggingChanged 监听
                property bool dragging: SplitHandle.pressed
                // 拖动释放时保存最终宽度：拖动/窗口重排过程中的中间值会污染持久化值
                //（分割条持久化失败根因——2026-08-19）
                onDraggingChanged: {
                    if (!dragging && sidebarContainer.visible
                            && sidebarContainer.width >= 180) {
                        window.sidebarWidth = sidebarContainer.width
                        // 只在「用户拖动释放」时写盘：启动时初始布局宽度（minimumWidth）
                        // 会先触发 onWidthChanged，若此时持久化会把持久化值覆盖掉
                        //（分割条持久化失败根因——2026-08-19）
                        configService.set("ui", "leftPanelWidth",
                                          Math.round(sidebarContainer.width))
                    }
                }
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
                // 直接保存（不节流）：节流会在「拖动后立即退出」时丢最后一次值
                onWidthChanged: {
                    if (visible && width >= 180) {
                        window.sidebarWidth = width
                    }
                }

                Loader {
                    id: panelLoader
                    anchors.fill: parent
                    // 避开顶部收起条（sidebarTopBar：y=appBar.height，高 30）——
                    // 否则面板自带标题（如「章节导航」）与收起条叠字
                    anchors.topMargin: appBar.height + 30
                }

                // 顶部条：标题 + 收起按钮（面板自己提供内容，这里只放收起）
                // 注意：同图标栏收起按钮——appBar 拖拽区（y 0-30）点击被拦截，必须下移
                Rectangle {
                    id: sidebarTopBar
                    width: parent.width
                    height: 30
                    y: appBar.height
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

    // ---- 图标栏收起后的悬浮展开入口 ----
    // 收起后图标栏完全隐藏（width 0），此胶囊按钮浮在窗口左缘垂直居中，点击重新展开。
    // z 必须高于内容区：图标栏收起后 SplitView 顶到最左边（x=0），侧边栏会盖住按钮
    Rectangle {
        id: iconBarExpandButton
        visible: !window.iconBarVisible
        z: 100
        width: 26
        height: 44
        radius: 13
        color: FluTheme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.88)
        border.color: FluTheme.dividerColor
        border.width: 1
        anchors.left: parent.left
        anchors.leftMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        opacity: expandHover.hovered ? 1.0 : 0.55
        Behavior on opacity { NumberAnimation { duration: 120 } }
        FluIcon {
            iconSource: FluentIcons.ChevronRight
            iconSize: 14
            anchors.centerIn: parent
            color: FluTheme.fontPrimaryColor
        }
        MouseArea {
            id: expandHover
            anchors.fill: parent
            hoverEnabled: true
            onClicked: window.iconBarVisible = true
        }
        ToolTip.visible: expandHover.hovered
        ToolTip.text: qsTr("展开图标栏")
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
    // 注意：不做「每次 sidebarWidth 变化都写盘」——启动时初始布局宽度（minimumWidth）
    // 会先于恢复触发，把持久化值覆盖掉（分割条持久化失败根因）；写盘只在拖动释放时
    //（handle 的 onDraggingChanged）进行
    onSidebarWidthChanged: { /* 仅内存更新，持久化在拖动释放时 */ }

    // 可见性持久化
    onIconBarVisibleChanged: configService.set("ui", "iconBarVisible", iconBarVisible)
    onSidebarVisibleChanged: configService.set("ui", "sidebarVisible", sidebarVisible)

    // ---- 主窗口几何持久化（大小/位置/最大化；拖动/缩放节流保存，恢复钳制到屏幕内） ----
    property bool restoringGeometry: false
    // 恢复完成后才允许保存：抑制窗口初始化时把默认/无效几何写进配置（污染下次恢复）
    property bool geometryReady: false
    onXChanged: { if (geometryReady && !restoringGeometry) windowGeoSaveTimer.restart() }
    onYChanged: { if (geometryReady && !restoringGeometry) windowGeoSaveTimer.restart() }
    onWidthChanged: { if (geometryReady && !restoringGeometry) windowGeoSaveTimer.restart() }
    onHeightChanged: { if (geometryReady && !restoringGeometry) windowGeoSaveTimer.restart() }
    onVisibilityChanged: {
        // 最大化时只记状态不记几何（最大化几何会污染普通几何）；恢复普通窗口时记几何
        if (window.visibility === Window.Maximized) {
            configService.set("ui", "windowMaximized", true)
        } else if (window.visibility === Window.Windowed) {
            if (window.geometryReady) {
                configService.set("ui", "windowMaximized", false)
                windowGeoSaveTimer.restart()
            }
        }
    }
    Timer {
        id: windowGeoSaveTimer
        interval: 350
        onTriggered: {
            if (window.visibility !== Window.Windowed) return
            configService.set("ui", "windowX", Math.round(window.x))
            configService.set("ui", "windowY", Math.round(window.y))
            configService.set("ui", "windowWidth", Math.round(window.width))
            configService.set("ui", "windowHeight", Math.round(window.height))
        }
    }
    // 恢复几何：延迟到窗口映射完成（同浮窗 restoreFloatWindowPos 的时机问题）
    Timer {
        id: windowGeoRestoreTimer
        interval: 120
        onTriggered: {
            // 先恢复普通几何再处理最大化：取消最大化后窗口回到记住的位置/大小，
            // 避免「最大化退出 → 重启最大化 → 取消最大化时窗口落到启动默认位置」
            const scr = window.screen
            const sx = Number(scr && scr.virtualX) || 0
            const sy = Number(scr && scr.virtualY) || 0
            const sw = Number(scr && scr.virtualWidth) || 1536
            const sh = Number(scr && scr.virtualHeight) || 864
            let wx = Number(configService.get("ui", "windowX"))
            let wy = Number(configService.get("ui", "windowY"))
            let ww = Number(configService.get("ui", "windowWidth"))
            let wh = Number(configService.get("ui", "windowHeight"))
            if (!isFinite(ww) || ww < 900) ww = 1200
            if (!isFinite(wh) || wh < 600) wh = 780
            if (!isFinite(wx)) wx = sx + (sw - ww) / 2
            if (!isFinite(wy)) wy = sy + (sh - wh) / 2
            wx = Math.max(sx, Math.min(wx, sx + Math.max(0, sw - ww)))
            wy = Math.max(sy, Math.min(wy, sy + Math.max(0, sh - wh)))
            window.restoringGeometry = true
            window.setX(Math.round(wx))
            window.setY(Math.round(wy))
            window.width = Math.round(ww)
            window.height = Math.round(wh)
            window.restoringGeometry = false
            if (configService.get("ui", "windowMaximized") === true) {
                window.showMaximized()
            }
            window.geometryReady = true
        }
    }

    // 首次面板加载延迟到布局完成：SplitView 首次布局前 sidebarContainer 尺寸为 0，
    // 面板内容（ColumnLayout）按 0 尺寸布局导致文字堆叠重叠；重进面板时布局已稳定
    // 所以正常（2026-08-20 用户反馈「侧边栏文字重叠，重进页面后变好」根因）
    Timer {
        id: firstPanelTimer
        interval: 120
        onTriggered: {
            if (panelList.length > 0) {
                switchPanel(panelList[0].id)
            }
        }
    }

    Component.onCompleted: {
        // 注册表提供的面板列表（service sidebarPanel 非空）
        panelList = serviceRegistry.sidebarPanels()
        // 恢复侧边栏宽度：显式读取并钳制——绑定初始化在布局前可能取到无效值，
        // 导致 SplitView 落到最小宽度（分割条持久化失败根因，2026-08-19）
        const sw = Number(configService.get("ui", "leftPanelWidth"))
        if (isFinite(sw) && sw >= 180) {
            window.sidebarWidth = sw
        }
        // 默认进入编辑页 + 第一个面板（面板延迟到布局完成后再加载）
        contentLoader.source = editUrl
        firstPanelTimer.restart()
        // 恢复主窗口几何（延迟到窗口映射完成）
        windowGeoRestoreTimer.restart()
    }
}
