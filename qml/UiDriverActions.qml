// UI 驱动操作实现（review agent 模拟用户操作的测试钩子）：
// 仅当 uiDriverBridge（UiDriverService，TRANSLEX_UI_DRIVER=1）存在时实例化并注册。
// 操作粒度 = 业务动作（打开文件/切主题/翻译/查询状态），走服务层接口。
// 命令协议见 src/driver_service.h。
import QtQuick
import FluentUI

QtObject {
    id: actions

    // ---- 打开文档（DocumentManager 分发：txt/trx/docx/pdf）----
    function openFile(path) {
        const ok = documentManager.openFile(path)
        return JSON.stringify({ ok: ok, name: documentManager.documentName(),
                                lines: documentModel.lineCount() })
    }

    // ---- 切换深浅色（FluTheme.darkMode：0=System 1=Light 2=Dark）----
    function setDark(dark) {
        FluTheme.darkMode = dark ? 2 : 1
        return JSON.stringify({ dark: FluTheme.dark })
    }

    // ---- 查询状态（review 断言用）----
    function getState() {
        return JSON.stringify({
            name: documentManager.documentName(),
            path: documentManager.currentPath(),
            lines: documentModel.lineCount(),
            currentLine: -1,
            dark: FluTheme.dark,
            limited: documentModel.limitedMode,
            comments: commentService.count()
        })
    }

    // ---- 翻译指定行（业务动作：走 TranslationService）----
    function translateLine(line) {
        if (line < 0 || line >= documentModel.lineCount()) {
            return JSON.stringify({ ok: false, error: "行号越界" })
        }
        const all = []
        for (let i = 0; i < documentModel.lineCount(); ++i) {
            all.push(documentModel.lineText(i))
        }
        // translateLines 为异步信号回调；此处仅触发并返回提交结果
        const ok = translationService.translateLines([line], all)
        return JSON.stringify({ ok: ok, line: line })
    }

    // ---- 翻译全部待译行 ----
    function translateAll() {
        const lines = []
        const all = []
        for (let i = 0; i < documentModel.lineCount(); ++i) {
            const text = documentModel.lineText(i)
            all.push(text)
            if (text.trim() && !documentModel.hasCommentAt(i)) {
                lines.push(i)
            }
        }
        if (lines.length === 0) {
            return JSON.stringify({ ok: true, translated: 0 })
        }
        const ok = translationService.translateLines(lines, all)
        return JSON.stringify({ ok: ok, translated: lines.length })
    }

    // ---- 导出（另存为：txt/trx/docx/pdf，按扩展名分发）----
    function saveFileAs(path) {
        const ok = documentManager.saveFileAs(path)
        return JSON.stringify({ ok: ok, name: documentManager.documentName(),
                                path: documentManager.currentPath() })
    }

    // ---- 新建文档 ----
    function newDocument() {
        documentManager.newDocument()
        return JSON.stringify({ ok: true, lines: documentModel.lineCount() })
    }

    // ---- 文档元数据（导出后检查 sourceFormat/sourceFile 等）----
    function getMeta() {
        return JSON.stringify(documentManager.documentMeta())
    }

    // ---- 读指定行文本（断言导出/编辑结果）----
    function getLineText(line) {
        if (line < 0 || line >= documentModel.lineCount()) {
            return JSON.stringify({ ok: false, error: "行号越界" })
        }
        return JSON.stringify({ ok: true, text: documentModel.lineText(line) })
    }

    // ---- 编辑指定行（模拟输入文字；arg = [line, text]）----
    function setLineText(arg) {
        const line = Number(arg[0])
        const text = String(arg[1])
        if (line < 0 || line >= documentModel.lineCount()) {
            return JSON.stringify({ ok: false, error: "行号越界" })
        }
        documentModel.updateLineText(line, text)
        return JSON.stringify({ ok: true, line: line, text: documentModel.lineText(line) })
    }

    // ---- 批注：读取 / 清空 ----
    function getComments() {
        return JSON.stringify(commentService.allComments())
    }
    function clearComments() {
        commentService.clear()
        return JSON.stringify({ ok: true })
    }

    // ---- 导航（main.qml 注入 navView 引用；index 0=编辑 1=设置）----
    property var navViewRef: null
    function navigate(index) {
        if (!navViewRef) {
            return JSON.stringify({ ok: false, error: "navView 未注入" })
        }
        navViewRef.setCurrentIndex(Number(index))
        return JSON.stringify({ ok: true, index: Number(index) })
    }

    Component.onCompleted: {
        // 仅在有驱动桥时注册（正常启动 uiDriverBridge 为 undefined）
        if (typeof uiDriverBridge !== "undefined" && uiDriverBridge !== null) {
            uiDriverBridge.setSink(actions)
        }
    }
}
