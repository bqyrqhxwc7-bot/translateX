import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import FluentUI
import Translex
import Translex.Services 1.0

FluContentPage {
    id: page
    // NoStack 模式下由 FluNavigationView 的 FluLoader 直接加载，须显式填满父级
    //（否则页面宽度不跟随窗口，全屏/最大化后右侧出现透明空白）
    anchors.fill: parent
    title: qsTr("编辑")
    launchMode: FluPageType.SingleTask

    // 视觉语言 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

    // 页面标题：自定义 header（FluPage 默认用 Title 字号偏大，缩小以留出更多编辑空间）
    header: Item {
        implicitHeight: 28
        FluText {
            text: page.title
            font.pixelSize: tokens.fontBody
            font.bold: true
            color: FluTheme.fontPrimaryColor
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // 核心文档模型（应用级单例，main_qml.cpp 提供 context property）：
    // NoStack 页面重建时内容跨页面保留，未保存编辑不丢失。
    property int currentLine: -1

    // 拖放打开文件（txt/trx/docx/pdf）：覆盖全页，拖入直接打开
    DropArea {
        anchors.fill: parent
        z: 100
        onEntered: (drag) => {
            drag.accepted = drag.hasUrls
        }
        onDropped: (drag) => {
            if (!drag.hasUrls) {
                return
            }
            const urls = drag.urls
            if (urls.length === 0) {
                return
            }
            const path = urls[0].toString().replace(/^file:\/\//, "")
            if (documentManager.openFile(path)) {
                statusLabel.text = qsTr("已打开：%1").arg(documentManager.documentName())
                statusIconSource = 0
                statusIconColor = FluTheme.fontSecondaryColor
            } else {
                statusLabel.text = qsTr("打开失败：%1").arg(path)
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            }
            refreshDocStatus()
            chapterService.rebuild()
        }
    }

    // 翻译服务（主进程提供，context property 全局可见）
    readonly property var translator: translationService

    // 图片行渲染：从文档 meta.images 取 base64 → data URI（docx 纯图段显示）
    // 结果缓存（键 = 文档路径 + imageId：同一页面内切换文档不会串图）
    property var _imageUriCache: ({})
    property var _imageUriMetaVersion: 0
    // 文档版本号：documentChanged 后 +1（此时 documentMeta 已就绪），
    // 强制 rich 文本/图片绑定重新求值（openFile 时 setLines 触发绑定早于
    // meta 赋值，[图片] 占位替换必须等版本号变化后重跑）
    property int _docVersion: 0
    function imageSource(imageId) {
        if (!imageId) return ""
        const v = page._docVersion   // 绑定追踪：换文档后强制重求值
        const key = documentManager.currentPath() + "|" + imageId
        if (page._imageUriCache[key] !== undefined) return page._imageUriCache[key]
        const meta = documentManager.documentMeta()
        const images = meta && meta.images ? meta.images : []
        for (const img of images) {
            if (img.id === imageId && img.dataBase64) {
                const mime = img.format === "png" ? "image/png"
                           : img.format === "jpg" || img.format === "jpeg" ? "image/jpeg"
                           : img.format === "gif" ? "image/gif" : "image/png"
                const uri = "data:" + mime + ";base64," + img.dataBase64
                page._imageUriCache[key] = uri
                return uri
            }
        }
        return ""
    }

    // rich 行显示文本：把 [图片] 占位替换为真实 <img>（data URI 内嵌，Text 支持 img 标签）
    function richTextFor(row) {
        const v = page._docVersion   // 绑定追踪：换文档后强制重求值（meta 已就绪）
        let t = row.model.rich || row.model.text
        if (t.indexOf("[图片]") < 0) {
            return t
        }
        const ids = row.model.imageIds || []
        let i = 0
        while (t.indexOf("[图片]") >= 0 && i < ids.length) {
            const uri = page.imageSource(ids[i])
            if (uri) {
                t = t.replace("[图片]", '<img src="' + uri + '" height="60">')
            }
            ++i
        }
        return t
    }

    // 状态栏图标（0=无图标）
    property int statusIconSource: 0
    property color statusIconColor: FluTheme.fontSecondaryColor
    // 大文件受限模式（>5 万行 / >200MB，DocumentManager 打开时自动置位）：
    // 禁富文本/图片渲染、批注编辑、翻译；编辑/滚动/查找/章节正常
    readonly property bool limited: documentModel.limitedMode
    // 翻译进度状态
    property bool translating: false
    property int progressDone: 0
    property int progressTotal: 0
    // 翻译面板宽度（分隔条可拖拽调整，200~380）
    property real panelWidth: 280
    // 多选翻译：Ctrl+点击 加入/移出选中集
    property var selectedLines: []
    // 行内编辑框当前选区（delegate 内 TextEdit 经此中转，供右键菜单「朗读选区」）
    property string editorSelectedText: ""
    // 翻译面板模式（ppt=Ribbon 功能区 / floating=独立浮窗）与默认显示
    property string panelMode: "floating"
    property bool panelVisible: false
    // 翻译面板当前是否显示（运行状态；同步持久化到 ui.translatePanelVisible）
    property bool panelShown: false
    // Ribbon 功能区状态（Component 内按钮通过 page.xxx 访问）
    property bool canUndo: false
    property bool canRedo: false
    property bool hasRecent: false
    // 章节标签状态
    property var chapterTitles: []
    property int currentChapterIndex: -1
    // 批注标签状态
    property int commentCount: 0
    property var commentLines: []
    // 查找标签状态
    property int findResultCount: 0
    property string findQuery: ""
    property bool findCaseSensitive: false
    property bool findWholeWord: false
    property bool findFuzzy: false      // 模糊查找（子序列匹配，设置页开关）
    property var findMatches: []   // 当前查询的命中行号列表（供行高亮）
    // 显示设置：字号独立可设（原文/批注），持久化到 config；draftLine = 批注编辑会话
    property int originalFontSize: 14
    property int commentFontSize: 12
    property int commentDraftLine: -1

    // 当前行变化 → 同步「章节」指示；离开批注编辑会话时清除 draft。
    // 富文本/图片行的降级（编辑即降级）放在 lineEditor.onTextChanged——
    // 仅在真正输入时触发，浏览点击不破坏显示层。
    onCurrentLineChanged: {
        page.currentChapterIndex = page.currentLine >= 0
            ? chapterService.chapterAtLine(page.currentLine) : -1
        if (page.commentDraftLine >= 0 && page.commentDraftLine !== page.currentLine) {
            page.commentDraftLine = -1
        }
    }

    function isLineSelected(lineNumber) {
        return page.selectedLines.indexOf(lineNumber) >= 0
    }

    function isFindMatch(lineNumber) {
        return page.findMatches.indexOf(lineNumber) >= 0
    }

    Component.onCompleted: {
        // 批注统一由 CommentService 管理（单一数据源），模型经 provider 委托
        documentModel.setCommentProvider(commentService)
        // 文档管理 / 章节 / 查找服务关联当前文档模型
        documentManager.setDocument(documentModel)
        documentManager.setComments(commentService)
        chapterService.setDocument(documentModel)
        findService.setDocument(documentModel)
        // 文档切换：meta（images 等）就绪后版本号 +1，强制 rich 文本/图片绑定重求值
        //（openFile 时 setLines 先于 meta 赋值触发绑定，[图片] 占位需重跑替换）
        documentManager.documentChanged.connect(function () {
            page._docVersion += 1
            page._imageUriCache = {}
        })
        // 仅首次（模型为空）加载示例文档：模型已提升到应用级，若每次重建都调用
        // loadDemoDocument() 会把用户编辑/打开的内容覆盖掉（编辑丢失的根因之一）
        if (documentModel.lineCount() === 0) {
            loadDemoDocument()
        }
        chapterService.rebuild()
        // 最近文件菜单改在弹出前重建（见 recentMenu.onAboutToShow），此处仅初始化状态
        page.hasRecent = documentManager.recentFiles().length > 0
        refreshDocStatus()
        // 翻译面板模式/默认显示（ConfigService 持久化；ppt=Ribbon 标签，floating=浮窗）
        let panelMode0 = String(configService.get("ui", "translatePanelMode") || "ppt")
        if (panelMode0 === "docked") {
            // 旧版 docked（停靠面板）已由 Ribbon 标签替代，迁移为 ppt
            panelMode0 = "ppt"
            configService.set("ui", "translatePanelMode", "ppt")
        }
        page.panelMode = panelMode0
        // 启动时主窗口尚未完全显示，Qt.Tool 浮窗（transientParent）立即 show 可能竞态失败
        //（症状：设置了启动显示但浮窗不出现，切一下页面才出现）。延迟 350ms 再显示。
        page.panelShown = false
        if (panelMode0 === "floating") {
            floatShowTimer.start()
        }
        // 显示设置：原文/批注字号、查找选项（设置页与右键「显示设置」共用同一配置）
        const ofs = Number(configService.get("ui", "originalFontSize"))
        page.originalFontSize = isFinite(ofs) && ofs > 0 ? Math.round(ofs) : 14
        const cfs = Number(configService.get("ui", "commentFontSize"))
        page.commentFontSize = isFinite(cfs) && cfs > 0 ? Math.round(cfs) : 12
        page.findCaseSensitive = Boolean(configService.get("ui", "findCaseSensitive"))
        page.findWholeWord = Boolean(configService.get("ui", "findWholeWord"))
        page.findFuzzy = Boolean(configService.get("ui", "findFuzzy"))
        // 自动保存（迭代4）：配置开关同步到 DocumentManager；启动时检测崩溃残留
        documentManager.setAutosaveEnabled(Boolean(configService.get("ui", "autosaveEnabled")))
        if (documentManager.takeAutosavePrompt()) {
            const desc = documentManager.autosaveDescription()
            const dirtyWarn = documentManager.isDirty()
                ? qsTr("\n\n注意：当前文档有未保存的修改，恢复将覆盖当前内容。")
                : ""
            restoreAutosaveDialog.message = qsTr("检测到上次未保存的更改（%1），是否恢复？%2")
                                            .arg(desc).arg(dirtyWarn)
            restoreAutosaveDialog.open()
        }
        // 浮窗位置在 floatWindow.Component.onCompleted 中恢复（真实窗口屏幕坐标）
        // 翻译选项已由 TranslationService 从 ConfigService 持久化恢复，无需在此强制覆盖
    }

    // 通知条：NoStack 模式下页面由 FluLoader 加载，Window.window 附加属性解析失败
    // （运行时报 showWarning of null），故改用页面内 FluInfoBar 实例
    FluInfoBar {
        id: infoBar
        root: page
    }

    // 自动保存开关（迭代4）：设置页修改后立即生效（无需等导航回主页）
    Connections {
        target: configService
        function onConfigChanged(section, key, value) {
            if (section === "ui" && key === "autosaveEnabled") {
                documentManager.setAutosaveEnabled(Boolean(value))
            }
        }
    }

    // 翻译结果回调 → 写入批注（走 CommentService，单一数据源）
    Connections {
        target: translationService
        function onLineTranslated(lineNumber, text, success) {
            translationHistoryService.record(lineNumber, documentModel.lineText(lineNumber), text, success)
            if (success && text) {
                commentService.setComment(lineNumber, text)
                statusLabel.text = qsTr("第 %1 行翻译完成").arg(lineNumber + 1)
                statusIconSource = FluentIcons.Message
                statusIconColor = tokens.success
            } else {
                // 翻译失败：清除该行旧译文批注（避免残留过时/回显原文）
                commentService.removeComment(lineNumber)
                statusLabel.text = qsTr("第 %1 行翻译失败").arg(lineNumber + 1)
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            }
            // 统计在 onBatchFinished 统一刷新（避免批量翻译时每行全量 stats() → O(N²)）
        }
        function onBatchFinished(total, ok, failed) {
            page.translating = false
            statusLabel.text = qsTr("翻译完成：成功 %1 / 失败 %2（共 %3 行）")
                                .arg(ok).arg(failed).arg(total)
            refreshDocStatus()
            // 有质量告警 → 弹出复核面板（收集在 onQualityWarning）
            if (qualityWarnings.count > 0) {
                qualityReport.visible = true
            }
        }
        function onTranslationStarted(total) {
            page.translating = true
            page.progressTotal = total
            page.progressDone = 0
            qualityWarnings.clear()   // 新一批翻译，清空上一批质量告警
            qualityReport.visible = false
        }
        function onTranslationProgress(done, total) {
            page.progressDone = done
            page.progressTotal = total
            statusLabel.text = qsTr("正在翻译 %1/%2 行...").arg(done).arg(total)
        }
        function onTranslationCanceled() {
            page.translating = false
            statusLabel.text = qsTr("翻译已取消")
            statusIconSource = FluentIcons.Cancel
            statusIconColor = FluTheme.fontSecondaryColor
        }
        function onTranslationFailed(lineNumber, errorMessage) {
            const loc = lineNumber >= 0 ? qsTr("第 %1 行：").arg(lineNumber + 1) : ""
            infoBar.showWarning(loc + errorMessage, 6000)
        }
        function onQualityWarning(lineNumber, issue) {
            const loc = lineNumber >= 0 ? qsTr("第 %1 行：").arg(lineNumber + 1) : ""
            infoBar.showWarning(loc + issue, 6000)
            // 收集到复核面板（onBatchFinished 汇总展示）
            qualityWarnings.append({ line: lineNumber, issue: issue })
        }
        // 术语建议译文（提取弹窗）：填充勾选术语的建议译文（可修改后添加）
        function onTermSuggestionsReady(suggestions, ok, errorMessage) {
            if (!ok) {
                statusLabel.text = errorMessage
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
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
            statusLabel.text = qsTr("已为 %1 个术语填入建议译文（可修改）").arg(filled)
            statusIconSource = 0
            statusIconColor = FluTheme.fontSecondaryColor
        }
    }

    // TTS 朗读状态（迭代3）：逐行朗读时高亮当前行；无引擎时提示
    Connections {
        target: textToSpeechService
        function onLineStarted(lineNumber) {
            if (lineNumber >= 0 && page.currentLine !== lineNumber) {
                page.focusLine(lineNumber)
            }
        }
        function onUnavailable() {
            infoBar.showWarning(qsTr("系统无 TTS 引擎，无法朗读（Linux 需 speech-dispatcher）"), 5000)
        }
    }

    // 编辑历史 → 刷新撤销/重做按钮（Ribbon 内按钮经 page 属性访问）
    Connections {
        target: documentModel
        function onUndoStackChanged() {
            page.canUndo = documentModel.canUndo()
            page.canRedo = documentModel.canRedo()
        }
    }

    // 设置页修改「翻译面板」配置 → 实时生效（NoStack 页面不会自动重建）
    Connections {
        target: configService
        function onConfigChanged(section, key, value) {
            if (section !== "ui") {
                return
            }
            // 浮层 visible 绑定 panelMode/panelShown，仅需更新状态即可自动显示/隐藏
            if (key === "translatePanelMode") {
                page.panelMode = String(value || "ppt")
            } else if (key === "translatePanelVisible") {
                page.panelShown = Boolean(value)
            }
        }
    }

    // 章节 / 批注 / 查找 标签状态刷新（service 变更 → 更新 Ribbon 功能区）
    Connections {
        target: chapterService
        function onChaptersChanged() {
            page.refreshChapterState()
        }
    }
    Connections {
        target: commentService
        function onCommentChanged(lineNumber) {
            page.refreshCommentState()
        }
        function onCommentsReset() {
            page.refreshCommentState()
        }
    }
    Connections {
        target: findService
        function onSearchCompleted(resultCount) {
            page.findResultCount = resultCount
        }
    }

    // 编辑快捷键（Ctrl+Z / Ctrl+Y）
    Shortcut {
        sequence: "Ctrl+Z"
        onActivated: {
            if (documentModel.canUndo()) {
                documentModel.undo()
                afterUndoRedo()
            }
        }
    }
    Shortcut {
        sequence: "Ctrl+Y"
        onActivated: {
            if (documentModel.canRedo()) {
                documentModel.redo()
                afterUndoRedo()
            }
        }
    }
    // 翻译快捷键（与旧版 QtWidgets 一致）
    Shortcut {
        sequence: "Ctrl+Alt+T"
        onActivated: translateCurrent()
    }
    Shortcut {
        sequence: "Ctrl+Alt+Shift+T"
        onActivated: translateAllPending()
    }
    // 快捷键总览（? 呼出/关闭）
    Shortcut {
        sequence: "?"
        onActivated: shortcutHelp.visible = !shortcutHelp.visible
    }

    // 撤销/重做后：钳制当前行并刷新编辑行文本（TextEdit 用户输入会解除绑定）
    function afterUndoRedo() {
        if (currentLine >= documentModel.lineCount()) {
            currentLine = documentModel.lineCount() - 1
        }
        if (currentLine >= 0) {
            const del = lineView.itemAtIndex(currentLine)
            if (del) {
                del.refreshEditor()
            }
        }
    }

    // 清除全部译文/批注（清理历史残留，如旧版本回显的原文批注）
    function clearAllComments() {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        commentService.clear()
        statusLabel.text = qsTr("已清除全部译文/批注")
        statusIconSource = 0
        statusIconColor = FluTheme.fontSecondaryColor
    }

    // ---------- 文档操作 ----------
    function urlToPath(url) {
        let s = url.toString()
        s = s.replace(/^file:\/\//, "")
        if (s.length > 2 && s[0] === "/" && s[2] === ":") {
            s = s.substring(1)   // /C:/x → C:/x
        }
        return decodeURIComponent(s)
    }

    function openDocument() {
        openDialog.open()
    }

    function saveDocument() {
        if (documentManager.saveFile()) {
            refreshDocStatus()
        } else {
            saveAsDialog.open()
        }
    }

    function saveDocumentAs() {
        saveAsDialog.open()
    }

    function newDocument() {
        if (documentManager.isDirty()) {
            confirmNewDialog.open()
        } else {
            doNewDocument()
        }
    }

    function doNewDocument() {
        documentManager.newDocument([])
        refreshDocStatus()
        statusLabel.text = qsTr("已新建文档")
        statusIconSource = 0
        statusIconColor = FluTheme.fontSecondaryColor
        chapterService.rebuild()
    }

    function openRecent(path) {
        if (documentManager.openFile(path)) {
            statusLabel.text = qsTr("已打开：%1").arg(documentManager.documentName())
            statusIconSource = 0
            statusIconColor = FluTheme.fontSecondaryColor
        } else {
            statusLabel.text = qsTr("打开失败：%1").arg(path)
            statusIconSource = FluentIcons.Warning
            statusIconColor = tokens.error
        }
        refreshDocStatus()
        chapterService.rebuild()
    }

    // 刷新状态栏：文档名 + 修改标记 + 文档统计（迭代4）
    function refreshDocStatus() {
        documentNameLabel.text = documentManager.documentName()
        dirtyDot.visible = documentManager.isDirty()
        const s = documentModel.stats()
        docStatsLabel.text = qsTr("共 %1 行 · %2 字 · %3 条批注")
                             .arg(s.lines).arg(s.chars).arg(s.comments)
    }

    // 最近文件菜单重建（打开文件后自动记录）
    function rebuildRecentMenu() {
        // T.Menu 无 clear()，用 removeItem 清空
        while (recentMenu.count > 0) {
            recentMenu.removeItem(recentMenu.itemAt(0))
        }
        const files = documentManager.recentFiles()
        for (let i = 0; i < files.length; ++i) {
            const item = recentItemComp.createObject(recentMenu, { text: files[i] })
            item.triggered.connect(() => openRecent(files[i]))
        }
        page.hasRecent = files.length > 0
    }

    function loadDemoDocument() {
        const sample = [
            "第一章：开始",
            "这是第一行待翻译的英文文本。",
            "This is the second line to translate.",
            "The quick brown fox jumps over the lazy dog.",
            "Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
            "第二段：继续",
            "Another paragraph to be translated into Chinese.",
            "Etiam quis risus vel justo tempor ultricies.",
            "Donec vitae nisi vitae nunc faucibus commodo.",
            "第三段：结束",
            "The final paragraph of the sample document.",
            "Curabitur malesuada est vitae nunc tempus, sed dictum velit feugiat."
        ];
        documentModel.setLines(sample);
        documentModel.setComment(2, "这是第一条翻译批注示例。");
        documentModel.setComment(4, "Lorem 示例段落的中文翻译。");
        statusLabel.text = qsTr("已加载 %1 行示例文档").arg(documentModel.lineCount())
    }

    // 聚焦指定行并进入编辑
    function focusLine(lineNumber) {
        currentLine = lineNumber
        lineView.currentIndex = lineNumber
        lineView.positionViewAtIndex(lineNumber, ListView.Contain)
        const del = lineView.itemAtIndex(lineNumber)
        if (del) {
            del.startEdit()
        }
    }

    // 翻译历史弹窗（迭代4b）：打开时从 service 拉取最新条目
    function openHistoryDialog() {
        historyModel.clear()
        const entries = translationHistoryService.entries()
        for (let i = 0; i < entries.length; i++) {
            const e = entries[i]
            historyModel.append({
                line: e.line,
                source: e.source,
                translated: e.translated,
                success: e.success,
                time: e.time
            })
        }
        historyDialog.open()
    }

    // Enter 换行：在光标处拆分当前行
    function splitCurrentLine(cursorPosition) {
        const line = documentModel.lineText(currentLine)
        const before = line.substring(0, cursorPosition)
        const after = line.substring(cursorPosition)
        documentModel.updateLineText(currentLine, before)
        const newIndex = documentModel.insertLine(currentLine + 1, after)
        currentLine = newIndex
        focusLine(currentLine)
    }

    // Backspace 行首：合并到上一行
    function mergeWithPrevious() {
        if (currentLine <= 0) {
            return
        }
        const prevText = documentModel.lineText(currentLine - 1)
        const curText = documentModel.lineText(currentLine)
        documentModel.updateLineText(currentLine - 1, prevText + curText)
        const newIndex = documentModel.removeLine(currentLine)
        currentLine = newIndex - 1
        focusLine(currentLine)
    }

    // 大文件受限模式拦截提示（翻译/批注编辑入口共用）
    function limitedBlocked() {
        statusLabel.text = qsTr("大文件受限模式：已禁用翻译与批注编辑")
        statusIconSource = FluentIcons.Warning
        statusIconColor = tokens.warning
    }

    // 翻译当前行
    function translateCurrent() {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        if (currentLine < 0) {
            statusLabel.text = qsTr("请先点击选择一行")
            return
        }
        const text = documentModel.lineText(currentLine)
        if (!text.trim()) {
            statusLabel.text = qsTr("当前行为空，无需翻译")
            return
        }
        if (translator.isTargetLanguageText(text)) {
            statusLabel.text = qsTr("当前行已是目标语言，无需翻译")
            statusIconSource = FluentIcons.Info
            statusIconColor = FluTheme.fontSecondaryColor
            return
        }
        // 传完整文档（translateBatchSync 用行号取上下文行，sourceLines 须为全文）
        const all = []
        for (let i = 0; i < documentModel.lineCount(); ++i) {
            all.push(documentModel.lineText(i))
        }
        statusLabel.text = qsTr("正在翻译第 %1 行...").arg(currentLine + 1)
        statusIconSource = FluentIcons.Sync
        statusIconColor = FluTheme.primaryColor
        translator.translateLines([currentLine], all)
    }

    // 翻译所有待译行（支持多语言：所有非空、无批注、且非目标语言的行）
    function translateAllPending() {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        const lines = []
        const all = []
        let skipped = 0
        for (let i = 0; i < documentModel.lineCount(); ++i) {
            const text = documentModel.lineText(i)
            all.push(text)
            if (!text.trim()) continue
            if (!documentModel.hasCommentAt(i)) {
                if (translator.isTargetLanguageText(text)) {
                    ++skipped
                    continue
                }
                lines.push(i)
            }
        }
        if (lines.length === 0) {
            if (skipped > 0) {
                // 全部是目标语言：多半是源/目标语言配置与文档语言相同，明确引导
                statusLabel.text = qsTr("没有需要翻译的行（已跳过 %1 行目标语言文本）——文档可能已是目标语言，请检查设置页的源语言/目标语言").arg(skipped)
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            } else {
                statusLabel.text = qsTr("没有可翻译的行")
            }
            return
        }
        statusLabel.text = skipped > 0
            ? qsTr("正在批量翻译 %1 行（已跳过 %2 行目标语言文本）...").arg(lines.length).arg(skipped)
            : qsTr("正在批量翻译 %1 行...").arg(lines.length)
        statusIconSource = FluentIcons.Sync
        statusIconColor = FluTheme.primaryColor
        translator.translateLines(lines, all)
    }

    // 翻译选中的行（Ctrl+点击多选；跳过空行/目标语言行）
    function translateSelected() {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        if (selectedLines.length === 0) {
            statusLabel.text = qsTr("请先选中要翻译的行（Ctrl+点击可多选）")
            return
        }
        const lines = []
        const all = []
        let skipped = 0
        for (let i = 0; i < documentModel.lineCount(); ++i) {
            all.push(documentModel.lineText(i))
        }
        for (const ln of selectedLines) {
            const text = documentModel.lineText(ln)
            if (!text.trim()) continue
            if (translator.isTargetLanguageText(text)) {
                ++skipped
                continue
            }
            lines.push(ln)
        }
        if (lines.length === 0) {
            if (skipped > 0) {
                statusLabel.text = qsTr("选中的行均为目标语言，无需翻译——请检查设置页的源语言/目标语言")
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            } else {
                statusLabel.text = qsTr("没有可翻译的选中行")
            }
            return
        }
        statusLabel.text = skipped > 0
            ? qsTr("正在翻译选中 %1 行（已跳过 %2 行目标语言文本）...").arg(lines.length).arg(skipped)
            : qsTr("正在翻译选中 %1 行...").arg(lines.length)
        statusIconSource = FluentIcons.Sync
        statusIconColor = FluTheme.primaryColor
        translator.translateLines(lines, all)
    }

    // ---------- 术语自动提取（翻译功能区入口，与设置页共用 TranslationService 术语表） ----------
    function extractTermsFromDoc() {
        if (page.limited) {
            statusLabel.text = qsTr("大文件受限模式下不支持术语提取")
            statusIconSource = FluentIcons.Warning
            statusIconColor = tokens.error
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
            termCandidateModel.append({ word: c.word, count: c.count, checked: false, translation: "" })
        }
        if (termCandidateModel.count === 0) {
            statusLabel.text = qsTr("未提取到高频词（词需出现 3 次以上；英文/标识符/中文均支持）")
            statusIconSource = FluentIcons.Warning
            statusIconColor = tokens.error
            return
        }
        termExtractDialog.open()
    }

    // 请求大模型按文档上下文建议勾选术语的译文（仅网络大模型后端可用）
    function suggestTermTranslations() {
        const terms = []
        for (let i = 0; i < termCandidateModel.count; ++i) {
            if (termCandidateModel.get(i).checked) {
                terms.push(termCandidateModel.get(i).word)
            }
        }
        if (terms.length === 0) {
            statusLabel.text = qsTr("请先勾选要建议译文的术语")
            return
        }
        const lines = []
        const count = Math.min(documentModel.lineCount(), 50000)
        for (let i = 0; i < count; ++i) {
            lines.push(documentModel.lineText(i))
        }
        statusLabel.text = qsTr("正在请求大模型建议 %1 个术语译文...").arg(terms.length)
        statusIconSource = FluentIcons.Sync
        statusIconColor = FluTheme.primaryColor
        translationService.suggestTermTranslations(terms, lines)
    }

    function termExtractSelectAll(checked) {
        for (let i = 0; i < termCandidateModel.count; ++i) {
            termCandidateModel.setProperty(i, "checked", checked)
        }
    }

    // 勾选的词加入术语表（含已填译文；空译文为占位，设置页可继续补填）
    function addExtractedTermsFromDoc() {
        const glossary = translationService.glossary()
        let added = 0
        for (let i = 0; i < termCandidateModel.count; ++i) {
            if (!termCandidateModel.get(i).checked) {
                continue
            }
            const w = termCandidateModel.get(i).word
            if (glossary[w] === undefined) {
                glossary[w] = termCandidateModel.get(i).translation.trim()
                ++added
            }
        }
        if (added > 0) {
            translationService.setGlossary(glossary)
            statusLabel.text = qsTr("已添加 %1 个术语；标准译文可在「设置→术语表」补填").arg(added)
            statusIconSource = 0
            statusIconColor = FluTheme.fontSecondaryColor
        }
    }

    // ---------- TTS 朗读（迭代3，独立 service） ----------
    // 朗读选中行：有译文批注读译文（听译文校验），无批注读原文；逐行队列 + 行高亮跟随
    function speakSelectedLines() {
        if (textToSpeechService.speaking) {
            textToSpeechService.stop()
            return
        }
        if (selectedLines.length === 0) {
            statusLabel.text = qsTr("请先选中要朗读的行（Ctrl+点击可多选）")
            return
        }
        const items = []
        for (const ln of selectedLines) {
            const text = documentModel.lineText(ln)
            if (!text.trim()) continue
            const comment = documentModel.hasCommentAt(ln) ? documentModel.commentAt(ln) : ""
            items.push({ line: ln, text: comment && comment.trim() ? comment : text })
        }
        if (items.length === 0) {
            statusLabel.text = qsTr("没有可朗读的选中行")
            return
        }
        if (!textToSpeechService.speakLines(items)) {
            statusLabel.text = qsTr("系统无 TTS 引擎，无法朗读")
            return
        }
        statusLabel.text = qsTr("正在朗读 %1 行...").arg(items.length)
    }
    // 朗读单行（右键菜单「朗读此行」：targetLine 可能不在选中集）
    function speakLine(lineNumber) {
        const text = documentModel.lineText(lineNumber)
        if (!text.trim()) {
            statusLabel.text = qsTr("该行为空，无法朗读")
            return
        }
        const comment = documentModel.hasCommentAt(lineNumber) ? documentModel.commentAt(lineNumber) : ""
        const items = [{ line: lineNumber, text: comment && comment.trim() ? comment : text }]
        if (!textToSpeechService.speakLines(items)) {
            statusLabel.text = qsTr("系统无 TTS 引擎，无法朗读")
            return
        }
        statusLabel.text = qsTr("正在朗读第 %1 行...").arg(lineNumber + 1)
    }
    // 朗读行内编辑框选区（右键菜单「朗读选区」）
    function speakSelection() {
        const sel = page.editorSelectedText
        if (!sel || !sel.trim()) {
            statusLabel.text = qsTr("没有选中文本")
            return
        }
        if (!textToSpeechService.speakText(sel)) {
            statusLabel.text = qsTr("系统无 TTS 引擎，无法朗读")
            return
        }
        statusLabel.text = qsTr("正在朗读选区...")
    }

    // ---------- 章节（ChapterService） ----------
    function refreshChapterState() {
        page.chapterTitles = chapterService.chapterTitles()
        page.currentChapterIndex = page.currentLine >= 0
            ? chapterService.chapterAtLine(page.currentLine) : -1
    }
    function goChapter(delta) {
        const n = chapterService.chapterCount()
        if (n === 0) {
            statusLabel.text = qsTr("文档中没有识别到章节")
            return
        }
        let idx = page.currentChapterIndex
        if (idx < 0 || idx >= n) idx = 0
        idx = (idx + delta + n) % n
        page.currentChapterIndex = idx
        page.focusLine(chapterService.chapterStartLine(idx))
    }
    function goPrevChapter() { goChapter(-1) }
    function goNextChapter() { goChapter(1) }
    function rebuildChapters() {
        chapterService.rebuild()
        refreshChapterState()
        statusLabel.text = qsTr("已重新检测 %1 个章节").arg(chapterService.chapterCount())
    }

    // ---------- 批注（CommentService） ----------
    function refreshCommentState() {
        const map = commentService.allComments()
        const lines = []
        for (const k in map) {
            lines.push(Number(k))
        }
        lines.sort((a, b) => a - b)
        page.commentLines = lines
        page.commentCount = lines.length
    }
    function gotoComment(delta) {
        const n = page.commentLines.length
        if (n === 0) {
            statusLabel.text = qsTr("当前没有批注")
            return
        }
        let idx = page.commentLines.indexOf(page.currentLine)
        if (idx < 0) {
            idx = 0
            while (idx < n && page.commentLines[idx] < page.currentLine) ++idx
            if (idx >= n) idx = n - 1
        }
        idx = (idx + delta + n) % n
        page.focusLine(page.commentLines[idx])
    }
    function goPrevComment() { gotoComment(-1) }
    function goNextComment() { gotoComment(1) }
    function exportComments() { exportCommentsDialog.open() }
    function importComments() {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        importCommentsDialog.open()
    }

    // ---------- 查找/替换（FindService） ----------
    function doFindNext(query) {
        const q = String(query || "").trim()
        if (!q) {
            page.findMatches = []
            statusLabel.text = qsTr("请输入查找内容")
            return
        }
        // 换了新查询 → 从文档开头找第一个匹配；否则从当前行之后继续（避免原地不跳）
        const newQuery = q !== page.findQuery
        const from = newQuery ? 0 : (page.currentLine + 1)
        const line = findService.findNext(q, from, page.findCaseSensitive, page.findWholeWord, true, page.findFuzzy)
        page.findQuery = q
        page.findMatches = findService.find(q, page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        page.findResultCount = findService.count(q, page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        if (line >= 0) {
            page.focusLine(line)
            statusLabel.text = qsTr("找到 %1 处，已定位第 %2 行").arg(page.findResultCount).arg(line + 1)
        } else {
            statusLabel.text = qsTr("未找到匹配（共 0 处）")
        }
    }
    function doFindPrev(query) {
        const q = String(query || "").trim()
        if (!q) {
            page.findMatches = []
            statusLabel.text = qsTr("请输入查找内容")
            return
        }
        // 换了新查询 → 从文档末尾找最后一个匹配；否则从当前行之前继续
        const newQuery = q !== page.findQuery
        const from = newQuery ? (documentModel.lineCount() - 1) : (page.currentLine - 1)
        const line = findService.findPrevious(q, from, page.findCaseSensitive, page.findWholeWord, true, page.findFuzzy)
        page.findQuery = q
        page.findMatches = findService.find(q, page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        page.findResultCount = findService.count(q, page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        if (line >= 0) {
            page.focusLine(line)
            statusLabel.text = qsTr("找到 %1 处，已定位第 %2 行").arg(page.findResultCount).arg(line + 1)
        } else {
            statusLabel.text = qsTr("未找到匹配（共 0 处）")
        }
    }
    function doReplace(query, replacement) {
        const q = String(query || "")
        if (!q || page.currentLine < 0) {
            statusLabel.text = qsTr("请先输入查找内容并定位到一行")
            return
        }
        const ok = findService.replaceLine(page.currentLine, q, String(replacement || ""),
                                           page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        if (ok) {
            const del = lineView.itemAtIndex(page.currentLine)
            if (del) del.refreshEditor()
            refreshDocStatus()
            statusLabel.text = qsTr("已替换当前行")
        } else {
            statusLabel.text = qsTr("当前行无匹配")
        }
    }
    function doReplaceAll(query, replacement) {
        const q = String(query || "")
        if (!q) {
            statusLabel.text = qsTr("请输入查找内容")
            return
        }
        const n = findService.replaceAll(q, String(replacement || ""),
                                         page.findCaseSensitive, page.findWholeWord, page.findFuzzy)
        if (n > 0) {
            refreshDocStatus()
            afterUndoRedo()
        }
        statusLabel.text = n > 0 ? qsTr("已替换 %1 处").arg(n) : qsTr("无匹配可替换")
    }

    // ---------- 行操作与批注编辑（A1） ----------
    function insertLineAt(lineNumber) {
        if (lineNumber < 0 || lineNumber > documentModel.lineCount()) return
        const idx = documentModel.insertLine(lineNumber, "")
        page.currentLine = idx
        page.focusLine(idx)
        refreshDocStatus()
    }
    function deleteLineAt(lineNumber) {
        if (documentModel.lineCount() <= 1) {
            statusLabel.text = qsTr("至少保留一行")
            return
        }
        documentModel.removeLine(lineNumber)
        page.currentLine = Math.min(lineNumber, documentModel.lineCount() - 1)
        page.focusLine(page.currentLine)
        refreshDocStatus()
    }
    // 聚焦批注行内编辑（与原文一致）；无批注行进入 draft 会话
    function focusComment(lineNumber) {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        page.commentDraftLine = lineNumber
        page.currentLine = lineNumber
        page.selectedLines = []
        const del = lineView.itemAtIndex(lineNumber)
        if (del) del.startCommentEdit()
    }
    function deleteCommentAt(lineNumber) {
        if (page.limited) {
            page.limitedBlocked()
            return
        }
        commentService.removeComment(lineNumber)
        statusLabel.text = qsTr("已删除第 %1 行批注").arg(lineNumber + 1)
        statusIconSource = 0
        statusIconColor = FluTheme.fontSecondaryColor
    }
    // 显示设置：原文/批注字号（滑动条，持久化到 config，与设置页同步）
    function setOriginalFontSize(size) {
        page.originalFontSize = size
        configService.set("ui", "originalFontSize", size)
    }
    function setCommentFontSize(size) {
        page.commentFontSize = size
        configService.set("ui", "commentFontSize", size)
    }
    function openCommentSettings() {
        commentSettings.visible = true
    }
    // 剪切/复制/粘贴作用于当前可编辑行（仅当前行是 TextEdit）
    function clipboardOp(op) {
        const del = lineView.itemAtIndex(page.currentLine)
        if (!del) return
        if (op === "cut") del.cutLine()
        else if (op === "copy") del.copyLine()
        else if (op === "paste") del.pasteLine()
    }

    // ---------- 浮窗位置/大小（真实窗口，屏幕坐标持久化） ----------
    function saveFloatWindowPos() {
        const x = floatWindow.x
        const y = floatWindow.y
        if (isFinite(x) && isFinite(y) && x > -10000 && y > -10000) {
            configService.set("ui", "translatePanelX", Math.round(x))
            configService.set("ui", "translatePanelY", Math.round(y))
        }
        if (floatWindow.userResized) {
            configService.set("ui", "translatePanelWidth", Math.round(floatWindow.width))
            configService.set("ui", "translatePanelHeight", Math.round(floatWindow.height))
        }
    }
    function restoreFloatWindowPos() {
        // 真实窗口无任务栏（Qt.Tool），必须钳制到桌面内，防止开到屏幕外找不到。
        // screen 优先取浮窗自身（映射后有效），其次主窗口，最后兜底 1536x864。
        // ⚠️ 窗口未完全映射时 screen 的 virtualX/virtualWidth 可能是 undefined，
        // 必须 Number() || fallback，否则 NaN 污染钳制计算导致位置不生效（根因）。
        const scr = floatWindow.screen || (mainWindow ? mainWindow.screen : null)
        const sx = Number(scr && scr.virtualX) || 0
        const sy = Number(scr && scr.virtualY) || 0
        const sw = Number(scr && scr.virtualWidth) || 1536
        const sh = Number(scr && scr.virtualHeight) || 864
        let px = Number(configService.get("ui", "translatePanelX"))
        let py = Number(configService.get("ui", "translatePanelY"))
        if (!isFinite(px)) px = sx + sw - 340
        if (!isFinite(py)) py = sy + 80
        px = Math.max(sx, Math.min(px, sx + Math.max(0, sw - 300)))
        py = Math.max(sy, Math.min(py, sy + Math.max(0, sh - 240)))
        // setX/setY 显式触发窗口位置更新；isFinite 防御（坏值一律回退默认右上角）
        if (isFinite(px) && isFinite(py)) {
            floatWindow.setX(Math.round(px))
            floatWindow.setY(Math.round(py))
        }
        // 恢复手动缩放大小（用户真的缩放过才固定；否则保持跟随内容）
        if (configService.isUserSet("ui", "translatePanelWidth")
                && configService.isUserSet("ui", "translatePanelHeight")) {
            const mw = Number(configService.get("ui", "translatePanelWidth"))
            const mh = Number(configService.get("ui", "translatePanelHeight"))
            if (isFinite(mw) && isFinite(mh) && mw >= 200 && mh >= 130) {
                floatWindow.userResized = true
                floatWindow.width = mw
                floatWindow.manualHeight = mh
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        // 顶边距收紧：减少「编辑」标题与 Ribbon 工具栏之间的空白
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        anchors.topMargin: 6
        anchors.bottomMargin: 16
        spacing: 12

        // ---------- Ribbon 工具栏（PPT 式标签页 + 功能区，替代旧菜单栏/工具栏） ----------
        // 每个 service 提供 PPT 式（标签功能区）；翻译额外提供浮窗（翻译标签内切换）
        // NoStack 页面每次导航重建，当前标签持久化到 config，重建后恢复
        FluPivot {
            id: ribbonPivot
            property bool ribbonInitialized: false   // 初始化完成前不保存（避免把 0 覆盖掉用户值）
            Layout.fillWidth: true
            Layout.preferredHeight: 84   // header 40 + 功能区 44
            Component.onCompleted: {
                const idx = Number(configService.get("ui", "currentRibbonTab"))
                if (isFinite(idx) && idx >= 0 && idx < 6) {
                    ribbonPivot.currentIndex = idx
                }
                ribbonInitialized = true
            }
            onCurrentIndexChanged: {
                if (ribbonInitialized) {
                    configService.set("ui", "currentRibbonTab", ribbonPivot.currentIndex)
                }
            }
            font.pixelSize: tokens.fontBody

            // 文件标签（DocumentManager service 的 PPT 式功能区）
            FluPivotItem {
                title: qsTr("文件")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        RibbonButton { iconSource: FluentIcons.Add; text: qsTr("新建"); onClicked: newDocument() }
                        RibbonButton { iconSource: FluentIcons.OpenFile; text: qsTr("打开"); onClicked: openDocument() }
                        RibbonButton { iconSource: FluentIcons.History; text: qsTr("最近"); enabled: page.hasRecent; onClicked: recentMenu.popup() }
                        RibbonButton { iconSource: FluentIcons.Save; text: qsTr("保存"); onClicked: saveDocument() }
                        RibbonButton { iconSource: FluentIcons.SaveAs; text: qsTr("另存为"); onClicked: saveDocumentAs() }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        RibbonButton { iconSource: FluentIcons.Import; text: qsTr("加载示例"); onClicked: loadDemoDocument() }
                        RibbonButton { iconSource: FluentIcons.Delete; text: qsTr("清除译文"); onClicked: clearAllComments() }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // 开始标签（DocumentModel 编辑操作的 PPT 式功能区）
            FluPivotItem {
                title: qsTr("开始")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        RibbonButton { iconSource: FluentIcons.Undo; text: qsTr("撤销"); enabled: page.canUndo; onClicked: { documentModel.undo(); afterUndoRedo() } }
                        RibbonButton { iconSource: FluentIcons.Redo; text: qsTr("重做"); enabled: page.canRedo; onClicked: { documentModel.redo(); afterUndoRedo() } }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        FluText { text: qsTr("共 %1 行").arg(documentModel.lineCount()); font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // 翻译标签（TranslationService：PPT 式功能区 + 浮窗切换）
            FluPivotItem {
                title: qsTr("翻译")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        RibbonButton { filled: true; iconSource: FluentIcons.Play; text: qsTr("翻译当前行"); enabled: !page.limited; onClicked: translateCurrent() }
                        RibbonButton { iconSource: FluentIcons.Play; text: qsTr("翻译全部待译行"); enabled: !page.limited; onClicked: translateAllPending() }
                        RibbonButton { iconSource: FluentIcons.Play; text: qsTr("翻译选中行"); enabled: page.selectedLines.length > 0 && !page.limited; onClicked: translateSelected() }
                        // 术语提取（翻译功能区入口）：文档高频词 → 勾选 + 填译文 → 加入术语表
                        RibbonButton {
                            iconSource: FluentIcons.DictionaryAdd
                            text: qsTr("提取术语")
                            enabled: !page.limited
                            onClicked: page.extractTermsFromDoc()
                        }
                        // TTS 朗读（迭代3）：朗读选中行（有译文读译文，否则读原文）；朗读中变「停止」
                        RibbonButton {
                            iconSource: FluentIcons.Speakers
                            text: textToSpeechService.speaking ? qsTr("停止朗读") : qsTr("朗读选中行")
                            enabled: page.selectedLines.length > 0
                            onClicked: page.speakSelectedLines()
                        }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        // 翻译历史（迭代4b）：会话内翻译记录，点击条目跳转行；
                        // 属查询/辅助功能，与浮窗开关同区（review 2026-08-18：不混入翻译动作组）
                        RibbonButton {
                            iconSource: FluentIcons.History
                            text: qsTr("翻译历史")
                            onClicked: page.openHistoryDialog()
                        }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        // 浮窗切换（PPT 式 ↔ 浮窗；唯一模式入口）
                        // checked 反映「浮窗是否实际显示」；覆盖 clickListener 直接切换（不依赖
                        // checked 绑定的 toggled——绑定会干扰点击赋值，导致开关“点了没反应”）
                        FluToggleSwitch {
                            text: qsTr("浮窗")
                            checked: page.panelMode === "floating" && page.panelShown
                            clickListener: () => {
                                const wantOpen = !(page.panelMode === "floating" && page.panelShown)
                                if (wantOpen) {
                                    // 开浮窗：切模式 + 显示浮层
                                    page.panelMode = "floating"
                                    page.panelShown = true
                                    configService.set("ui", "translatePanelMode", "floating")
                                    configService.set("ui", "translatePanelVisible", true)
                                } else {
                                    // 关浮窗：回功能区（ppt）+ 隐藏浮窗 + 记忆屏幕位置
                                    page.panelMode = "ppt"
                                    page.panelShown = false
                                    configService.set("ui", "translatePanelMode", "ppt")
                                    configService.set("ui", "translatePanelVisible", false)
                                    page.saveFloatWindowPos()
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                        // 翻译进度（翻译中显示在右侧；出现时淡入）
                        FluProgressBar {
                            visible: page.translating
                            opacity: page.translating ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            Layout.preferredWidth: 160
                            from: 0
                            to: Math.max(page.progressTotal, 1)
                            value: page.progressDone
                        }
                        FluText {
                            visible: page.translating
                            opacity: page.translating ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            text: qsTr("%1/%2").arg(page.progressDone).arg(page.progressTotal)
                            font.pixelSize: tokens.fontCaption
                        }
                        FluButton {
                            visible: page.translating
                            opacity: page.translating ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                            text: qsTr("取消")
                            onClicked: translator.cancelTranslation()
                        }
                    }
                }
            }

            // 章节标签（ChapterService：章节导航 + 重新检测）
            FluPivotItem {
                title: qsTr("章节")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        FluText { text: qsTr("章节"); font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                        RibbonButton { iconSource: FluentIcons.ChevronUp; text: qsTr("上一章"); enabled: page.chapterTitles.length > 1; onClicked: page.goPrevChapter() }
                        RibbonButton { iconSource: FluentIcons.ChevronDown; text: qsTr("下一章"); enabled: page.chapterTitles.length > 1; onClicked: page.goNextChapter() }
                        FluText {
                            text: page.currentChapterIndex >= 0
                                ? qsTr("%1 · 共 %2 章").arg(page.chapterTitles[page.currentChapterIndex] || "").arg(page.chapterTitles.length)
                                : qsTr("共 %1 章").arg(page.chapterTitles.length)
                            Layout.maximumWidth: 260
                            elide: Text.ElideRight
                            font.pixelSize: tokens.fontCaption
                            color: FluTheme.fontSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                        RibbonButton { iconSource: FluentIcons.Refresh; text: qsTr("重新检测"); onClicked: page.rebuildChapters() }
                    }
                }
            }

            // 批注标签（CommentService：统计 + 导航 + 清空 + 导入导出）
            FluPivotItem {
                title: qsTr("批注")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        FluText { text: qsTr("批注 %1 条").arg(page.commentCount); font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                        RibbonButton { iconSource: FluentIcons.ChevronUp; text: qsTr("上一条"); enabled: page.commentCount > 1; onClicked: page.goPrevComment() }
                        RibbonButton { iconSource: FluentIcons.ChevronDown; text: qsTr("下一条"); enabled: page.commentCount > 1; onClicked: page.goNextComment() }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        RibbonButton { iconSource: FluentIcons.Delete; text: qsTr("清空"); onClicked: page.clearAllComments() }
                        RibbonButton { iconSource: FluentIcons.Export; text: qsTr("导出"); onClicked: page.exportComments() }
                        RibbonButton { iconSource: FluentIcons.Import; text: qsTr("导入"); onClicked: page.importComments() }
                        Item { Layout.fillWidth: true }
                    }
                }
            }

            // 查找标签（FindService：查找/替换 + 大小写/整词）
            // 注：FluComboBox 在 NoStack 下 Popup 不可用，故章节/查找均用按钮+文本框而非下拉框
            FluPivotItem {
                title: qsTr("查找")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        FluTextBox {
                            id: findInput
                            placeholderText: qsTr("查找")
                            Layout.fillWidth: true
                            Layout.preferredWidth: 180
                            Layout.maximumWidth: 240
                            onAccepted: page.doFindNext(findInput.text)
                        }
                        RibbonButton { iconSource: FluentIcons.ChevronUp; text: qsTr("上一个"); onClicked: page.doFindPrev(findInput.text) }
                        RibbonButton { iconSource: FluentIcons.ChevronDown; text: qsTr("下一个"); onClicked: page.doFindNext(findInput.text) }
                        FluText { text: qsTr("%1 处").arg(page.findResultCount); font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        FluTextBox {
                            id: replaceInput
                            placeholderText: qsTr("替换为")
                            Layout.fillWidth: true
                            Layout.preferredWidth: 140
                            Layout.maximumWidth: 200
                        }
                        FluButton { text: qsTr("替换"); onClicked: page.doReplace(findInput.text, replaceInput.text) }
                        FluButton { text: qsTr("全部替换"); onClicked: page.doReplaceAll(findInput.text, replaceInput.text) }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }

        // ---------- 大文件受限模式提示条（超 5 万行 / 200MB 自动进入，见 docs/services/large-file.md） ----------
        Rectangle {
            visible: page.limited
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            radius: tokens.radiusControl
            color: tokens.findHighlight
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                FluIcon {
                    iconSource: FluentIcons.Warning
                    iconSize: 15
                    color: tokens.warning
                }
                FluText {
                    text: qsTr("大文件受限模式：已禁用富文本/图片渲染、批注编辑与翻译（编辑/查找/章节正常）")
                    font.pixelSize: tokens.fontCaption
                    color: FluTheme.fontPrimaryColor
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
        }

        // ---------- 编辑器卡片（占满剩余空间；PPT 式功能区在顶部 Ribbon，浮窗为可选） ----------
        FluFrame {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: tokens.radiusCard
            // 注：不加 layer/MultiEffect 阴影——layer 离屏渲染在非整数 DPI 缩放下
            // 文字模糊（2026-08-19 用户实测），且含滚动 ListView 开销大（性能铁律）

            ListView {
                id: lineView
                anchors.fill: parent
                anchors.margins: 4
                model: documentModel
                clip: true
                cacheBuffer: 600
                focus: true
                keyNavigationWraps: false
                ScrollBar.vertical: FluScrollBar { }

                // 上下键在行间移动
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_Up && currentLine > 0) {
                        focusLine(currentLine - 1)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Down && currentLine < documentModel.lineCount() - 1) {
                        focusLine(currentLine + 1)
                        event.accepted = true
                    }
                }

                delegate: Rectangle {
                    id: row
                    required property var model
                    required property int index
                    width: lineView.width
                    // 图片行：行号 36 + 图片区 180 + 边距；图文混排（rich+imageIds）：
                    // 文本行 36 + 图片区（img height=60 + 间距）；其余：原文 36 + 批注区
                    height: row.model.display === "image"
                            ? 224
                            : row.model.display === "rich"
                              && row.model.imageIds && row.model.imageIds.length > 0
                              ? 36 + 70
                              : 36 + (row.model.hasComment || page.commentDraftLine === index
                                      ? Math.max(20, commentMeasurer.contentHeight) + 6 : 0)
                    radius: tokens.radiusControl   // 经页面属性中转（delegate 内单例访问缺陷，见上）
                    // 当前行 → 主题色浅背景；多选行 → 主题色更浅；批注行 → 主题色最浅；hover → itemHoverColor；否则透明
                    color: {
                        if (index === page.currentLine) {
                            return Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g,
                                           FluTheme.primaryColor.b, FluTheme.dark ? 0.22 : 0.08)
                        }
                        if (page.isLineSelected(index)) {
                            return Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g,
                                           FluTheme.primaryColor.b, FluTheme.dark ? 0.16 : 0.06)
                        }
                        if (row.model.hasComment) {
                            return Qt.rgba(FluTheme.primaryColor.r, FluTheme.primaryColor.g,
                                           FluTheme.primaryColor.b, FluTheme.dark ? 0.12 : 0.05)
                        }
                        if (page.isFindMatch(index)) {
                            // 查找命中：琥珀色浅底，与主题色（当前行/选中/批注）区分
                            return tokens.findHighlight
                        }
                        return row.hovered ? FluTheme.itemHoverColor : "transparent"
                    }
                    // 交互动画（ui-improvement-plan P0）：hover/选中/当前行颜色平滑过渡
                    Behavior on color { ColorAnimation { duration: 120; easing.type: Easing.OutCubic } }

                    property bool hovered: false
                    HoverHandler {
                        onHoveredChanged: row.hovered = hovered
                    }

                    // 进入编辑：聚焦行内编辑器（供 page.focusLine 通过 itemAtIndex 调用）
                    function startEdit() {
                        lineEditor.forceActiveFocus()
                        lineEditor.cursorPosition = lineEditor.length
                    }

                    // 进入批注编辑：从模型刷新文本并聚焦（防 ListView 复用残留 + draft 空文本）
                    // 注意：Qt.callLater 不可靠（铁律），用 Timer 延迟聚焦
                    function startCommentEdit() {
                        commentEditor.text = row.model.commentText
                        commentFocusTimer.start()
                    }

                    Timer {
                        id: commentFocusTimer
                        interval: 0
                        onTriggered: {
                            commentEditor.forceActiveFocus()
                            commentEditor.cursorPosition = commentEditor.length
                        }
                    }

                    // 撤销/重做后刷新编辑行文本（TextEdit 用户输入会解除绑定）
                    function refreshEditor() {
                        lineEditor.text = row.model.text
                    }

                    // 剪贴板操作（当前行可编辑时）
                    function cutLine() { if (lineEditor.visible) lineEditor.cut() }
                    function copyLine() { if (lineEditor.visible) lineEditor.copy() }
                    function pasteLine() { if (lineEditor.visible) lineEditor.paste() }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        onClicked: (mouse) => {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                // Ctrl+点击：切换多选状态（不改当前行）
                                const idx = page.selectedLines.indexOf(index)
                                if (idx >= 0) {
                                    page.selectedLines.splice(idx, 1)
                                } else {
                                    page.selectedLines.push(index)
                                }
                                page.selectedLines = page.selectedLines.slice()   // 触发重算
                                page.currentLine = index
                                page.focusLine(index)
                            } else {
                                // 普通点击：设为当前行并清空多选
                                page.currentLine = index
                                page.selectedLines = []
                                page.focusLine(index)
                            }
                        }
                    }

                    // 当前行左侧强调条（Fluent 选中指示，宽度 0→3 动画展开/收起）
                    Rectangle {
                        width: index === page.currentLine ? 3 : 0
                        visible: width > 0
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        radius: 1.5   // 指示条专用圆角（细条，非标准控件圆角，不走 token）
                        color: tokens.accentBar
                        Behavior on width { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                    }

                    // 原文行（顶部 36px）
                    RowLayout {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 36
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10

                        FluText {
                            text: String(index + 1)   // 依赖 ListView 的 index 更新，插入行后行号正确刷新
                            font.pixelSize: tokens.fontCaption
                            color: FluTheme.fontTertiaryColor
                            Layout.preferredWidth: 44
                            horizontalAlignment: Text.AlignRight
                        }

                        // 可编辑文本（虚拟化下每行一个 TextEdit，仅可见行实例化）
                        TextEdit {
                            id: lineEditor
                            text: row.model.text
                            font.pixelSize: page.originalFontSize
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            selectByMouse: true
                            clip: true
                            visible: page.currentLine === index
                            readOnly: page.currentLine !== index
                            onSelectedTextChanged: page.editorSelectedText = selectedText

                            // 编辑实时同步到模型（只在当前行时）
                            onTextChanged: {
                                if (page.currentLine === index) {
                                    documentModel.updateLineText(index, text)
                                    // 编辑任意行即清除显示层（幂等）：富文本/图片行编辑后
                                    // 降级纯文本，保证退出后显示编辑层、.trx 保存往返一致
                                    documentModel.setLineRich(index, "")
                                    documentModel.setLineImages(index, [])
                                    documentModel.setLineDisplay(index, "plain")
                                }
                            }

                            Keys.onPressed: (event) => {
                                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                    page.splitCurrentLine(cursorPosition)
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Backspace && cursorPosition === 0) {
                                    page.mergeWithPrevious()
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Up) {
                                    if (page.currentLine > 0) {
                                        page.focusLine(page.currentLine - 1)
                                        event.accepted = true
                                    }
                                } else if (event.key === Qt.Key_Down) {
                                    if (page.currentLine < documentModel.lineCount() - 1) {
                                        page.focusLine(page.currentLine + 1)
                                        event.accepted = true
                                    }
                                }
                            }

                            // 当前行编辑下划线（Fluent 输入指示）
                            Rectangle {
                                visible: page.currentLine === index
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 2
                                radius: 1   // 指示条专用圆角（细条，非标准控件圆角，不走 token）
                                color: tokens.accentBar
                            }
                        }

                        // 非编辑状态只读显示：plain → 纯文本；rich → 富文本（HTML 仅显示，编辑即降级）
                        Text {
                            text: row.model.text
                            font.pixelSize: page.originalFontSize
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            visible: page.currentLine !== index && row.model.display !== "rich"
                        }
                        // 富文本行：Text 原生响应 <a href> 点击（onLinkActivated），
                        // [图片] 占位 → 真实 <img>（data URI 内嵌）；非链接区域点击透传给行选中 MouseArea
                        Text {
                            text: page.richTextFor(row)
                            textFormat: Text.RichText
                            font.pixelSize: page.originalFontSize
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.Wrap
                            visible: page.currentLine !== index && row.model.display === "rich"
                            onLinkActivated: Qt.openUrlExternally(link)
                        }

                        // 批注图标（点击进入批注行内编辑）
                        MouseArea {
                            visible: row.model.hasComment === true || page.commentDraftLine === index
                            Layout.preferredWidth: 20
                            Layout.preferredHeight: 20
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.focusComment(index)
                            FluIcon {
                                anchors.centerIn: parent
                                iconSource: FluentIcons.Message
                                iconSize: 15
                                color: FluTheme.primaryColor   // 批注色：dark 下 accentColor 是深蓝看不清，primaryColor 自动变亮
                            }
                        }
                    }

                    // ---------- 图片行显示（display=image：docx 纯图段；点击/编辑即降级） ----------
                    Image {
                        visible: row.model.display === "image" && page.currentLine !== index
                        anchors.left: parent.left
                        anchors.leftMargin: 12 + 44 + 10   // 对齐原文文本（行号宽 + 间距）
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.top: parent.top
                        anchors.topMargin: 40
                        height: 180
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        source: row.model.imageIds && row.model.imageIds.length > 0
                                ? page.imageSource(row.model.imageIds[0]) : ""
                        clip: true
                    }

                    // ---------- 批注（译文）行内直编：与原文一样可编辑，超宽自动换行 ----------
                    Item {
                        id: commentBox
                        visible: row.model.hasComment === true || page.commentDraftLine === index
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 38
                        anchors.leftMargin: 12 + 44 + 10   // 对齐原文文本（行号宽 + 间距）
                        anchors.rightMargin: 12
                        height: Math.max(20, commentMeasurer.contentHeight)
                        property bool commentEditing: !page.limited
                            && page.currentLine === index
                            && (row.model.hasComment || page.commentDraftLine === index)

                        // 换行高度测量（透明度 0，始终参与布局；编辑/只读共用）
                        Text {
                            id: commentMeasurer
                            anchors.top: parent.top
                            anchors.left: parent.left
                            width: commentBox.width
                            text: row.model.commentText
                            font.pixelSize: page.commentFontSize
                            wrapMode: Text.Wrap
                            opacity: 0
                        }
                        // 非当前行：只读显示（自动换行）
                        Text {
                            id: commentReadonly
                            visible: !commentBox.commentEditing
                            anchors.top: parent.top
                            anchors.left: parent.left
                            width: commentBox.width
                            height: commentMeasurer.contentHeight
                            text: row.model.commentText
                            font.pixelSize: page.commentFontSize
                            color: FluTheme.primaryColor   // 批注色：dark 下 accentColor 是深蓝看不清，primaryColor 自动变亮
                            wrapMode: Text.Wrap
                        }
                        // 当前行：可编辑（与原文一致；清空=删除，draft 会话保持编辑框不塌缩）
                        TextEdit {
                            id: commentEditor
                            visible: commentBox.commentEditing
                            anchors.top: parent.top
                            anchors.left: parent.left
                            width: commentBox.width
                            height: commentMeasurer.contentHeight
                            text: row.model.commentText
                            font.pixelSize: page.commentFontSize
                            color: FluTheme.primaryColor   // 批注色：dark 下 accentColor 是深蓝看不清，primaryColor 自动变亮
                            wrapMode: Text.Wrap
                            selectByMouse: true
                            // 受限模式兜底只读（commentEditing 已挡编辑态，双保险）
                            readOnly: page.limited
                            onTextChanged: {
                                if (commentBox.commentEditing) {
                                    commentService.setComment(index, text)
                                }
                            }
                        }
                        // 点击批注区（非编辑态）→ 进入行内编辑
                        MouseArea {
                            visible: row.model.hasComment === true && !commentBox.commentEditing
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.focusComment(index)
                        }
                    }

                    // 右键（任意位置，含当前行编辑框上方）：弹出行菜单；左键穿透
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onClicked: (mouse) => {
                            page.currentLine = index
                            page.selectedLines = []
                            const pos = row.mapToItem(page, mouse.x, mouse.y)
                            lineMenu.openForLine(index, pos.x, pos.y)
                        }
                    }
                }
            }
        }

        // ---------- Fluent 状态栏 ----------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            radius: tokens.radiusCard
            color: tokens.bgCardAlt
            border.color: FluTheme.dividerColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                FluIcon {
                    visible: statusIconSource !== 0
                    iconSource: statusIconSource
                    iconSize: 15
                    color: statusIconColor
                }
                FluText {
                    id: statusLabel
                    text: qsTr("就绪")
                    font.pixelSize: tokens.fontCaption
                    color: FluTheme.fontSecondaryColor
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                // 修改标记 + 文档名
                Rectangle {
                    id: dirtyDot
                    visible: false
                    width: 8
                    height: 8
                    radius: tokens.radiusControl
                    color: FluTheme.primaryColor
                    Layout.alignment: Qt.AlignVCenter
                }
                FluText {
                    id: documentNameLabel
                    text: qsTr("未命名")
                    font.pixelSize: tokens.fontCaption
                    font.bold: true
                    color: FluTheme.fontPrimaryColor
                }
                FluText {
                    id: docStatsLabel
                    text: ""
                    font.pixelSize: tokens.fontCaption
                    color: FluTheme.fontTertiaryColor
                }
                FluText {
                    text: currentLine >= 0 ? qsTr("当前行 %1").arg(currentLine + 1) : ""
                    font.pixelSize: tokens.fontCaption
                    color: FluTheme.fontTertiaryColor
                }
            }
        }
    }

    // ---------- 文件对话框 ----------
    FileDialog {
        id: openDialog
        title: qsTr("打开文档")
        nameFilters: [qsTr("翻译文档 (*.trx)"), qsTr("文本文件 (*.txt)"), qsTr("Office 文档 (*.docx)"), qsTr("PDF 文档 (*.pdf)"), qsTr("所有文件 (*)")]
        onAccepted: openRecent(urlToPath(selectedFile))
    }
    FileDialog {
        id: saveAsDialog
        title: qsTr("另存为")
        nameFilters: [qsTr("翻译文档 (*.trx)"), qsTr("文本文件 (*.txt)"), qsTr("Markdown 文档 (*.md)"), qsTr("PDF 文档 (*.pdf)"), qsTr("所有文件 (*)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            documentManager.saveFileAs(urlToPath(selectedFile))
            refreshDocStatus()
        }
    }
    // 批注导出 / 导入（CommentService 持久化 JSON）
    FileDialog {
        id: exportCommentsDialog
        title: qsTr("导出批注")
        nameFilters: [qsTr("批注文件 (*.json)"), qsTr("所有文件 (*)")]
        fileMode: FileDialog.SaveFile
        onAccepted: {
            if (commentService.exportToFile(urlToPath(selectedFile))) {
                statusLabel.text = qsTr("批注已导出")
            } else {
                statusLabel.text = qsTr("批注导出失败")
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            }
        }
    }
    FileDialog {
        id: importCommentsDialog
        title: qsTr("导入批注")
        nameFilters: [qsTr("批注文件 (*.json)"), qsTr("所有文件 (*)")]
        onAccepted: {
            if (commentService.importFromFile(urlToPath(selectedFile))) {
                refreshCommentState()
                statusLabel.text = qsTr("批注已导入")
            } else {
                statusLabel.text = qsTr("批注导入失败")
                statusIconSource = FluentIcons.Warning
                statusIconColor = tokens.error
            }
        }
    }

    // ---------- 新建确认 ----------
    FluContentDialog {
        id: confirmNewDialog
        title: qsTr("新建文档")
        message: qsTr("当前文档有未保存的修改，确定丢弃并新建吗？")
        negativeText: qsTr("取消")
        positiveText: qsTr("新建")
        onPositiveClicked: doNewDocument()
    }

    // ---------- 崩溃恢复（自动保存，迭代4） ----------
    // 只弹一次（takeAutosavePrompt 由应用级 DocumentManager 记住），
    // message 在 onCompleted 动态拼接：快照文件名/时间 + 当前 dirty 警告
    FluContentDialog {
        id: restoreAutosaveDialog
        title: qsTr("恢复未保存的更改")
        message: ""
        negativeText: qsTr("丢弃")
        positiveText: qsTr("恢复")
        onNegativeClicked: documentManager.discardAutosave()
        onPositiveClicked: {
            if (documentManager.restoreAutosave()) {
                statusLabel.text = qsTr("已恢复上次未保存的更改")
                statusIconSource = FluentIcons.Message
                statusIconColor = tokens.success
            }
            refreshDocStatus()
            chapterService.rebuild()
        }
    }

    // ---------- 翻译历史（迭代4b） ----------
    // ListModel 必须在 dialog 外声明（contentDelegate 重建时模型会被释放）
    ListModel {
        id: historyModel
    }

    // 历史新增/清空 → 弹窗打开时刷新（entryAdded 触发时若弹窗可见也同步）
    Connections {
        target: translationHistoryService
        function onEntryAdded() {
            if (historyDialog.visible) {
                page.openHistoryDialog()
            }
        }
    }

    FluContentDialog {
        id: historyDialog
        title: qsTr("翻译历史（本会话）")
        message: ""
        buttonFlags: FluContentDialogType.PositiveButton
        positiveText: qsTr("关闭")
        contentDelegate: Component {
            Column {
                width: 380
                spacing: 8
                RowLayout {
                    width: parent.width
                    spacing: 8
                    FluText {
                        text: qsTr("共 %1 条").arg(historyModel.count)
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Item { Layout.fillWidth: true }
                    FluButton {
                        text: qsTr("清空")
                        enabled: historyModel.count > 0
                        onClicked: translationHistoryService.clear()
                    }
                }
                ListView {
                    width: parent.width
                    height: 320
                    clip: true
                    model: historyModel
                    delegate: ItemDelegate {
                        width: parent.width
                        implicitHeight: 50
                        onClicked: {
                            page.focusLine(model.line)
                            historyDialog.close()
                        }
                        Column {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 4
                            spacing: 2
                            Row {
                                width: parent.width
                                spacing: 6
                                FluText { text: qsTr("第 %1 行").arg(model.line + 1); font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                                FluText { text: model.time; font.pixelSize: tokens.fontCaption; color: FluTheme.fontSecondaryColor }
                                FluText {
                                    text: model.success ? qsTr("成功") : qsTr("失败")
                                    font.pixelSize: tokens.fontCaption
                                    color: model.success ? tokens.success : tokens.error
                                }
                            }
                            FluText {
                                width: parent.width
                                elide: Text.ElideRight
                                text: qsTr("原文：%1").arg(model.source)
                                font.pixelSize: tokens.fontCaption
                            }
                            FluText {
                                width: parent.width
                                elide: Text.ElideRight
                                text: qsTr("译文：%1").arg(model.translated)
                                font.pixelSize: tokens.fontCaption
                                color: FluTheme.fontSecondaryColor
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- 术语自动提取弹窗（翻译功能区入口；与设置页共用 TranslationService 术语表） ----------
    // ListModel 必须在 dialog 外声明（contentDelegate 重建时模型会被释放）
    ListModel {
        id: termCandidateModel
    }

    FluContentDialog {
        id: termExtractDialog
        title: qsTr("从文档提取术语")
        negativeText: qsTr("取消")
        positiveText: qsTr("添加选中")
        contentDelegate: Component {
            ColumnLayout {
                // FluContentDialog implicitWidth 400，内容宽度须小于对话框宽度避免裁剪
                width: 380
                spacing: 8
                FluText {
                    text: qsTr("勾选要加入术语表的词（支持英文/技术标识符/中文，出现 3 次以上）；可直接填写标准译文，留空则为占位（设置页可补填）：")
                    color: FluTheme.fontSecondaryColor
                    wrapMode: Text.Wrap
                }
                ListView {
                    id: termExtractList
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, 260)
                    model: termCandidateModel
                    clip: true
                    spacing: 4
                    delegate: RowLayout {
                        width: termExtractList.width
                        spacing: 8
                        FluCheckBox {
                            checked: model.checked
                            onToggled: termCandidateModel.setProperty(index, "checked", checked)
                        }
                        FluText {
                            text: qsTr("%1（出现 %2 次）").arg(model.word).arg(model.count)
                            Layout.preferredWidth: 170
                            Layout.alignment: Qt.AlignVCenter
                            elide: Text.ElideRight
                        }
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
                        onToggled: termExtractSelectAll(checked)
                    }
                    // AI 建议译文：仅配置了网络大模型 API 时可用（按文档上下文猜测标准译文）
                    FluButton {
                        text: qsTr("AI 建议译文")
                        enabled: translationService.termSuggestionAvailable()
                        onClicked: page.suggestTermTranslations()
                    }
                    Item { Layout.fillWidth: true }
                    FluText {
                        text: qsTr("译文为空 = 占位（不注入提示词）")
                        font.pixelSize: tokens.fontCaption
                        color: FluTheme.fontSecondaryColor
                        wrapMode: Text.Wrap
                    }
                }
            }
        }
        onPositiveClicked: addExtractedTermsFromDoc()
    }

    // ---------- 行右键菜单（页面内浮层：NoStack 下 Popup 不可用） ----------
    Item {
        id: lineMenu
        visible: false
        z: 8000
        anchors.fill: parent
        property int targetLine: -1
        function openForLine(lineNumber, x, y) {
            targetLine = lineNumber
            const cw = menuCard.width
            const ch = menuCard.height
            // 屏幕边缘自适应：按窗口可见区域钳制；靠右/下自动翻转到光标另一侧，保证完整显示
            const win = mainWindow
            const content = win && win.contentItem ? win.contentItem : null
            if (content) {
                const off = page.mapToItem(content, 0, 0)
                let wx = off.x + x
                let wy = off.y + y
                if (wx + cw + 10 > content.width) wx = wx - cw - 10
                if (wy + ch + 10 > content.height) wy = wy - ch - 10
                wx = Math.max(10, Math.min(wx, content.width - cw - 10))
                wy = Math.max(10, Math.min(wy, content.height - ch - 10))
                const back = page.mapFromItem(content, wx, wy)
                menuCard.x = back.x
                menuCard.y = back.y
            } else {
                menuCard.x = Math.max(4, Math.min(x, page.width - cw - 4))
                menuCard.y = Math.max(4, Math.min(y, page.height - ch - 4))
            }
            visible = true
        }
        function closeMenu() { visible = false }
        // 点击菜单外关闭
        MouseArea {
            anchors.fill: parent
            onClicked: lineMenu.closeMenu()
        }
        Rectangle {
            id: menuCard
            width: 200
            height: menuCol.implicitHeight + 8
            radius: tokens.radiusCard
            color: tokens.bgCard
            border.color: FluTheme.dividerColor
            border.width: 1
            // 弹出动画（ui-improvement-plan P0）：缩放 + 淡入
            opacity: lineMenu.visible ? 1.0 : 0.0
            scale: lineMenu.visible ? 1.0 : 0.95
            transformOrigin: Item.TopLeft
            Behavior on opacity { NumberAnimation { duration: 120 } }
            Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            ColumnLayout {
                id: menuCol
                anchors.fill: parent
                anchors.margins: 4
                spacing: 0
                Repeater {
                    model: [
                        { key: "addComment", text: qsTr("添加/编辑批注") },
                        { key: "delComment", text: qsTr("删除批注") },
                        { key: "sep" },
                        { key: "speakLine", text: qsTr("朗读此行") },
                        { key: "speakSelection", text: qsTr("朗读选区") },
                        { key: "sep" },
                        { key: "cut", text: qsTr("剪切") },
                        { key: "copy", text: qsTr("复制") },
                        { key: "paste", text: qsTr("粘贴") },
                        { key: "sep" },
                        { key: "insUp", text: qsTr("插入行（上方）") },
                        { key: "insDown", text: qsTr("插入行（下方）") },
                        { key: "delLine", text: qsTr("删除行") },
                        { key: "sep" },
                        { key: "settings", text: qsTr("显示设置…") }
                    ]
                    // 单个委托组件：分隔线 / 菜单项（Repeater 自动注入 modelData）
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: modelData.key === "sep" ? 9 : 30
                        color: "transparent"
                        // 批注项（添加/删除）在受限模式禁用；删除项还需目标行有批注
                        // 编辑类（剪切/复制/粘贴/插行/删行/显示设置）不受限
                        property bool menuRowEnabled: modelData.key === "addComment"
                                || modelData.key === "delComment"
                            ? !documentModel.limitedMode
                                && (modelData.key !== "delComment"
                                    || (lineMenu.targetLine >= 0 && documentModel.hasCommentAt(lineMenu.targetLine)))
                            : modelData.key === "speakSelection"
                                ? page.editorSelectedText.trim().length > 0
                                : true
                        // 分隔线
                        Rectangle {
                            visible: modelData.key === "sep"
                            height: 1
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            color: FluTheme.dividerColor
                        }
                        // 菜单项
                        Rectangle {
                            visible: modelData.key !== "sep"
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: tokens.radiusControl   // 经页面属性中转（delegate 内单例访问缺陷）
                            color: menuRowHover.hovered ? FluTheme.itemHoverColor : "transparent"
                            opacity: parent.menuRowEnabled ? 1 : 0.4
                            HoverHandler { id: menuRowHover }
                            FluText {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.text || ""   // 分隔线行无 text
                                font.pixelSize: tokens.fontMenu
                                color: parent.parent.menuRowEnabled
                                    ? FluTheme.fontPrimaryColor : FluTheme.fontTertiaryColor
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    const ln = lineMenu.targetLine
                                    lineMenu.closeMenu()
                                    switch (modelData.key) {
                                    case "addComment": page.focusComment(ln); break
                                    case "delComment": page.deleteCommentAt(ln); break
                                    case "speakLine": page.speakLine(ln); break
                                    case "speakSelection": page.speakSelection(); break
                                    case "cut": page.clipboardOp("cut"); break
                                    case "copy": page.clipboardOp("copy"); break
                                    case "paste": page.clipboardOp("paste"); break
                                    case "insUp": page.insertLineAt(ln); break
                                    case "insDown": page.insertLineAt(ln + 1); break
                                    case "delLine": page.deleteLineAt(ln); break
                                    case "settings": page.openCommentSettings(); break
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------- 显示设置（字号滑动条；页面内浮层，与设置页共用 config） ----------
    Item {
        id: commentSettings
        visible: false
        z: 9000
        anchors.fill: parent
        function closeSettings() { visible = false }
        // 遮罩（点击关闭）
        Rectangle {
            anchors.fill: parent
            color: tokens.overlayMask
            MouseArea { anchors.fill: parent; onClicked: commentSettings.closeSettings() }
        }
        Rectangle {
            width: 420
            height: settingsCol.implicitHeight + 32
            anchors.centerIn: parent
            radius: tokens.radiusCard
            color: tokens.bgCard
            border.color: FluTheme.dividerColor
            border.width: 1
            ColumnLayout {
                id: settingsCol
                anchors.fill: parent
                anchors.margins: 16
                spacing: 16
                FluText {
                    text: qsTr("显示设置")
                    font.pixelSize: tokens.fontTitle
                    font.bold: true
                    color: FluTheme.fontPrimaryColor
                }
                // 原文字号（滑动条）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    FluText {
                        text: qsTr("原文字号 %1").arg(page.originalFontSize)
                        font.pixelSize: tokens.fontMenu
                        color: FluTheme.fontPrimaryColor
                        Layout.preferredWidth: 100
                    }
                    FluSlider {
                        Layout.fillWidth: true
                        from: 10
                        to: 24
                        stepSize: 1
                        value: page.originalFontSize
                        onMoved: page.setOriginalFontSize(Math.round(value))
                    }
                }
                // 批注字号（滑动条，独立于原文）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    FluText {
                        text: qsTr("批注字号 %1").arg(page.commentFontSize)
                        font.pixelSize: tokens.fontMenu
                        color: FluTheme.fontPrimaryColor
                        Layout.preferredWidth: 100
                    }
                    FluSlider {
                        Layout.fillWidth: true
                        from: 8
                        to: 24
                        stepSize: 1
                        value: page.commentFontSize
                        onMoved: page.setCommentFontSize(Math.round(value))
                    }
                }
                FluText {
                    text: qsTr("字号即时生效并持久化；批注字号独立于原文，仅影响批注区域。设置页「显示」可同步调整。")
                    font.pixelSize: tokens.fontCaption
                    color: FluTheme.fontSecondaryColor
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FluButton { text: qsTr("关闭"); onClicked: commentSettings.closeSettings() }
                }
            }
        }
    }

    // 启动延迟显示浮窗（见 onCompleted：主窗口稳定后再 show，避免 Qt.Tool 竞态）
    Timer {
        id: floatShowTimer
        interval: 350
        onTriggered: page.panelShown = true
    }

    // ---------- 快捷键总览（? 呼出；页面内覆盖层，NoStack 安全） ----------
    Item {
        id: shortcutHelp
        visible: false
        z: 9000
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: tokens.overlayMask
            MouseArea { anchors.fill: parent; onClicked: shortcutHelp.visible = false }
        }
        Rectangle {
            width: 460
            height: helpCol.implicitHeight + 32
            anchors.centerIn: parent
            radius: tokens.radiusCard
            color: tokens.bgCard
            border.color: FluTheme.dividerColor
            border.width: 1
            ColumnLayout {
                id: helpCol
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                FluText {
                    text: qsTr("快捷键")
                    font.pixelSize: tokens.fontTitle
                    font.bold: true
                    color: FluTheme.fontPrimaryColor
                }
                Repeater {
                    model: [
                        { key: "Ctrl+Z", desc: qsTr("撤销") },
                        { key: "Ctrl+Y", desc: qsTr("重做") },
                        { key: "Ctrl+Alt+T", desc: qsTr("翻译当前行") },
                        { key: "Ctrl+Alt+Shift+T", desc: qsTr("翻译全部待译行") },
                        { key: "Ctrl+点击", desc: qsTr("多选行（再点取消）") },
                        { key: "Enter", desc: qsTr("拆分行") },
                        { key: "Backspace（行首）", desc: qsTr("合并上一行") },
                        { key: "?", desc: qsTr("本快捷键总览") }
                    ]
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        FluText {
                            text: modelData.key
                            font.pixelSize: tokens.fontMenu
                            font.bold: true
                            color: FluTheme.primaryColor
                            Layout.preferredWidth: 170
                        }
                        FluText {
                            text: modelData.desc
                            font.pixelSize: tokens.fontMenu
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FluButton { text: qsTr("关闭"); onClicked: shortcutHelp.visible = false }
                }
            }
        }
    }

    // ---------- 质量自检复核面板（翻译后汇总 qualityWarning，点击跳转复核） ----------
    // 警告收集：onQualityWarning 追加到 qualityWarnings；onBatchFinished 有告警时弹出。
    ListModel {
        id: qualityWarnings
    }
    Item {
        id: qualityReport
        visible: false
        z: 9000
        anchors.fill: parent
        Rectangle {
            anchors.fill: parent
            color: tokens.overlayMask
            MouseArea { anchors.fill: parent; onClicked: qualityReport.visible = false }
        }
        Rectangle {
            width: 520
            height: Math.min(qualityList.contentHeight + 96, parent.height - 160)
            anchors.centerIn: parent
            radius: tokens.radiusCard
            color: tokens.bgCard
            border.color: FluTheme.dividerColor
            border.width: 1
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 8
                RowLayout {
                    Layout.fillWidth: true
                    FluText {
                        text: qsTr("质量自检：%1 处需人工复核").arg(qualityWarnings.count)
                        font.pixelSize: tokens.fontTitle
                        font.bold: true
                        color: FluTheme.fontPrimaryColor
                    }
                    Item { Layout.fillWidth: true }
                    FluButton {
                        text: qsTr("关闭")
                        onClicked: qualityReport.visible = false
                    }
                }
                ListView {
                    id: qualityList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: qualityWarnings
                    clip: true
                    spacing: 2
                    delegate: Rectangle {
                        width: qualityList.width
                        height: 44
                        radius: tokens.radiusControl
                        color: rowHovered ? FluTheme.itemHoverColor : "transparent"
                        property bool rowHovered: false
                        HoverHandler { onHoveredChanged: rowHovered = hovered }
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 10
                            FluText {
                                text: model.line >= 0 ? qsTr("第 %1 行").arg(model.line + 1) : qsTr("全文")
                                font.pixelSize: tokens.fontCaption
                                font.bold: true
                                color: tokens.warning
                                Layout.preferredWidth: 76
                            }
                            FluText {
                                text: model.issue
                                font.pixelSize: tokens.fontMenu
                                color: FluTheme.fontPrimaryColor
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            FluButton {
                                text: qsTr("跳转")
                                disabled: model.line < 0
                                onClicked: {
                                    page.focusLine(model.line)
                                    qualityReport.visible = false
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    FluButton {
                        text: qsTr("全部已复核，清除列表")
                        onClicked: qualityWarnings.clear()
                    }
                }
            }
        }
    }

    // ---------- 浮动翻译窗口（真独立 Window：可拖到屏幕任意位置，不再受页面范围限制） ----------
    // 页面内浮层会被导航视图/窗口边界裁剪，拖出页面即失效（用户确认）；改用真 Window。
    // Qt.Tool：无任务栏、经 transientParent 跟随主窗口；FramelessWindowHint：去掉系统原生
    // 标题栏（否则与 Fluent 风格违和）；自绘卡片（圆角+边框）+ 标题栏（startSystemMove 原生
    // 拖动）。关闭/切换经 Ribbon「翻译」标签的浮窗开关。位置持久化为屏幕坐标，恢复时钳制到桌面内防止无任务栏找不到。
    Window {
        id: floatWindow
        // 主窗口最小化时隐藏（Qt.Tool 独立窗口不自动跟随最小化，需手动同步）
        visible: page.panelMode === "floating" && page.panelShown && !floatWindow.minimized
        transientParent: mainWindow
        flags: Qt.Tool | Qt.FramelessWindowHint
        title: qsTr("翻译工具")
        width: 300
        // 最小尺寸放宽（内容自适应缩放 + 窄窗口隐藏次要文本后仍可用）
        minimumWidth: 200
        minimumHeight: 130
        // 主窗口最小化时置 true 隐藏浮窗；恢复时置 false 重新显示
        property bool minimized: false
        // 应用退出（主窗口关闭）时置 true，放行浮窗关闭，避免拦截导致进程残留
        property bool appQuitting: false
        // 用户手动缩放后高度不再跟随内容（否则系统缩放会被绑定拉回）
        property bool userResized: false
        property real manualHeight: 300
        property real manualWidth: 300
        // 注意：floatWindow 内只能有一个 Component.onCompleted（多个会被 qmlcachegen
        // 判为「属性值设置多次」编译错误，导致整页加载失败——2026-08-19 踩过）
        // width 用普通属性（声明 300）+ onWidthChanged 写 manualWidth：若用绑定
        // 「width: userResized ? manualWidth : 300」+ onWidthChanged 写 manualWidth，
        // qmlcachegen 静态检测报「循环值绑定」（运行时标志无法打破静态检测）
        height: userResized ? manualHeight : Math.max(220, floatCol.implicitHeight + 4)
        onWidthChanged: {
            if (floatWindow.userResized) {
                floatWindow.manualWidth = floatWindow.width
                floatPosSaveTimer.restart()
            }
        }
        onHeightChanged: {
            if (floatWindow.userResized) {
                floatWindow.manualHeight = floatWindow.height
                floatPosSaveTimer.restart()
            }
        }
        Component.onCompleted: {
            // 兜底：创建完成后延迟恢复位置（首次可见可能不触发 onVisibleChanged）
            floatPosRestoreTimer.restart()
        }
        color: "transparent"
        // 恢复位置期间抑制实时保存（避免把未映射时的无效 x/y 存进 config）
        property bool restoringPos: false
        onVisibleChanged: {
            // 每次显示（含切页重建/重启后首次）都恢复记住的位置；窗口映射后再设，
            // 否则 Qt 6.5 无任务栏窗口在 show 前设置位置会落到无效坐标（-26214/INT_MIN）
            if (floatWindow.visible) {
                // 确保实际显示（Qt.Tool + transientParent 可能 show 竞态，启动时尤甚）
                if (floatWindow.visibility !== Window.Windowed) {
                    floatWindow.show()
                }
                floatWindow.restoringPos = true
                floatPosRestoreTimer.restart()
            }
        }
        // 恢复位置用 Timer（NoStack 下 Qt.callLater 可能不执行；延迟到窗口映射完成）
        Timer {
            id: floatPosRestoreTimer
            interval: 120
            onTriggered: {
                restoreFloatWindowPos()
                floatWindow.restoringPos = false
            }
        }
        // 拖动/移动实时保存（节流）：不依赖销毁/关闭时机，避免页面销毁瞬间 x/y 被
        // 重置导致保存坏值覆盖好值（切换页面/重启后位置失效的根因）
        onXChanged: { if (!floatWindow.restoringPos) floatPosSaveTimer.restart() }
        onYChanged: { if (!floatWindow.restoringPos) floatPosSaveTimer.restart() }
        Timer {
            id: floatPosSaveTimer
            interval: 350
            onTriggered: { if (floatWindow.visible) saveFloatWindowPos() }
        }
        onClosing: (close) => {
            // 应用退出（主窗口关闭）时放行，让浮窗随主窗口一起销毁，避免进程残留；
            // 否则仅隐藏而非销毁：保留对象供「浮窗」开关再次显示
            if (floatWindow.appQuitting) {
                return
            }
            page.panelShown = false
            configService.set("ui", "translatePanelVisible", false)
            saveFloatWindowPos()
            floatWindow.visibility = Window.Hidden
            close.accepted = false
        }
        // 跟随主窗口生命周期：最小化时隐藏浮窗、恢复时重新显示；主窗口关闭时放行浮窗关闭
        Connections {
            target: mainWindow
            function onVisibilityChanged() {
                if (mainWindow.visibility === Window.Minimized) {
                    floatWindow.minimized = true
                } else if (mainWindow.visibility === Window.Windowed) {
                    floatWindow.minimized = false
                }
            }
            function onClosing() {
                floatWindow.appQuitting = true
                floatWindow.close()
            }
        }

        // 自绘卡片（透明窗口 + 圆角 + 边框，保持 Fluent 观感）
        // 注：不加 layer/MultiEffect 阴影——layer 离屏渲染在非整数 DPI 缩放下文字模糊
        //（2026-08-19 用户实测，与编辑器卡片同因）
        Rectangle {
            anchors.fill: parent
            radius: tokens.radiusCard   // 内联 Window 组件内经 page 属性中转
            border.width: 1
            border.color: FluTheme.dividerColor
            color: tokens.bgFloatWindow
            clip: true

            ColumnLayout {
                id: floatCol
                anchors.fill: parent
                spacing: 0

                // 标题栏（原生系统拖动；关闭/切换经 Ribbon「翻译」标签的浮窗开关）
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    color: "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        FluText {
                            text: qsTr("翻译工具")
                            font.pixelSize: tokens.fontMenu
                            font.bold: true
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                    // 原生系统拖动（startSystemMove）：可拖到屏幕任意位置/跨屏，系统接管
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (mouse) => {
                            if (mouse.button === Qt.LeftButton) {
                                floatWindow.startSystemMove()
                            }
                        }
                    }
                }
                FluDivider { Layout.fillWidth: true }

                TranslatePanelContent {
                    id: floatingContent
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    translating: page.translating
                    progressDone: page.progressDone
                    progressTotal: page.progressTotal
                    hasSelection: page.selectedLines.length > 0
                    onTranslateCurrentRequested: translateCurrent()
                    onTranslateAllRequested: translateAllPending()
                    onTranslateSelectedRequested: translateSelected()
                    onCancelRequested: translator.cancelTranslation()
                }
            }
        }

        // ---------- 缩放手柄（原生系统缩放 startSystemResize：贴边/跨屏由系统接管） ----------
        // 右下角手柄（带 GripperResize 图标提示）+ 右/下细条手柄；按下时先锁定手动高度，
        // 避免系统缩放被 height 自动绑定拉回
        MouseArea {
            width: 6
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    floatWindow.userResized = true
                    floatWindow.manualHeight = floatWindow.height
                    floatWindow.startSystemResize(Qt.RightEdge)
                }
            }
        }
        MouseArea {
            height: 6
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeVerCursor
            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    floatWindow.userResized = true
                    floatWindow.manualHeight = floatWindow.height
                    floatWindow.startSystemResize(Qt.BottomEdge)
                }
            }
        }
        MouseArea {
            width: 18
            height: 18
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            onPressed: (mouse) => {
                if (mouse.button === Qt.LeftButton) {
                    floatWindow.userResized = true
                    floatWindow.manualHeight = floatWindow.height
                    floatWindow.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
                }
            }
            FluIcon {
                anchors.fill: parent
                iconSource: FluentIcons.GripperResize
                iconSize: 12
                iconColor: FluTheme.fontTertiaryColor
                opacity: 0.7
            }
        }
    }

    // ---------- 最近文件菜单 ----------
    // 注意：FluMenu 是 Popup 控件（HANDOVER §4 点名不可用），但此处是**已知可用姿势**：
    // 在可显示窗口内直接声明 + onAboutToShow 弹出前重建（非 onCompleted 创建），
    // 规避了 NoStack 下 "Popup 在不可见父级" 的错位/失效问题；若新增 Popup 控件，
    // 先照此模式验证，不要直接照抄 FluComboBox 的禁用结论。
    FluMenu {
        id: recentMenu
        // 弹出前重建（而非 onCompleted）：FluMenuItem 在不可见 Menu 内创建会触发
        // "Created graphical object was not placed in the graphics scene" 警告
        onAboutToShow: rebuildRecentMenu()
    }
    Component {
        id: recentItemComp
        FluMenuItem { }
    }
}
