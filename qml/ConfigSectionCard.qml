import QtQuick
import QtQuick.Layouts
import FluentUI

// schema 驱动配置卡片：遍历 ConfigService 某 section 的配置项自动生成控件。
// 效果类似 VSCode 设置面板——新增配置项只需在 config.json 声明，UI 自动出现，零核心改动。
//
// 注意：不能加 `pragma ComponentBehavior: Bound`——Bound 模式下 Repeater 注入的
// model/index/modelData 动态上下文属性在 qmlcachegen AOT 下会失效（运行时 ReferenceError）。
ColumnLayout {
    id: root

    property string sectionId: ""        // ConfigService 的 section id
    property var excludeKeys: []         // 需要排除的 key（如 backend/glossary）
    property var excludeGroups: []       // 需要排除的分组（如 ["高级"]，用于折叠到卡片外）
    property bool showSectionTitle: true // 是否显示 section 标题（卡片内可隐藏避免重复）
    // 配置外部变更计数：enum 单选组靠它驱动 checked 重算（configService.get 无 notify 信号）
    property int configVersion: 0

    Connections {
        target: configService
        function onConfigChanged(section, key, value) {
            if (section === root.sectionId) {
                root.configVersion++
            }
        }
    }

    // 配置项模型（QML 原生 ListModel）
    ListModel {
        id: itemModel
    }

    onSectionIdChanged: reload()
    onExcludeKeysChanged: reload()
    Component.onCompleted: reload()

    // 从 ConfigService 拉取并过滤配置项。
    // 只保留控件需要的字段且统一类型（避免 ListModel 角色类型冲突，如 defaultValue 在 bool/number 间不一致）
    function reload() {
        itemModel.clear()
        const all = configService.sectionItems(sectionId)
        for (let i = 0; i < all.length; ++i) {
            const it = all[i]
            if (excludeKeys.indexOf(it.key) >= 0) {
                continue
            }
            if (excludeGroups.indexOf(it.group) >= 0) {
                continue
            }
            if (it.type === "enum" && (!it.options || it.options.length === 0)) {
                continue
            }
            itemModel.append({
                key: it.key,
                displayName: it.displayName,
                type: it.type,
                options: it.options !== undefined ? it.options : [],
                min: it.min !== undefined ? it.min : 0,
                max: it.max !== undefined ? it.max : 0,
                step: it.step !== undefined ? it.step : 1,
                placeholder: it.placeholder !== undefined ? it.placeholder : ""
            })
        }
    }

    spacing: 6

    // ---------- section 标题 ----------
    FluText {
        visible: root.showSectionTitle && root.sectionId.length > 0 && itemModel.count > 0
        text: configService.sectionDisplayName(root.sectionId)
        font.pixelSize: 16
        font.bold: true
        Layout.fillWidth: true
        Layout.topMargin: 8
    }

    // ---------- 配置项 ----------
    Repeater {
        id: repeater
        Layout.fillWidth: true
        model: itemModel
        delegate: RowLayout {
            width: root.width
            spacing: 12
            // 别名：避免内部控件（如 ComboBox 的 model 属性）遮蔽 delegate 的 model
            property var cfgItem: model

            FluText {
                text: cfgItem.displayName
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                elide: Text.ElideRight
            }

            // bool → 开关
            FluToggleSwitch {
                visible: cfgItem.type === "bool"
                checked: cfgItem.type === "bool" && configService.get(root.sectionId, cfgItem.key)
                onToggled: configService.set(root.sectionId, cfgItem.key, checked)
            }

            // number → 数字框
            FluSpinBox {
                visible: cfgItem.type === "number"
                Layout.preferredWidth: 140
                from: cfgItem.min
                to: cfgItem.max > 0 ? cfgItem.max : 999999
                stepSize: cfgItem.step > 0 ? cfgItem.step : 1
                editable: true
                value: cfgItem.type === "number" ? Number(configService.get(root.sectionId, cfgItem.key)) : 0
                onValueModified: configService.set(root.sectionId, cfgItem.key, value)
            }

            // enum → 行内单选按钮组
            // 注意：NoStack 页面 window=null，FluComboBox 的下拉 Popup 弹不出（表现为“不可用”），
            // 改用不依赖 Popup 的 FluRadioButton 组；checked 由 configVersion 驱动重算。
            RowLayout {
                visible: cfgItem.type === "enum"
                spacing: 6
                Repeater {
                    model: cfgItem.options !== undefined ? cfgItem.options : []
                    FluRadioButton {
                        property string opt: modelData
                        text: opt
                        checked: {
                            root.configVersion
                            return configService.get(root.sectionId, cfgItem.key) === opt
                        }
                        clickListener: () => {
                            configService.set(root.sectionId, cfgItem.key, opt)
                        }
                    }
                }
            }

            // secret → 密码框（值经 SecureStorage 加密存储）
            FluTextBox {
                visible: cfgItem.type === "secret"
                Layout.preferredWidth: 220
                echoMode: TextInput.Password
                placeholderText: cfgItem.placeholder || qsTr("已保存，输入新值覆盖")
                text: configService.get(root.sectionId, cfgItem.key)
                onEditingFinished: configService.set(root.sectionId, cfgItem.key, text)
            }

            // multiline → 多行框（提示词等，宽且高）
            FluMultilineTextBox {
                visible: cfgItem.type === "multiline"
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                placeholderText: cfgItem.placeholder
                text: configService.get(root.sectionId, cfgItem.key)
                onEditingFinished: configService.set(root.sectionId, cfgItem.key, text)
            }

            // string / path / 其他 → 单行文本框
            FluTextBox {
                visible: cfgItem.type !== "bool" && cfgItem.type !== "number"
                        && cfgItem.type !== "enum" && cfgItem.type !== "secret"
                        && cfgItem.type !== "multiline"
                Layout.preferredWidth: 220
                placeholderText: cfgItem.placeholder
                text: configService.get(root.sectionId, cfgItem.key)
                onEditingFinished: configService.set(root.sectionId, cfgItem.key, text)
            }
        }
    }
}
