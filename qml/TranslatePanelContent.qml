import QtQuick
import QtQuick.Layouts
import FluentUI
import Translex

// 翻译面板主体内容：按钮 + 进度 + 快捷键
// 供「浮动 Popup」与「停靠面板」两种模式复用（组件无标题栏/拖拽逻辑）
Item {
    id: root

    // 视觉 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

    // 状态（由页面注入）
    property bool translating: false
    property int progressDone: 0
    property int progressTotal: 0
    property bool hasSelection: false
    // 可选标题（停靠模式显示；浮动模式由 Popup 标题栏承担）
    property string titleText: ""

    // 交互信号（页面连接）
    signal translateCurrentRequested()
    signal translateAllRequested()
    signal translateSelectedRequested()
    signal cancelRequested()

    // Item 默认 implicitHeight/Width=0，不加绑定父级会把内容算成 0；
    // 但 implicitWidth 若绑定 panelCol.implicitWidth 会被内部文本撑到 ~378，反过来撑大
    // 父级（卡片被撑宽、按钮凸出）。故 implicitWidth 固定为 300-2*16=268 的合理值。
    implicitWidth: 268
    // 必须加上下 anchors.margins(16*2)：ColumnLayout 的 implicitHeight 不含自身 anchors.margins，
    // 否则父级高度少算 32px → 浮窗高度不足，底部文字被窗口边缘裁掉
    implicitHeight: panelCol.implicitHeight + 32

    // 自适应缩放因子：浮窗变窄时字号/间距等比缩小，内容不溢出，提供更大的缩放范围
    readonly property real scale: Math.max(0.85, Math.min(1.0, root.width / 280))
    // 宽度过窄时隐藏次要文本（说明/快捷键），只保留核心按钮
    readonly property bool compact: root.width < 250

    ColumnLayout {
        id: panelCol
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8 * root.scale

        // 可选标题（停靠模式）
        FluText {
            visible: root.titleText !== ""
            text: root.titleText
            font.pixelSize: tokens.fontTitle * root.scale
            font.bold: true
            color: FluTheme.fontPrimaryColor
        }

        FluText {
            visible: !root.compact
            text: qsTr("译文会写入为批注。支持当前行/所选行翻译，以及整篇范围批量翻译。")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // 说明文字按两行渲染：必须显式给 preferredHeight——WordWrap 的 implicitHeight 按单行算，
            // 否则父级 implicitHeight 少算、浮窗高度不足导致底部文字被窗口边缘截断
            Layout.preferredHeight: 34 * root.scale
            Layout.minimumWidth: 0
            // wrapMode 不影响 implicitWidth（仍按整段文本宽度计算），会撑大 ColumnLayout；
            // 用 maximumWidth 限制实际/隐式宽度，避免按钮被 implicitWidth 撑出
            Layout.maximumWidth: root.width - 32
            font.pixelSize: tokens.fontCaption * root.scale
            color: FluTheme.fontSecondaryColor
        }

        FluFilledButton {
            id: btnPrimary
            text: qsTr("翻译当前行 / 所选行")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.width - 32
            font.pixelSize: tokens.fontBody * root.scale
            onClicked: root.translateCurrentRequested()
        }
        FluButton {
            text: qsTr("翻译全部待译行")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.width - 32
            font.pixelSize: tokens.fontBody * root.scale
            onClicked: root.translateAllRequested()
        }
        FluButton {
            text: qsTr("翻译选中行")
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.width - 32
            enabled: root.hasSelection
            font.pixelSize: tokens.fontBody * root.scale
            onClicked: root.translateSelectedRequested()
        }

        // 进度 + 取消
        RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.width - 32
            visible: root.translating
            spacing: 8 * root.scale
            FluProgressBar {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                from: 0
                to: Math.max(root.progressTotal, 1)
                value: root.progressDone
            }
            FluText {
                text: qsTr("%1/%2").arg(root.progressDone).arg(root.progressTotal)
                font.pixelSize: tokens.fontCaption * root.scale
                color: FluTheme.primaryColor
            }
            FluButton {
                text: qsTr("取消")
                Layout.preferredHeight: 26 * root.scale
                font.pixelSize: tokens.fontBody * root.scale
                onClicked: root.cancelRequested()
            }
        }

        // 快捷键说明（两行换行 + 显式 preferredHeight：保证隐式高度正确且文字完整不截断）
        FluText {
            visible: !root.compact
            text: qsTr("快捷键：Ctrl+Alt+T 当前行 · Ctrl+Alt+Shift+T 整篇范围")
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: 32 * root.scale
            Layout.maximumWidth: root.width - 32
            font.pixelSize: tokens.fontCaption * root.scale
            color: FluTheme.fontTertiaryColor
        }

        Item { Layout.fillHeight: true }
    }
}
