import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import FluentUI
import Translex

FluScrollablePage {
    id: page
    // NoStack 模式下由 FluNavigationView 的 FluLoader 直接加载，须显式填满父级
    //（否则页面宽度不跟随窗口，全屏/最大化后右侧出现透明空白）
    anchors.fill: parent
    title: qsTr("设置")
    launchMode: FluPageType.SingleTask

    // 视觉语言 token 实例（普通组件，见 docs/ui/visual-language.md）
    DesignTokens {
        id: tokens
    }

    // ---------- 本地状态 ----------
    property var glossaryMap: ({})          // 术语表（原文 → 标准译文）
    property var backendModel: []           // [{ id, name }]
    property string backendSection: ""      // 当前后端对应的配置 section（schema 渲染用）
    // 后端单选组重算驱动（函数调用绑定不会自动重算，参照 ConfigSectionCard.configVersion）
    property int backendVersion: 0
    // 连接测试状态
    property bool backendTesting: false
    property bool backendTestOk: false
    // 显示/查找设置（与编辑器共用 config，页面重建时读取）
    property int originalFontSize: 14
    property int commentFontSize: 12
    property bool findCaseSensitive: false
    property bool findWholeWord: false
    property bool findFuzzy: false
    // 调试卡片（迭代5）：服务健康度模型 + 插件错误文本
    property string pluginErrorsText: ""

    // 服务健康度模型（healthReport 聚合，刷新时重建）
    ListModel {
        id: debugHealthModel
    }
    function refreshDebugHealth() {
        debugHealthModel.clear()
        const items = serviceRegistry.healthReport()
        for (let i = 0; i < items.length; ++i) {
            debugHealthModel.append(items[i])
        }
        const errs = serviceRegistry.loadedPluginErrors()
        pluginErrorsText = errs.length === 0 ? qsTr("无插件加载错误") : errs.join("\n")
    }

    // 通知条：NoStack 模式下 Window.window 附加属性解析失败，改用页面内 InfoBar 实例
    FluInfoBar {
        id: infoBar
        root: page
    }

    // ---------- 初始化 ----------
    Component.onCompleted: {
        glossaryMap = translationService.glossary()
        rebuildTermList()
        rebuildBackendModel()
        updateBackendSection()
        // 显示/查找设置（与编辑器共用 ui section）
        const ofs = Number(configService.get("ui", "originalFontSize"))
        originalFontSize = isFinite(ofs) && ofs > 0 ? Math.round(ofs) : 14
        const cfs = Number(configService.get("ui", "commentFontSize"))
        commentFontSize = isFinite(cfs) && cfs > 0 ? Math.round(cfs) : 12
        findCaseSensitive = Boolean(configService.get("ui", "findCaseSensitive"))
        findWholeWord = Boolean(configService.get("ui", "findWholeWord"))
        findFuzzy = Boolean(configService.get("ui", "findFuzzy"))
        refreshDebugHealth()
    }

    // ---------- 术语表操作 ----------
    function addTerm() {
        const s = termSourceBox.text.trim()
        const t = termTargetBox.text.trim()
        if (!s || !t) {
            infoBar.showWarning(qsTr("原文术语和标准译文都不能为空"))
            return
        }
        glossaryMap[s] = t
        translationService.setGlossary(glossaryMap)
        rebuildTermList()
        termSourceBox.text = ""
        termTargetBox.text = ""
        infoBar.showSuccess(qsTr("已添加术语：%1 → %2").arg(s).arg(t))
    }

    function removeTerm(src) {
        delete glossaryMap[src]
        translationService.setGlossary(glossaryMap)
        rebuildTermList()
    }

    // 行内编辑译文（自动提取的占位项在此补填；空译文不注入提示词/不参与校验）
    function updateTermTranslation(src, t) {
        const cur = glossaryMap[src]
        if (t.trim() === cur) {
            return
        }
        glossaryMap[src] = t.trim()
        translationService.setGlossary(glossaryMap)
        rebuildTermList()
    }

    function clearTerms() {
        glossaryMap = ({})
        translationService.clearGlossary()
        rebuildTermList()
        infoBar.showInfo(qsTr("已清空术语表"))
    }

    // ---------- 术语自动提取（迭代4） ----------
    // 从文档原文提取高频英文词 → 弹窗勾选 → 加入术语表
    function extractTerms() {
        // 受限模式（大文件）禁用：一次性搬 50 万行到 JS 数组会卡死 UI
        if (documentModel.limitedMode) {
            infoBar.showWarning(qsTr("大文件受限模式下不支持术语提取"))
            return
        }
        const lines = []
        const count = Math.min(documentModel.lineCount(), 50000)
        for (let i = 0; i < count; ++i) {
            lines.push(documentModel.lineText(i))
        }
        const candidates = translationService.extractTermCandidates(lines, 3, -1)
        termCandidateModel.clear()
        for (const c of candidates) {
            termCandidateModel.append({ word: c.word, count: c.count, checked: false })
        }
        if (termCandidateModel.count === 0) {
            infoBar.showWarning(qsTr("未提取到高频词（词需出现 3 次以上；英文/标识符/中文均支持）"))
            return
        }
        extractDialog.open()
    }

    function extractSelectAll(checked) {
        for (let i = 0; i < termCandidateModel.count; ++i) {
            termCandidateModel.setProperty(i, "checked", checked)
        }
    }

    // AI 建议译文：请求大模型按文档上下文猜测勾选术语的标准译文（回填输入框）
    function suggestTermTranslations() {
        const terms = []
        for (let i = 0; i < termCandidateModel.count; ++i) {
            if (termCandidateModel.get(i).checked) {
                terms.push(termCandidateModel.get(i).word)
            }
        }
        if (terms.length === 0) {
            infoBar.showWarning(qsTr("请先勾选要建议译文的术语"))
            return
        }
        const lines = []
        const count = Math.min(documentModel.lineCount(), 50000)
        for (let i = 0; i < count; ++i) {
            lines.push(documentModel.lineText(i))
        }
        infoBar.showInfo(qsTr("正在请求大模型建议 %1 个术语译文...").arg(terms.length))
        translationService.suggestTermTranslations(terms, lines)
    }

    function addExtractedTerms() {
        let added = 0
        for (let i = 0; i < termCandidateModel.count; ++i) {
            if (termCandidateModel.get(i).checked) {
                const w = termCandidateModel.get(i).word
                const t = termCandidateModel.get(i).translation
                // 译文留空占位：不注入提示词、不参与质量校验（见 TermGlossary），
                // 避免「译文=原文」污染质量自检
                glossaryMap[w] = String(t || "").trim()
                ++added
            }
        }
        if (added > 0) {
            translationService.setGlossary(glossaryMap)
            rebuildTermList()
            infoBar.showSuccess(qsTr("已添加 %1 个术语（译文为空的是占位，可点击行内输入框补填）").arg(added))
        }
    }

    function rebuildTermList() {
        termModel.clear()
        for (const key of Object.keys(glossaryMap)) {
            termModel.append({ source: key, translation: glossaryMap[key] })
        }
    }

    // ---------- 后端列表 ----------
    function rebuildBackendModel() {
        const ids = translationService.availableBackends()
        const arr = []
        for (let i = 0; i < ids.length; ++i) {
            arr.push({ id: ids[i], name: translationService.backendDisplayName(ids[i]) })
        }
        backendModel = arr
        // 单选组 checked 由 backendVersion 驱动重算
        backendVersion++
    }

    // 当前后端 → 对应配置 section（仅已知带参数的后端显示参数区）
    function updateBackendSection() {
        const id = translationService.backend()
        backendSection = (id === "translation.ollama" || id === "translation.network_model") ? id : ""
    }

    // ---------- 服务信号 ----------
    Connections {
        target: translationService
        function onBackendChanged(id) {
            // 单选组 checked 由 backendVersion 驱动重算
            backendVersion++
            updateBackendSection()
        }
        function onQualityWarning(lineNumber, issue) {
            const loc = lineNumber >= 0 ? qsTr("第 %1 行：").arg(lineNumber + 1) : ""
            infoBar.showWarning(loc + issue, 4000)
        }
        function onConnectionTested(backendId, ok, message) {
            backendTesting = false
            backendTestOk = ok
            backendTestLabel.text = ok ? qsTr("连接正常：%1").arg(message) : qsTr("连接失败：%1").arg(message)
        }
        // 术语建议译文（提取弹窗）：回填勾选术语的建议译文（可修改后添加）
        function onTermSuggestionsReady(suggestions, ok, errorMessage) {
            if (!ok) {
                infoBar.showWarning(errorMessage, 6000)
                return
            }
            let filled = 0
            for (let i = 0; i < termCandidateModel.count; ++i) {
                const w = termCandidateModel.get(i).word
                const s = suggestions[w]
                if (s !== undefined && String(s).trim() !== "") {
                    termCandidateModel.setProperty(i, "translation", String(s).trim())
                    ++filled
                }
            }
            infoBar.showSuccess(qsTr("已为 %1 个术语填入建议译文（可修改）").arg(filled))
        }
    }

    // ================= 界面 =================
    spacing: 16

    // ---------- 页头 ----------
    FluText {
        text: qsTr("翻译设置")
        font.pixelSize: tokens.fontDisplay
        font.bold: true
        Layout.fillWidth: true
    }
    FluText {
        text: qsTr("可插拔翻译后端、翻译选项与术语一致性配置，实时生效并持久化。")
        font.pixelSize: tokens.fontCaption
        color: FluTheme.fontSecondaryColor
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }

    // ---------- 卡片：翻译后端 ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardBackendCol.implicitHeight + 32

        ColumnLayout {
            id: cardBackendCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.Sync; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("翻译后端"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("后端")
                    Layout.alignment: Qt.AlignVCenter
                }
                // NoStack 下 FluComboBox 的 Popup 不可用（铁律），改用行内单选组
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: backendModel
                        FluRadioButton {
                            property string opt: modelData.id
                            text: modelData.name
                            checked: {
                                backendVersion
                                return translationService.backend() === modelData.id
                            }
                            clickListener: () => {
                                translationService.setBackend(modelData.id)
                                backendVersion++
                            }
                        }
                    }
                }
            }
            FluText {
                text: qsTr("可插拔后端：本地 Ollama、内置免费云端、网络大模型（OpenAI 兼容）。")
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // 后端参数（schema 驱动：当前后端的 config section）
            ConfigSectionCard {
                id: backendConfigCard
                sectionId: backendSection
                Layout.fillWidth: true
                visible: backendSection.length > 0
            }

            // 连接测试（异步：Ollama 走模型扫描，网络模型走最小翻译探测）
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluButton {
                    text: qsTr("测试连接")
                    disabled: backendTesting
                    onClicked: {
                        backendTesting = true
                        backendTestLabel.text = qsTr("测试中…")
                        translationService.testBackendConnection(translationService.backend())
                    }
                }
                FluText {
                    id: backendTestLabel
                    text: ""
                    font.pixelSize: tokens.fontCaption
                    color: backendTestOk ? tokens.success : tokens.error
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    // ---------- 卡片：翻译选项 ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardOptionsCol.implicitHeight + 32

        ColumnLayout {
            id: cardOptionsCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.Edit; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("翻译选项"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            // 语言选择（FluComboBox：设置页内 Popup 实测可用，回退自 RadioButton 组）
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("源语言")
                    Layout.alignment: Qt.AlignVCenter
                }
                FluComboBox {
                    id: sourceLangCombo
                    Layout.preferredWidth: 140
                    model: ["auto", "en", "zh-CN", "ja", "ko", "fr", "de", "es", "ru"]
                    Component.onCompleted: {
                        const v = configService.get("translation", "sourceLang")
                        currentIndex = model.indexOf(v)
                    }
                    onActivated: configService.set("translation", "sourceLang", currentText)
                }
                FluText {
                    text: qsTr("目标语言")
                    Layout.alignment: Qt.AlignVCenter
                }
                FluComboBox {
                    id: targetLangCombo
                    Layout.preferredWidth: 140
                    model: ["zh-CN", "en", "ja", "ko", "fr", "de", "es", "ru"]
                    Component.onCompleted: {
                        const v = configService.get("translation", "targetLang")
                        currentIndex = model.indexOf(v)
                    }
                    onActivated: configService.set("translation", "targetLang", currentText)
                }
            }

            ConfigSectionCard {
                sectionId: "translation"
                excludeKeys: ["backend", "glossary", "sourceLang", "targetLang"]
                excludeGroups: ["高级"]
                showSectionTitle: false
                Layout.fillWidth: true
            }

            // 翻译面板设置（模式切换统一在 Ribbon「翻译」标签的浮窗开关；此处仅启动显示）
            FluExpander {
                Layout.fillWidth: true
                headerText: qsTr("翻译面板")
                contentHeight: 200
                ColumnLayout {
                    width: parent.width - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    FluText {
                        text: qsTr("翻译支持两种呈现：功能区（顶部 Ribbon「翻译」标签）与悬浮窗。在「翻译」标签页点「浮窗」开关即可切换；悬浮窗位置拖动标题栏即自动记忆，无需设置。下方仅设置启动时是否自动显示悬浮窗。")
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ConfigSectionCard {
                        sectionId: "ui"
                        // 模式由 Ribbon 浮窗开关统一切换；位置与当前标签为内部状态；
                        // 字号与查找选项由下方「显示/查找」卡片以滑动条/开关呈现
                        excludeKeys: ["translatePanelMode", "currentRibbonTab", "translatePanelX", "translatePanelY",
                                      "translatePanelWidth", "translatePanelHeight",
                                      "originalFontSize", "commentFontSize",
                                      "findCaseSensitive", "findWholeWord", "findFuzzy",
                                      "leftPanelWidth", "iconBarVisible", "sidebarVisible",
                                      "windowX", "windowY", "windowWidth", "windowHeight", "windowMaximized"]
                        showSectionTitle: false
                        Layout.fillWidth: true
                    }
                }
            }

            // 高级设置（自定义提示词，可折叠）
            FluExpander {
                Layout.fillWidth: true
                headerText: qsTr("高级：自定义提示词")
                contentHeight: 400
                ColumnLayout {
                    width: parent.width - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 8
                    FluText {
                        text: qsTr("自定义提示词仅对本地 Ollama 与网络大模型生效（免费云端 API 不支持自定义提示词）。")
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        FluText {
                            text: qsTr("启用自定义提示词（%1=原文，%2=上下文）")
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                        FluToggleSwitch {
                            checked: configService.get("translation", "enableCustomPrompt")
                            onToggled: configService.set("translation", "enableCustomPrompt", checked)
                        }
                    }
                    FluText {
                        text: qsTr("普通翻译提示词（%1 将被替换为原文）")
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                    }
                    FluMultilineTextBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        text: configService.get("translation", "customPrompt")
                        onEditingFinished: configService.set("translation", "customPrompt", text)
                    }
                    FluText {
                        text: qsTr("上下文翻译提示词（%1 原文，%2 上下文）")
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                    }
                    FluMultilineTextBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 120
                        text: configService.get("translation", "customContextPrompt")
                        onEditingFinished: configService.set("translation", "customContextPrompt", text)
                    }
                }
            }
        }
    }

    // ---------- 卡片：术语表 ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardTermsCol.implicitHeight + 32

        ColumnLayout {
            id: cardTermsCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.DictionaryAdd; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("术语表（保证术语翻译一致）"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluTextBox {
                    id: termSourceBox
                    Layout.fillWidth: true
                    placeholderText: qsTr("原文术语，如 API")
                }
                FluTextBox {
                    id: termTargetBox
                    Layout.fillWidth: true
                    placeholderText: qsTr("标准译文，如 应用程序接口")
                    onAccepted: addTerm()
                }
                FluButton {
                    text: qsTr("添加")
                    onClicked: addTerm()
                }
            }

            ListModel {
                id: termModel
            }

            ListView {
                id: termList
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 220)
                model: termModel
                clip: true
                spacing: 4
                delegate: RowLayout {
                    width: termList.width
                    spacing: 8
                    FluText {
                        text: "“%1” →".arg(model.source)
                        Layout.preferredWidth: Math.max(90, termList.width * 0.3)
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                    }
                    // 行内编辑标准译文（自动提取的占位项在此填写；失焦保存）
                    FluTextBox {
                        id: termTransBox
                        Layout.fillWidth: true
                        text: model.translation
                        placeholderText: qsTr("标准译文")
                        onEditingFinished: updateTermTranslation(model.source, text)
                    }
                    FluButton {
                        text: qsTr("删除")
                        onClicked: removeTerm(model.source)
                    }
                }
                FluText {
                    anchors.centerIn: parent
                    visible: termModel.count === 0
                    text: qsTr("暂无术语，添加后将注入翻译提示词并做一致性校验")
                    color: FluTheme.fontSecondaryColor
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                FluText {
                    text: qsTr("共 %1 条术语").arg(termModel.count)
                    color: FluTheme.fontSecondaryColor
                    Layout.fillWidth: true
                }
                FluButton {
                    text: qsTr("从文档提取")
                    onClicked: extractTerms()
                }
                FluButton {
                    text: qsTr("清空术语表")
                    enabled: termModel.count > 0
                    onClicked: clearTerms()
                }
            }
        }
    }

    // ---------- 术语提取弹窗（迭代4） ----------
    // ListModel 必须声明在页面级：FluContentDialog 的 contentDelegate 由 Loader 延迟创建，
    // 组件内 id 无法被页面级函数访问（跨 Component 边界），且 open() 时才实例化
    ListModel {
        id: termCandidateModel
    }
    FluContentDialog {
        id: extractDialog
        title: qsTr("从文档提取术语")
        negativeText: qsTr("取消")
        positiveText: qsTr("添加选中")
        contentDelegate: Component {
            ColumnLayout {
                // FluContentDialog implicitWidth 400，内容宽度须小于对话框宽度避免裁剪
                width: 380
                spacing: 8
                FluText {
                    text: qsTr("勾选要加入术语表的词（译文初始为空，添加后请填写标准译文）：")
                    color: FluTheme.fontSecondaryColor
                    wrapMode: Text.Wrap
                }
                ListView {
                    id: extractListView
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 240)
                    model: termCandidateModel
                    clip: true
                    spacing: 2
                    delegate: RowLayout {
                        width: extractListView.width
                        spacing: 8
                        FluCheckBox {
                            checked: model.checked
                            onToggled: termCandidateModel.setProperty(index, "checked", checked)
                        }
                        FluText {
                            text: qsTr("%1（出现 %2 次）").arg(model.word).arg(model.count)
                            Layout.preferredWidth: 150
                            Layout.alignment: Qt.AlignVCenter
                            elide: Text.ElideRight
                        }
                        // 行内填标准译文（AI 建议译文回填；空 = 占位）
                        FluTextBox {
                            Layout.fillWidth: true
                            text: model.translation
                            placeholderText: qsTr("标准译文")
                            onEditingFinished: termCandidateModel.setProperty(index, "translation", text)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FluCheckBox {
                        text: qsTr("全选")
                        onToggled: extractSelectAll(checked)
                    }
                    // AI 建议译文：仅配置了网络大模型 API 时可用（按文档上下文猜测标准译文）
                    FluButton {
                        text: qsTr("AI 建议译文")
                        enabled: translationService.termSuggestionAvailable()
                        onClicked: suggestTermTranslations()
                    }
                    Item { Layout.fillWidth: true }
                }
            }
        }
        onPositiveClicked: addExtractedTerms()
    }

    // ---------- 卡片：显示（原文/批注字号滑动条） ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardDisplayCol.implicitHeight + 32

        ColumnLayout {
            id: cardDisplayCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.FontIncrease; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("显示"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("原文字号 %1").arg(page.originalFontSize)
                    font.pixelSize: tokens.fontMenu
                    Layout.preferredWidth: 120
                }
                FluSlider {
                    Layout.fillWidth: true
                    from: 10
                    to: 24
                    stepSize: 1
                    value: page.originalFontSize
                    onMoved: {
                        page.originalFontSize = Math.round(value)
                        configService.set("ui", "originalFontSize", page.originalFontSize)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("批注字号 %1").arg(page.commentFontSize)
                    font.pixelSize: tokens.fontMenu
                    Layout.preferredWidth: 120
                }
                FluSlider {
                    Layout.fillWidth: true
                    from: 8
                    to: 24
                    stepSize: 1
                    value: page.commentFontSize
                    onMoved: {
                        page.commentFontSize = Math.round(value)
                        configService.set("ui", "commentFontSize", page.commentFontSize)
                    }
                }
            }
            FluText {
                text: qsTr("字号即时生效并持久化；编辑器内右键「显示设置」可快速调整，与本页完全同步。")
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    // ---------- 卡片：查找（选项从 Ribbon 移入设置，保持功能区简洁） ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardFindCol.implicitHeight + 32

        ColumnLayout {
            id: cardFindCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.Search; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("查找"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("区分大小写")
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
                FluToggleSwitch {
                    checked: page.findCaseSensitive
                    onToggled: {
                        page.findCaseSensitive = checked
                        configService.set("ui", "findCaseSensitive", checked)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("整词匹配")
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
                FluToggleSwitch {
                    checked: page.findWholeWord
                    onToggled: {
                        page.findWholeWord = checked
                        configService.set("ui", "findWholeWord", checked)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                FluText {
                    text: qsTr("模糊查找")
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                }
                FluToggleSwitch {
                    checked: page.findFuzzy
                    onToggled: {
                        page.findFuzzy = checked
                        configService.set("ui", "findFuzzy", checked)
                    }
                }
            }
            FluText {
                text: qsTr("模糊查找：查询字符按顺序出现即匹配（如查「tran」可命中 translation），不要求连续。")
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }
    }

    // ---------- 卡片：帮助与引导（迭代4b：不默认首启弹窗，手动打开） ----------
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: cardGuideCol.implicitHeight + 32

        ColumnLayout {
            id: cardGuideCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.Info; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("帮助与引导"); font.pixelSize: tokens.fontTitle; font.bold: true }
            }

            FluText {
                text: qsTr("快速了解 Translex 的核心功能与快捷键。")
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            FluButton {
                text: qsTr("功能引导")
                onClicked: guideDialog.open()
            }
        }
    }

    // ---------- 卡片：调试（迭代5：服务健康度/插件诊断/路径） ----------
    // 注意：implicitHeight 用固定值而非 cardDebugCol.implicitHeight + 32——实测动态绑定
    // 在含 Repeater 列表时少算尾部行（插件加载/路径行被裁），固定值最稳妥。
    // 高度按实测行高估算：10 行服务（行距 ~46px）+ 2 标题 + 插件行 + 配置/日志行（各 40px）
    // + spacing/margins ≈ 720+；实测 700 时配置/日志行仍被窗口底缘裁掉，取 820（留余量）。
    Rectangle {
        Layout.fillWidth: true
        radius: tokens.radiusCard
        color: tokens.bgCard
        border.color: FluTheme.dividerColor
        implicitHeight: 820

        ColumnLayout {
            id: cardDebugCol
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                FluIcon { iconSource: FluentIcons.DeveloperTools; iconSize: 16; color: FluTheme.primaryColor }
                FluText { text: qsTr("调试"); font.pixelSize: tokens.fontTitle; font.bold: true }
                Item { Layout.fillWidth: true }
                FluButton {
                    text: qsTr("刷新")
                    onClicked: refreshDebugHealth()
                }
            }

            // 服务健康度（healthReport 聚合：状态色点 + 消息）
            FluText {
                text: qsTr("服务健康度")
                font.pixelSize: tokens.fontMenu
                font.bold: true
                color: FluTheme.fontSecondaryColor
            }
            Repeater {
                model: debugHealthModel
                RowLayout {
                    width: cardDebugCol.width - 32
                    spacing: 8
                    Rectangle {
                        width: 8
                        height: 8
                        radius: tokens.radiusControl
                        color: model.status === "ok" ? tokens.success : tokens.error
                        Layout.alignment: Qt.AlignVCenter
                    }
                    FluText {
                        text: model.displayName
                        font.pixelSize: tokens.fontMenu
                        font.bold: true
                        Layout.preferredWidth: 130
                        elide: Text.ElideRight
                    }
                    FluText {
                        text: model.version
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontTertiaryColor
                        Layout.preferredWidth: 60
                    }
                    FluText {
                        text: model.message
                        font.pixelSize: tokens.fontCaption
                        color: model.status === "ok" ? FluTheme.fontSecondaryColor : tokens.error
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                    }
                }
            }

            // 插件加载诊断
            FluText {
                text: qsTr("插件加载")
                font.pixelSize: tokens.fontMenu
                font.bold: true
                color: FluTheme.fontSecondaryColor
            }
            FluText {
                text: pluginErrorsText
                font.pixelSize: tokens.fontCaption
                color: pluginErrorsText === qsTr("无插件加载错误") ? tokens.success : tokens.error
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }

            // 配置/日志路径（wrapMode 文本给显式 preferredHeight：与既有卡片模式一致，
            // 避免 implicitHeight 按单行算导致卡片高度不足、底部内容被裁）
            FluText {
                text: qsTr("配置文件：%1").arg(configService.configFilePath())
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.preferredHeight: 40
            }
            FluText {
                text: qsTr("日志文件：%1").arg(serviceRegistry.serviceById("appGuard").logFile())
                font.pixelSize: tokens.fontCaption
                color: FluTheme.fontSecondaryColor
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.preferredHeight: 40
            }
        }
    }

    // ---------- 功能引导弹窗（静态内容清单） ----------
    FluContentDialog {
        id: guideDialog
        title: qsTr("功能引导")
        negativeText: ""
        positiveText: qsTr("知道了")
        contentDelegate: Component {
            ColumnLayout {
                width: 380
                spacing: 8
                Repeater {
                    model: [
                        qsTr("编辑器：逐行编辑，Enter 拆分 / Backspace 行首合并，右键菜单快速操作"),
                        qsTr("翻译：选中行或全部待译行，支持 Ollama / 云端 / OpenAI 风格 API 三后端"),
                        qsTr("批注：译文以批注形式附着在行尾，可导出/导入 JSON"),
                        qsTr("术语表：设置页维护术语，翻译时注入提示词并做一致性校验，可从文档自动提取"),
                        qsTr("自动保存：每 60 秒快照未保存更改，崩溃后启动时提示恢复（设置页可关闭）"),
                        qsTr("朗读：选中行 TTS 朗读（有译文读译文，否则读原文）"),
                        qsTr("大文件：超过 5 万行 / 200MB 自动进入受限模式（纯文本显示，禁编辑/翻译）"),
                        qsTr("格式：.trx 完整往返（含批注），.txt / .docx / .pdf / .md 导出")
                    ]
                    delegate: FluText {
                        Layout.fillWidth: true
                        text: "• " + modelData
                        font.pixelSize: tokens.fontMenu
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
    }
}
