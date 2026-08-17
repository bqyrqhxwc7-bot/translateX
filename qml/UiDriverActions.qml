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

    Component.onCompleted: {
        // 仅在有驱动桥时注册（正常启动 uiDriverBridge 为 undefined）
        if (typeof uiDriverBridge !== "undefined" && uiDriverBridge !== null) {
            uiDriverBridge.setSink(actions)
        }
    }
}
