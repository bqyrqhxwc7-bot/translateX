# TTS 朗读服务（迭代3）设计

> 状态：✅ 已实现（2026-08-17）
> 路线图：HANDOVER.md §3 迭代3（用户确认：选中行朗读 + 选区朗读；语速 + 暂停/停止；语音跟随系统默认）
> 跨平台：Qt6TextToSpeech（Windows=SAPI / macOS=AVSpeechSynthesizer / Linux=speech-dispatcher）

## 1. 目标与定位

- **独立 service**：`TextToSpeechService` 不依赖 TranslationService/DocumentModel 等其他 service，只做"文本 → 语音"一件事；行文本/译文/选区的组装由 QML 层完成
- 朗读内容（用户确认）：
  1. **选中行**（工具栏「朗读」按钮；Ctrl+点击多选 → 逐行朗读，当前行高亮跟随）
  2. **选区**（行内编辑框 selectedText 非空时，右键菜单「朗读选区」）
- 行内容规则：该行有译文批注 → 朗读译文（听译文校验）；无批注 → 朗读原文

## 2. 跨平台与降级（贡献者友好）

- CMake 条件编译：`find_package(Qt6 COMPONENTS TextToSpeech QUIET)` + `if(TARGET Qt6::TextToSpeech)` 才链接并定义 `TRANSLEX_HAS_TTS=1`——**未安装 TextToSpeech 模块的平台照常构建**（service 为空实现）
- 运行时无可用引擎（如 Linux 无 speech-dispatcher）：`QTextToSpeech::availableEngines()` 为空 → service 进入降级态，`speak*` 返回 false 且发 `unavailable()` 信号，QML 提示"系统无 TTS 引擎"，不崩溃
- 语音跟随系统默认（不提供语音选择）；语速 0.5x~2.0x

## 3. 接口

```cpp
class TextToSpeechService : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE bool speakText(const QString &text);          // 单段（选区/单行）
    Q_INVOKABLE bool speakLines(const QVariantList &items);   // [{line:int, text:str}] 逐行
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE double rate() const;                          // 0.5~2.0
    Q_INVOKABLE void setRate(double rate);                    // 写回 ConfigService
    Q_PROPERTY(bool speaking READ speaking NOTIFY stateChanged)
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
signals:
    void stateChanged();          // speaking/available 变化
    void lineStarted(int lineNumber);  // speakLines 逐行开始（QML 高亮跟随）
    void finished();              // 队列全部播完
    void unavailable();           // 无 TTS 引擎
};
```

- `speakLines`：内部队列，`QTextToSpeech::stateChanged` 驱动逐条播放；`lineStarted` 带行号（-1 表示纯文本项）
- `speakText`：立即替换当前播放（stop 当前 + 播放新文本）
- `rate` 初始化从 `ConfigService.get("textToSpeech", "rate")` 读；`setRate` 写回（VSCode-like 模式）

## 4. 配置

`src/services/config/textToSpeech.json`（新 section，ConfigService 自动扫描 `:/config/*.json`）：

```json
{
  "id": "textToSpeech",
  "displayName": "朗读",
  "settings": [
    {
      "key": "rate",
      "displayName": "朗读语速",
      "description": "0.5x~2.0x，语音跟随系统默认",
      "type": "number",
      "default": 1.0,
      "min": 0.5,
      "max": 2.0,
      "step": 0.1,
      "group": "朗读"
    }
  ]
}
```

设置页由 ConfigSectionCard 自动渲染（number 滑条）。

## 5. QML UI

- 工具栏「朗读」按钮（`FluButton`，朗读选中行；朗读中变「停止」）：
  - `page.selectedLines` 非空 → `speakLines`（每行：有译文读译文，否则读原文）
  - 朗读中点击 → `stop()`
- 右键菜单（lineMenu）追加：
  - 「朗读此行」（`speakLines` 单行）
  - 「朗读选区」（`lineEditor.selectedText` 非空时显示；`speakText`）
- 逐行朗读高亮：`lineStarted(line)` → 高亮该行（复用现有选中行高亮机制）
- 无引擎：`unavailable()` → FluInfoBar 提示

## 6. 测试（tst_texttospeech）

- `rateRoundTrip`：ConfigService 隔离目录，setRate 写回 → 新实例读回一致
- `gracefulDegrade`：无 TTS 引擎环境（CI/Linux）下 `speakText` 返回 false、`available` 为 false、不崩溃（有引擎环境同样通过——只断言不崩溃与状态一致性）
- `speakLinesEmpty`：空列表直接返回 true 且不发信号
- 有引擎环境（Windows SAPI）额外断言 `available()==true`（QSKIP 无引擎）

## 7. 限制（明示）

- 语音不可选（跟随系统默认）；语速 0.5~2.0
- 选区朗读仅限行内编辑框选区（编辑器为单行编辑模型，无跨行选区）
- 无 TTS 模块/引擎的平台：功能不可用但构建与运行不受影响
- 朗读不阻塞 UI（QTextToSpeech 异步）