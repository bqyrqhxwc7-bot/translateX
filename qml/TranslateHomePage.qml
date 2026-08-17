import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import FluentUI
import Translex.Services 1.0

FluContentPage {
    id: page
    // NoStack 模式下由 FluNavigationView 的 FluLoader 直接加载，须显式填满父级
    //（否则页面宽度不跟随窗口，全屏/最大化后右侧出现透明空白）
    anchors.fill: parent
    title: qsTr("编辑")
    launchMode: FluPageType.SingleTask

    // 页面标题：自定义 header（FluPage 默认用 Title 字号偏大，缩小以留出更多编辑空间）
    header: Item {
        implicitHeight: 28
        FluText {
            text: page.title
            font.pixelSize: 14
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

    // 翻译服务（主进程提供，context property 全局可见）
    readonly property var translator: translationService

    // 视觉语言 token 实例（普通组件；delegate/内联组件内经页面属性中转）
    DesignTokens {
        id: tokens
    }

    // 图片行渲染：从文档 meta.images 取 base64 → data URI（docx 纯图段显示）
    // 结果缓存，避免每次委托创建都遍历 meta
    property var _imageUriCache: ({})
    property var _imageUriMetaVersion: 0
    function imageSource(imageId) {
        if (!imageId) return ""
        if (page._imageUriCache[imageId] !== undefined) return page._imageUriCache[imageId]
        const meta = documentManager.documentMeta()
        const images = meta && meta.images ? meta.images : []
        for (const img of images) {
            if (img.id === imageId && img.dataBase64) {
                const mime = img.format === "png" ? "image/png"
                           : img.format === "jpg" || img.format === "jpeg" ? "image/jpeg"
                           : img.format === "gif" ? "image/gif" : "image/png"
                const uri = "data:" + mime + ";base64," + img.dataBase64
                page._imageUriCache[imageId] = uri
                return uri
            }
        }
        return ""
    }

    // 状态栏图标（0=无图标）
    property int statusIconSource: 0
    property color statusIconColor: FluTheme.fontSecondaryColor
    // 大文件受限模式（>5 万行 / >200MB，DocumentManager 打开时自动置位）：
    // 禁富文本/图片渲染、批注编辑、翻译；编辑/滚动/查找/章节正常
    readonly property bool limited: documentModel.limitedMode
    // delegate/内联组件内不能直接访问实例 id（tokens），经页面属性中转
    //（Qt 6.5 的 QML pragma Singleton 在本应用内绑定不生效的替代方案，见 HANDOVER.md §6）
    readonly property int rowRadius: tokens.radiusControl
    readonly property color rowFindHighlight: tokens.findHighlight
    readonly property int cardRadius: tokens.radiusCard   // 浮窗（inline Window）用
    // 翻译进度状态
    property bool translating: false
    property int progressDone: 0
    property int progressTotal: 0
    // 翻译面板宽度（分隔条可拖拽调整，200~380）
    property real panelWidth: 280
    // 多选翻译：Ctrl+点击 加入/移出选中集
    property var selectedLines: []
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
        // 浮窗位置在 floatWindow.Component.onCompleted 中恢复（真实窗口屏幕坐标）
        // 翻译选项已由 TranslationService 从 ConfigService 持久化恢复，无需在此强制覆盖
    }

    // 通知条：NoStack 模式下页面由 FluLoader 加载，Window.window 附加属性解析失败
    // （运行时报 showWarning of null），故改用页面内 FluInfoBar 实例
    FluInfoBar {
        id: infoBar
        root: page
    }

    // 翻译结果回调 → 写入批注（走 CommentService，单一数据源）
    Connections {
        target: translationService
        function onLineTranslated(lineNumber, text, success) {
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
        }
        function onBatchFinished(total, ok, failed) {
            page.translating = false
            statusLabel.text = qsTr("翻译完成：成功 %1 / 失败 %2（共 %3 行）")
                                .arg(ok).arg(failed).arg(total)
        }
        function onTranslationStarted(total) {
            page.translating = true
            page.progressTotal = total
            page.progressDone = 0
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

    // 刷新状态栏：文档名 + 修改标记
    function refreshDocStatus() {
        documentNameLabel.text = documentManager.documentName()
        dirtyDot.visible = documentManager.isDirty()
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
            statusLabel.text = skipped > 0
                ? qsTr("没有需要翻译的行（已跳过 %1 行目标语言文本）").arg(skipped)
                : qsTr("没有可翻译的行")
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
            statusLabel.text = skipped > 0
                ? qsTr("选中的行均为目标语言，无需翻译")
                : qsTr("没有可翻译的选中行")
            return
        }
        statusLabel.text = skipped > 0
            ? qsTr("正在翻译选中 %1 行（已跳过 %2 行目标语言文本）...").arg(lines.length).arg(skipped)
            : qsTr("正在翻译选中 %1 行...").arg(lines.length)
        statusIconSource = FluentIcons.Sync
        statusIconColor = FluTheme.primaryColor
        translator.translateLines(lines, all)
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

    // ---------- 浮窗位置（真实窗口，屏幕坐标持久化） ----------
    // ---------- 浮窗位置（真实窗口，屏幕坐标持久化） ----------
    function saveFloatWindowPos() {
        const x = floatWindow.x
        const y = floatWindow.y
        if (isFinite(x) && isFinite(y) && x > -10000 && y > -10000) {
            configService.set("ui", "translatePanelX", Math.round(x))
            configService.set("ui", "translatePanelY", Math.round(y))
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
            font.pixelSize: 14

            // 文件标签（DocumentManager service 的 PPT 式功能区）
            FluPivotItem {
                title: qsTr("文件")
                contentItem: Component {
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 8
                        FluButton { text: qsTr("新建"); onClicked: newDocument() }
                        FluButton { text: qsTr("打开"); onClicked: openDocument() }
                        FluButton { text: qsTr("最近"); enabled: page.hasRecent; onClicked: recentMenu.popup() }
                        FluButton { text: qsTr("保存"); onClicked: saveDocument() }
                        FluButton { text: qsTr("另存为"); onClicked: saveDocumentAs() }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        FluButton { text: qsTr("加载示例"); onClicked: loadDemoDocument() }
                        FluButton { text: qsTr("清除译文"); onClicked: clearAllComments() }
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
                        FluButton { text: qsTr("撤销"); enabled: page.canUndo; onClicked: { documentModel.undo(); afterUndoRedo() } }
                        FluButton { text: qsTr("重做"); enabled: page.canRedo; onClicked: { documentModel.redo(); afterUndoRedo() } }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        FluText { text: qsTr("共 %1 行").arg(documentModel.lineCount()); font.pixelSize: 12; color: FluTheme.fontSecondaryColor }
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
                        FluFilledButton { text: qsTr("翻译当前行"); enabled: !page.limited; onClicked: translateCurrent() }
                        FluButton { text: qsTr("翻译全部待译行"); enabled: !page.limited; onClicked: translateAllPending() }
                        FluButton { text: qsTr("翻译选中行"); enabled: page.selectedLines.length > 0 && !page.limited; onClicked: translateSelected() }
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
                        // 翻译进度（翻译中显示在右侧）
                        FluProgressBar {
                            visible: page.translating
                            Layout.preferredWidth: 160
                            from: 0
                            to: Math.max(page.progressTotal, 1)
                            value: page.progressDone
                        }
                        FluText { visible: page.translating; text: qsTr("%1/%2").arg(page.progressDone).arg(page.progressTotal); font.pixelSize: 12 }
                        FluButton { visible: page.translating; text: qsTr("取消"); onClicked: translator.cancelTranslation() }
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
                        FluText { text: qsTr("章节"); font.pixelSize: 12; color: FluTheme.fontSecondaryColor }
                        FluButton { text: qsTr("上一章"); enabled: page.chapterTitles.length > 1; onClicked: page.goPrevChapter() }
                        FluButton { text: qsTr("下一章"); enabled: page.chapterTitles.length > 1; onClicked: page.goNextChapter() }
                        FluText {
                            text: page.currentChapterIndex >= 0
                                ? qsTr("%1 · 共 %2 章").arg(page.chapterTitles[page.currentChapterIndex] || "").arg(page.chapterTitles.length)
                                : qsTr("共 %1 章").arg(page.chapterTitles.length)
                            Layout.maximumWidth: 260
                            elide: Text.ElideRight
                            font.pixelSize: 12
                            color: FluTheme.fontSecondaryColor
                        }
                        Item { Layout.fillWidth: true }
                        FluButton { text: qsTr("重新检测"); onClicked: page.rebuildChapters() }
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
                        FluText { text: qsTr("批注 %1 条").arg(page.commentCount); font.pixelSize: 12; color: FluTheme.fontSecondaryColor }
                        FluButton { text: qsTr("上一条"); enabled: page.commentCount > 1; onClicked: page.goPrevComment() }
                        FluButton { text: qsTr("下一条"); enabled: page.commentCount > 1; onClicked: page.goNextComment() }
                        FluDivider { orientation: Qt.Vertical; Layout.preferredHeight: 24; Layout.alignment: Qt.AlignVCenter }
                        FluButton { text: qsTr("清空"); onClicked: page.clearAllComments() }
                        FluButton { text: qsTr("导出"); onClicked: page.exportComments() }
                        FluButton { text: qsTr("导入"); onClicked: page.importComments() }
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
                        FluButton { text: qsTr("上一个"); onClicked: page.doFindPrev(findInput.text) }
                        FluButton { text: qsTr("下一个"); onClicked: page.doFindNext(findInput.text) }
                        FluText { text: qsTr("%1 处").arg(page.findResultCount); font.pixelSize: 12; color: FluTheme.fontSecondaryColor }
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
                    font.pixelSize: 12
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
                    // 图片行：行号 36 + 图片区 180 + 边距；其余：原文 36 + 批注区（自动换行）
                    height: row.model.display === "image"
                            ? 224
                            : 36 + (row.model.hasComment || page.commentDraftLine === index
                                    ? Math.max(20, commentMeasurer.contentHeight) + 6 : 0)
                    radius: page.rowRadius   // 经页面属性中转（delegate 内单例访问缺陷，见上）
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
                            return page.rowFindHighlight
                        }
                        return row.hovered ? FluTheme.itemHoverColor : "transparent"
                    }

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
                    function startCommentEdit() {
                        commentEditor.text = row.model.commentText
                        Qt.callLater(function () {
                            commentEditor.forceActiveFocus()
                            commentEditor.cursorPosition = commentEditor.length
                        })
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

                    // 当前行左侧强调条（Fluent 选中指示）
                    Rectangle {
                        visible: index === page.currentLine
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 3
                        radius: 1.5
                        color: FluTheme.primaryColor
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
                            font.pixelSize: 12
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
                                radius: 1
                                color: FluTheme.primaryColor
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
                        Text {
                            text: row.model.rich || row.model.text
                            textFormat: Text.RichText
                            font.pixelSize: page.originalFontSize
                            color: FluTheme.fontPrimaryColor
                            Layout.fillWidth: true
                            verticalAlignment: Text.AlignVCenter
                            wrapMode: Text.Wrap
                            visible: page.currentLine !== index && row.model.display === "rich"
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
                    font.pixelSize: 12
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
                    font.pixelSize: 12
                    font.bold: true
                    color: FluTheme.fontPrimaryColor
                }
                FluText {
                    text: currentLine >= 0 ? qsTr("当前行 %1").arg(currentLine + 1) : ""
                    font.pixelSize: 12
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
        nameFilters: [qsTr("翻译文档 (*.trx)"), qsTr("文本文件 (*.txt)"), qsTr("PDF 文档 (*.pdf)"), qsTr("所有文件 (*)")]
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
                            radius: page.rowRadius   // 经页面属性中转（delegate 内单例访问缺陷）
                            color: menuRowHover.hovered ? FluTheme.itemHoverColor : "transparent"
                            opacity: parent.menuRowEnabled ? 1 : 0.4
                            HoverHandler { id: menuRowHover }
                            FluText {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.text || ""   // 分隔线行无 text
                                font.pixelSize: 13
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
            color: Qt.rgba(0, 0, 0, 0.35)
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
                    font.pixelSize: 15
                    font.bold: true
                    color: FluTheme.fontPrimaryColor
                }
                // 原文字号（滑动条）
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    FluText {
                        text: qsTr("原文字号 %1").arg(page.originalFontSize)
                        font.pixelSize: 13
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
                        font.pixelSize: 13
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
                    font.pixelSize: 12
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

    // ---------- 浮动翻译窗口（真独立 Window：可拖到屏幕任意位置，不再受页面范围限制） ----------
    // 页面内浮层会被导航视图/窗口边界裁剪，拖出页面即失效（用户确认）；改用真 Window。
    // Qt.Tool：无任务栏、经 transientParent 跟随主窗口；FramelessWindowHint：去掉系统原生
    // 标题栏（否则与 Fluent 风格违和）；自绘卡片（圆角+边框）+ 标题栏（startSystemMove 原生
    // 拖动）。关闭/切换经 Ribbon「翻译」标签的浮窗开关。位置持久化为屏幕坐标，恢复时钳制到桌面内防止无任务栏找不到。
    Window {
        id: floatWindow
        visible: page.panelMode === "floating" && page.panelShown
        transientParent: mainWindow
        flags: Qt.Tool | Qt.FramelessWindowHint
        title: qsTr("翻译工具")
        width: 300
        height: Math.max(220, floatCol.implicitHeight + 4)
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
            // 隐藏而非销毁：保留对象供「浮窗」开关再次显示
            page.panelShown = false
            configService.set("ui", "translatePanelVisible", false)
            saveFloatWindowPos()
            floatWindow.visibility = Window.Hidden
            close.accepted = false
        }
        // 兜底：创建完成后延迟恢复位置（首次可见可能不触发 onVisibleChanged）
        Component.onCompleted: {
            floatPosRestoreTimer.restart()
        }

        // 自绘卡片（透明窗口 + 圆角 + 边框，保持 Fluent 观感）
        Rectangle {
            anchors.fill: parent
            radius: page.cardRadius   // 内联 Window 组件内经 page 属性中转
            border.width: 1
            border.color: FluTheme.dividerColor
            color: FluTheme.dark ? Qt.rgba(0.13, 0.13, 0.13, 0.98) : Qt.rgba(1, 1, 1, 0.98)
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
                            font.pixelSize: 13
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
    }

    // ---------- 最近文件菜单 ----------
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
