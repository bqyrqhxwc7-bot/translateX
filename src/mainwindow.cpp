#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QAction>
#include <QApplication>
#include <QSignalBlocker>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDialog>
#include <QFormLayout>
#include <QFrame>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHash>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressBar>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSaveFile>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QToolBar>
#include <QUrlQuery>
#include <QUuid>
#include <QVBoxLayout>
#include <QXmlStreamReader>
#include <QtConcurrent>

namespace {

QString escapeXmlText(const QString &text)
{
    QString escaped = text;
    escaped.replace("&", "&amp;");
    escaped.replace("<", "&lt;");
    escaped.replace(">", "&gt;");
    escaped.replace("\"", "&quot;");
    return escaped;
}

QString quoteForPowerShell(QString value)
{
    value.replace("'", "''");
    return QString("'%1'").arg(value);
}

QString formatDurationLabel(qint64 totalSeconds)
{
    const qint64 safeSeconds = qMax<qint64>(0, totalSeconds);
    const qint64 hours = safeSeconds / 3600;
    const qint64 minutes = (safeSeconds % 3600) / 60;
    const qint64 seconds = safeSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours, 2, 10, QLatin1Char('0'))
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

bool isTranslationTimeoutMessage(const QString &message)
{
    return message.contains(QStringLiteral("总耗时已超过"))
        || message.contains(QStringLiteral("翻译请求超时"));
}

QString normalizeOllamaEndpoint(QString endpoint)
{
    endpoint = endpoint.trimmed();
    if (endpoint.isEmpty()) {
        endpoint = QStringLiteral("http://127.0.0.1:11434");
    }
    if (!endpoint.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !endpoint.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        endpoint.prepend(QStringLiteral("http://"));
    }
    while (endpoint.endsWith(QLatin1Char('/'))) {
        endpoint.chop(1);
    }
    return endpoint;
}

QStringList requestAvailableOllamaModels(const QString &endpoint, QString *errorMessage)
{
    QNetworkRequest request(QUrl(normalizeOllamaEndpoint(endpoint) + QStringLiteral("/api/tags")));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VSCodeQt/1.0"));
    request.setRawHeader("Accept", "application/json");

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.get(request);
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    timeoutTimer.start(3000);
    loop.exec();
    timeoutTimer.stop();

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (timedOut) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("扫描本地 Ollama 超时，请检查地址或稍后重试。");
        }
        return {};
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法连接到该 Ollama 地址：%1").arg(networkMessage);
        }
        return {};
    }

    const QJsonDocument jsonDocument = QJsonDocument::fromJson(payload);
    if (!jsonDocument.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Ollama 返回了无效的模型列表数据。");
        }
        return {};
    }

    QStringList models;
    const QJsonArray modelArray = jsonDocument.object().value(QStringLiteral("models")).toArray();
    for (const QJsonValue &value : modelArray) {
        const QString modelName = value.toObject().value(QStringLiteral("name")).toString().trimmed();
        if (!modelName.isEmpty() && !models.contains(modelName)) {
            models.append(modelName);
        }
    }

    if (models.isEmpty() && errorMessage) {
        *errorMessage = QStringLiteral("已连接到 Ollama，但没有扫描到可用模型。请先拉取模型后再试。");
    }
    return models;
}

QString translationTagForLine(int zeroBasedLineNumber)
{
    return QStringLiteral("[[L%1]]").arg(zeroBasedLineNumber + 1, 5, 10, QLatin1Char('0'));
}

QString buildContextualTranslationPrompt(const QStringList &sourceLines, const QList<int> &targetLines, int contextStartLine, int contextEndLine, bool strictOutputParsing)
{
    QStringList requestedTags;
    requestedTags.reserve(targetLines.size());
    for (int lineNumber : targetLines) {
        requestedTags.append(translationTagForLine(lineNumber));
    }

    QStringList contextLines;
    contextLines.reserve(contextEndLine - contextStartLine + 1);
    for (int lineNumber = contextStartLine; lineNumber <= contextEndLine; ++lineNumber) {
        contextLines.append(QStringLiteral("%1 %2").arg(translationTagForLine(lineNumber), sourceLines.value(lineNumber)));
    }

    const QString strictHint = strictOutputParsing ? QStringLiteral("2. 每一行输出格式必须严格为 [[L00001]] 译文。\n3. 不要解释，不要添加前言，不要遗漏行，不要合并行。\n")
                                           : QStringLiteral("2. 每一行输出格式请尽量按照 [[L00001]] 译文 输出；不必过于死板。\n3. 重点输出翻译结果，避免解释和扩展。\n");

    return QStringLiteral(
               "你是专业翻译。请结合上下文，将指定行翻译成中文。\n"
               "要求：\n"
               "1. 只翻译“需要输出的行”。\n%1"
               "4. 需要保持人名、代词、时态和语气在上下文中一致。\n"
               "5. 如果上下文能帮助理解省略主语或代词，请优先结合上下文处理。\n\n"
               "需要输出的行：\n%2\n\n"
               "原文上下文：\n%3")
        .arg(strictHint, requestedTags.join(QChar('\n')), contextLines.join(QChar('\n')));
}

QString buildSingleLineContextualTranslationPrompt(const QStringList &sourceLines, int targetLine, int contextStartLine, int contextEndLine)
{
    QStringList contextLines;
    contextLines.reserve(contextEndLine - contextStartLine + 1);
    for (int lineNumber = contextStartLine; lineNumber <= contextEndLine; ++lineNumber) {
        const QString prefix = lineNumber == targetLine ? QStringLiteral("[目标]") : QStringLiteral("[参考]");
        contextLines.append(QStringLiteral("%1 %2").arg(prefix, sourceLines.value(lineNumber)));
    }

    return QStringLiteral(
               "你是专业翻译。请结合上下文，将标记为[目标]的这一行翻译成中文。\n"
               "要求：\n"
               "1. 只输出目标行的中文译文。\n"
               "2. 不要解释，不要附加说明，不要重复原文。\n"
               "3. 参考上下文统一代词、时态、语气和术语。\n"
               "4. 如果上下文不足，就按目标行原意自然翻译。\n"
               "5. 最终输出格式必须严格为 <translation>译文</translation> 。标签外不要输出任何内容。\n\n"
               "上下文：\n%1")
        .arg(contextLines.join(QChar('\n')));
}

QString extractTaggedTranslationPayload(const QString &text)
{
    const QList<QPair<QString, QString>> markers = {
        qMakePair(QStringLiteral("<translation>"), QStringLiteral("</translation>")),
        qMakePair(QStringLiteral("<final>"), QStringLiteral("</final>")),
        qMakePair(QStringLiteral("[[TRANSLATION]]"), QStringLiteral("[[/TRANSLATION]]")),
    };

    for (const auto &marker : markers) {
        const int startIndex = text.lastIndexOf(marker.first, -1, Qt::CaseInsensitive);
        if (startIndex < 0) {
            continue;
        }
        const int contentStart = startIndex + marker.first.size();
        const int endIndex = text.indexOf(marker.second, contentStart, Qt::CaseInsensitive);
        if (endIndex < 0) {
            continue;
        }
        const QString extracted = text.mid(contentStart, endIndex - contentStart).trimmed();
        if (!extracted.isEmpty()) {
            return extracted;
        }
    }

    return QString();
}

bool looksLikeTranslationMetaLine(const QString &line)
{
    static const QStringList metaHints = {
        QStringLiteral("用户"),
        QStringLiteral("要求"),
        QStringLiteral("上下文"),
        QStringLiteral("目标行"),
        QStringLiteral("参考"),
        QStringLiteral("输出"),
        QStringLiteral("解释"),
        QStringLiteral("原文"),
        QStringLiteral("需要翻译"),
        QStringLiteral("所以"),
        QStringLiteral("首先"),
        QStringLiteral("另外"),
        QStringLiteral("最终决定"),
        QStringLiteral("在中文中"),
        QStringLiteral("我认为"),
        QStringLiteral("我需要"),
        QStringLiteral("这里的"),
    };

    for (const QString &hint : metaHints) {
        if (line.contains(hint, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QHash<int, QString> parseTaggedTranslationResponse(const QString &responseText)
{
    QHash<int, QString> translations;
    const QString normalizedResponse = responseText.trimmed();
    if (normalizedResponse.isEmpty()) {
        return translations;
    }

    const QRegularExpression tagPattern(QStringLiteral(R"(\[\[L(\d{1,5})\]\])"));
    QRegularExpressionMatchIterator it = tagPattern.globalMatch(normalizedResponse);
    int lastLineNumber = -1;
    int lastEnd = 0;

    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int lineNumber = match.captured(1).toInt() - 1;
        const int start = match.capturedStart();
        if (lastLineNumber >= 0 && lastEnd <= start) {
            const QString section = normalizedResponse.mid(lastEnd, start - lastEnd).trimmed();
            if (!section.isEmpty()) {
                translations.insert(lastLineNumber, section);
            }
        }
        lastLineNumber = lineNumber;
        lastEnd = match.capturedEnd();
    }

    if (lastLineNumber >= 0 && lastEnd <= normalizedResponse.size()) {
        const QString section = normalizedResponse.mid(lastEnd).trimmed();
        if (!section.isEmpty()) {
            translations.insert(lastLineNumber, section);
        }
    }

    return translations;
}

QHash<int, QString> parseSequentialTranslationResponse(const QString &responseText, const QList<int> &targetLines)
{
    QHash<int, QString> translations;
    const QString normalizedResponse = responseText.trimmed();
    if (normalizedResponse.isEmpty() || targetLines.isEmpty()) {
        return translations;
    }

    QStringList lines = normalizedResponse.split(QChar('\n'), Qt::KeepEmptyParts);
    QStringList candidateLines;
    for (QString line : lines) {
        line = line.trimmed();
        if (!line.isEmpty()) {
            candidateLines.append(line);
        }
    }

    if (candidateLines.size() < targetLines.size()) {
        return translations;
    }

    for (int i = 0; i < targetLines.size() && i < candidateLines.size(); ++i) {
        translations.insert(targetLines.at(i), candidateLines.at(i).trimmed());
    }
    return translations;
}

bool isTranslationCanceled(const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    return cancelFlag && cancelFlag->load();
}

QString tpxPayloadMagic()
{
    return QStringLiteral("TPX-EDITOR-DOC");
}

QString legacyTrxPayloadMagic()
{
    return QStringLiteral("TRX-EDITOR-DOC");
}

QString appSettingsGroup()
{
    return QStringLiteral("MainWindow");
}

QString shortcutSettingsKey()
{
    return QStringLiteral("shortcutOverrides");
}

QString defaultThemeName()
{
    return QStringLiteral("Fluent Sandstone");
}

QString autosaveStateLabel(bool enabled)
{
    return enabled ? QStringLiteral("自动保存已开启") : QStringLiteral("自动保存已关闭");
}

QString decodeProcessText(const QByteArray &data)
{
    if (data.isEmpty()) {
        return QString();
    }

    const QString utf8 = QString::fromUtf8(data);
    if (!utf8.contains(QChar::ReplacementCharacter)) {
        return utf8.trimmed();
    }
    return QString::fromLocal8Bit(data).trimmed();
}

QString summarizeHtml(const QString &html)
{
    const QString text = QTextDocumentFragment::fromHtml(html).toPlainText().simplified();
    if (text.isEmpty()) {
        return QStringLiteral("空注释");
    }
    return text.size() > 24 ? text.left(24) + QStringLiteral("...") : text;
}

QString summarizeSourceLine(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty()) {
        return QStringLiteral("空行");
    }
    return simplified.size() > 30 ? simplified.left(30) + QStringLiteral("...") : simplified;
}

QScrollArea *createDockScrollArea(QWidget *content, QWidget *parent)
{
    auto *scrollArea = new QScrollArea(parent);
    scrollArea->setObjectName(QStringLiteral("dockScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidget(content);
    return scrollArea;
}

QJsonObject appearanceToJson(const AnnotatedTextEdit::TextAppearance &appearance)
{
    QJsonObject object;
    object.insert(QStringLiteral("family"), appearance.family);
    object.insert(QStringLiteral("pointSize"), appearance.pointSize);
    object.insert(QStringLiteral("fontWeight"), appearance.fontWeight);
    object.insert(QStringLiteral("italic"), appearance.italic);
    object.insert(QStringLiteral("underline"), appearance.underline);
    object.insert(QStringLiteral("textColor"), appearance.textColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("backgroundColor"), appearance.backgroundColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("leftMargin"), appearance.leftMargin);
    object.insert(QStringLiteral("topMargin"), appearance.topMargin);
    object.insert(QStringLiteral("bottomMargin"), appearance.bottomMargin);
    object.insert(QStringLiteral("highlightAnnotatedLines"), appearance.highlightAnnotatedLines);
    object.insert(QStringLiteral("highlightFullWidth"), appearance.highlightFullWidth);
    object.insert(QStringLiteral("annotatedLineColor"), appearance.annotatedLineColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("annotatedLineBorderColor"), appearance.annotatedLineBorderColor.name(QColor::HexArgb));
    object.insert(QStringLiteral("annotatedLinePadding"), appearance.annotatedLinePadding);
    return object;
}

AnnotatedTextEdit::TextAppearance appearanceFromJson(
    const QJsonObject &object,
    const AnnotatedTextEdit::TextAppearance &fallback)
{
    AnnotatedTextEdit::TextAppearance appearance = fallback;
    appearance.family = object.value(QStringLiteral("family")).toString(fallback.family);
    appearance.pointSize = object.value(QStringLiteral("pointSize")).toInt(fallback.pointSize);
    appearance.fontWeight = object.value(QStringLiteral("fontWeight")).toInt(fallback.fontWeight);
    appearance.italic = object.value(QStringLiteral("italic")).toBool(fallback.italic);
    appearance.underline = object.value(QStringLiteral("underline")).toBool(fallback.underline);
    appearance.leftMargin = object.value(QStringLiteral("leftMargin")).toInt(fallback.leftMargin);
    appearance.topMargin = object.value(QStringLiteral("topMargin")).toInt(fallback.topMargin);
    appearance.bottomMargin = object.value(QStringLiteral("bottomMargin")).toInt(fallback.bottomMargin);

    const QColor textColor(object.value(QStringLiteral("textColor")).toString());
    if (textColor.isValid()) {
        appearance.textColor = textColor;
    }

    const QColor backgroundColor(object.value(QStringLiteral("backgroundColor")).toString());
    if (backgroundColor.isValid()) {
        appearance.backgroundColor = backgroundColor;
    }

    appearance.highlightAnnotatedLines = object.value(QStringLiteral("highlightAnnotatedLines")).toBool(fallback.highlightAnnotatedLines);
    appearance.highlightFullWidth = object.value(QStringLiteral("highlightFullWidth")).toBool(fallback.highlightFullWidth);

    const QColor annotatedLineColor(object.value(QStringLiteral("annotatedLineColor")).toString());
    if (annotatedLineColor.isValid()) {
        appearance.annotatedLineColor = annotatedLineColor;
    }

    const QColor annotatedLineBorderColor(object.value(QStringLiteral("annotatedLineBorderColor")).toString());
    if (annotatedLineBorderColor.isValid()) {
        appearance.annotatedLineBorderColor = annotatedLineBorderColor;
    }

    appearance.annotatedLinePadding = object.value(QStringLiteral("annotatedLinePadding")).toInt(fallback.annotatedLinePadding);

    return appearance;
}

QString appearanceToSettingsString(const AnnotatedTextEdit::TextAppearance &appearance)
{
    return QString::fromUtf8(QJsonDocument(appearanceToJson(appearance)).toJson(QJsonDocument::Compact));
}

AnnotatedTextEdit::TextAppearance appearanceFromSettingsString(
    const QVariant &value,
    const AnnotatedTextEdit::TextAppearance &fallback)
{
    const QByteArray json = value.toString().toUtf8();
    if (json.isEmpty()) {
        return fallback;
    }

    const QJsonDocument document = QJsonDocument::fromJson(json);
    return document.isObject() ? appearanceFromJson(document.object(), fallback) : fallback;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , editor(nullptr)
    , navigationDock(nullptr)
    , inspectorDock(nullptr)
    , findDock(nullptr)
    , commentDock(nullptr)
    , translationDock(nullptr)
    , recentFilesList(nullptr)
    , chapterList(nullptr)
    , commentLineList(nullptr)
    , translationProgressBar(nullptr)
    , translationProgressTimeLabel(nullptr)
    , translationCancelButton(nullptr)
    , fileNameValueLabel(nullptr)
    , filePathValueLabel(nullptr)
    , formatValueLabel(nullptr)
    , charValueLabel(nullptr)
    , lineValueLabel(nullptr)
    , savedAtValueLabel(nullptr)
    , autosaveValueLabel(nullptr)
    , statusSummaryLabel(nullptr)
    , statusDetailLabel(nullptr)
    , autosaveCheckBox(nullptr)
    , findLineEdit(nullptr)
    , replaceLineEdit(nullptr)
    , mainToolBar(nullptr)
    , recentFilesMenu(nullptr)
    , newAction(nullptr)
    , openAction(nullptr)
    , saveAction(nullptr)
    , saveAsAction(nullptr)
    , exportTxtAction(nullptr)
    , exportDocxAction(nullptr)
    , exportTrxAction(nullptr)
    , autosaveNowAction(nullptr)
    , exitAction(nullptr)
    , undoAction(nullptr)
    , redoAction(nullptr)
    , cutAction(nullptr)
    , copyAction(nullptr)
    , pasteAction(nullptr)
    , selectAllAction(nullptr)
    , showFindAction(nullptr)
    , findNextAction(nullptr)
    , findPreviousAction(nullptr)
    , replaceAction(nullptr)
    , replaceAllAction(nullptr)
    , focusModeAction(nullptr)
    , insertCommentAction(nullptr)
    , batchInsertCommentsAction(nullptr)
    , translateLinesAction(nullptr)
    , translateDocumentAction(nullptr)
    , deleteCommentAction(nullptr)
    , toggleCommentAction(nullptr)
    , deleteSelectedCommentsAction(nullptr)
    , toggleSelectedCommentsAction(nullptr)
    , previousCommentAction(nullptr)
    , nextCommentAction(nullptr)
    , sourceAppearanceAction(nullptr)
    , commentAppearanceAction(nullptr)
    , settingsAction(nullptr)
    , autosaveTimer(new QTimer(this))
    , statsRefreshTimer(new QTimer(this))
    , chapterRefreshTimer(new QTimer(this))
    , translationUiFlushTimer(new QTimer(this))
    , translationElapsedUiTimer(new QTimer(this))
    , chapterIndexWatcher(new QFutureWatcher<QVector<ChapterEntry>>(this))
    , translationWatcher(new QFutureWatcher<TranslationTaskResult>(this))
    , translationProgressDialog(nullptr)
    , translationCancelFlag(nullptr)
    , translationUiUpdateMutex()
    , translationPendingUiUpdates()
    , translationUiFlushScheduled(false)
    , translationPendingSourceTexts()
    , translationOllamaEndpoint(QStringLiteral("http://127.0.0.1:11434"))
    , translationOllamaModel(QStringLiteral("qwen3:14b-q4_K_M"))
    , translationProgressTitle()
    , translationSuccessLabelTemplate()
    , defaultSourceAppearance()
    , defaultCommentAppearance()
    , currentFormat(DocumentFormat::Txt)
    , autosaveEnabled(true)
    , focusModeEnabled(false)
    , translationUseOllama(true)
    , translationFallbackToOnline(true)
    , translationDisableThinking(true)
    , translationEnableCustomPrompt(false)
    , translationCustomPromptTemplate(QStringLiteral("你是专业翻译。请将下面的原文翻译成中文，只输出译文，不要解释，不要附加说明，不要保留原文。\n原文：\n%1"))
    , translationCustomContextPromptTemplate(QStringLiteral("你是专业翻译。请结合上下文，将标记为[目标]的这一行翻译成中文。要求：\n1. 只输出目标行的中文译文。\n2. 不要解释，不要附加说明，不要重复原文。\n3. 参考上下文统一代词、时态、语气和术语。\n4. 如果上下文不足，就按目标行原意自然翻译。\n上下文：\n%2"))
    , translationLargeModelApiEndpoint()
    , translationLargeModelApiKey()
    , translationConfigPreset(QStringLiteral("默认"))
    , translationMaxChunkTargetLines(20)
    , translationMaxChunkChars(8000)
    , translationStrictOutputParsing(true)
    , translationLocalStrategy(LocalTranslationStrategy::Responsive)
    , suppressDocumentRefresh(false)
    , rememberWindowLayout(true)
    , commentManagerDirty(false)
    , inspectorPanelDirty(false)
    , chapterIndexDirty(false)
    , autosaveIntervalMs(4000)
    , translationContextRadius(2)
    , translationTimeoutMs(90000)
    , translationRequestedCount(0)
    , translationCompletedCount(0)
    , translationInsertedCount(0)
    , translationFailedCount(0)
    , translationSkippedConflictCount(0)
    , translationAbortedCount(0)
    , editorDocumentMargin(28)
    , editorTabStopDistance(32)
    , editorWrapEnabled(false)
    , translationSkippedExistingCount(0)
    , translationSkippedEmptyCount(0)
    , translationPendingSourceLineCount(0)
    , chapterRebuildRevision(0)
    , activeChapterRevision(0)
    , chapterRefreshPending(false)
{
    ui->setupUi(this);
    sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setCreatedAtUtc(createdAtUtc());
    setupCentralEditor();
    setupDockPanels();
    setupMenus();
    setupToolBar();
    setupStatusBarWidgets();
    applyFluentTheme();
    connectSignals();
    restorePersistentState();
    newDocument();
}

MainWindow::~MainWindow()
{
    savePersistentState();
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }

    savePersistentState();
    event->accept();
}

void MainWindow::setupCentralEditor()
{
    auto *layout = new QVBoxLayout(ui->centralwidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    editor = new AnnotatedTextEdit(ui->centralwidget);
    editor->setObjectName(QStringLiteral("editorSurface"));
    editor->setPlaceholderText(QStringLiteral("开始输入内容，或打开 TXT / DOCX / TPX 文档..."));
    applyEditorUiPreferences();
    defaultSourceAppearance = editor->sourceAppearance();
    defaultCommentAppearance = editor->commentAppearance();
    layout->addWidget(editor);

    setCentralWidget(ui->centralwidget);
}

void MainWindow::setupDockPanels()
{
    navigationDock = new QDockWidget(QStringLiteral("导航"), this);
    navigationDock->setObjectName(QStringLiteral("navigationDock"));
    navigationDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    navigationDock->setMinimumWidth(300);

    auto *navContent = new QWidget(navigationDock);
    auto *navLayout = new QVBoxLayout(navContent);
    navLayout->setContentsMargins(14, 14, 14, 14);
    navLayout->setSpacing(12);
    navLayout->setAlignment(Qt::AlignTop);

    auto *recentLabel = new QLabel(QStringLiteral("最近文件"), navContent);
    recentLabel->setObjectName(QStringLiteral("dockSectionTitle"));
    recentFilesList = new QListWidget(navContent);
    recentFilesList->setObjectName(QStringLiteral("dockList"));
    recentFilesList->setAlternatingRowColors(false);
    recentFilesList->setUniformItemSizes(true);
    recentFilesList->setMinimumHeight(120);

    auto *chapterLabel = new QLabel(QStringLiteral("章节导航"), navContent);
    chapterLabel->setObjectName(QStringLiteral("dockSectionTitle"));
    chapterList = new QListWidget(navContent);
    chapterList->setObjectName(QStringLiteral("dockList"));
    chapterList->setUniformItemSizes(true);
    chapterList->setMinimumHeight(220);

    navLayout->addWidget(recentLabel);
    navLayout->addWidget(recentFilesList);
    navLayout->addWidget(chapterLabel);
    navLayout->addWidget(chapterList);
    navLayout->addStretch();
    navigationDock->setWidget(createDockScrollArea(navContent, navigationDock));
    addDockWidget(Qt::LeftDockWidgetArea, navigationDock);

    inspectorDock = new QDockWidget(QStringLiteral("文档信息"), this);
    inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    inspectorDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    inspectorDock->setMinimumWidth(310);

    auto *inspectorContent = new QWidget(inspectorDock);
    auto *inspectorLayout = new QVBoxLayout(inspectorContent);
    inspectorLayout->setContentsMargins(14, 14, 14, 14);
    inspectorLayout->setSpacing(10);
    inspectorLayout->setAlignment(Qt::AlignTop);

    auto createInfoPair = [inspectorContent, inspectorLayout](const QString &title, QLabel **valueLabel) {
        auto *titleLabel = new QLabel(title, inspectorContent);
        titleLabel->setObjectName(QStringLiteral("dockInfoTitle"));
        auto *value = new QLabel(inspectorContent);
        value->setObjectName(QStringLiteral("dockInfoValue"));
        value->setWordWrap(true);
        inspectorLayout->addWidget(titleLabel);
        inspectorLayout->addWidget(value);
        *valueLabel = value;
    };

    createInfoPair(QStringLiteral("名称"), &fileNameValueLabel);
    createInfoPair(QStringLiteral("路径"), &filePathValueLabel);
    createInfoPair(QStringLiteral("格式"), &formatValueLabel);
    createInfoPair(QStringLiteral("字符数"), &charValueLabel);
    createInfoPair(QStringLiteral("行数"), &lineValueLabel);
    createInfoPair(QStringLiteral("上次保存"), &savedAtValueLabel);
    createInfoPair(QStringLiteral("自动保存"), &autosaveValueLabel);

    autosaveCheckBox = new QCheckBox(QStringLiteral("启用自动保存"), inspectorContent);
    autosaveCheckBox->setChecked(true);
    inspectorLayout->addSpacing(4);
    inspectorLayout->addWidget(autosaveCheckBox);
    inspectorLayout->addStretch();

    inspectorDock->setWidget(createDockScrollArea(inspectorContent, inspectorDock));
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock);

    findDock = new QDockWidget(QStringLiteral("查找与替换"), this);
    findDock->setObjectName(QStringLiteral("findDock"));
    findDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    findDock->setMinimumHeight(158);

    auto *findContent = new QWidget(findDock);
    auto *findLayout = new QGridLayout(findContent);
    findLayout->setContentsMargins(14, 12, 14, 12);
    findLayout->setHorizontalSpacing(10);
    findLayout->setVerticalSpacing(10);

    auto *findLabel = new QLabel(QStringLiteral("查找"), findContent);
    auto *replaceLabel = new QLabel(QStringLiteral("替换"), findContent);
    findLineEdit = new QLineEdit(findContent);
    replaceLineEdit = new QLineEdit(findContent);
    auto *findPrevButton = new QPushButton(QStringLiteral("上一个"), findContent);
    auto *findNextButton = new QPushButton(QStringLiteral("下一个"), findContent);
    auto *replaceButton = new QPushButton(QStringLiteral("替换当前"), findContent);
    auto *replaceAllButton = new QPushButton(QStringLiteral("全部替换"), findContent);

    findLayout->addWidget(findLabel, 0, 0);
    findLayout->addWidget(findLineEdit, 0, 1, 1, 3);
    findLayout->addWidget(findPrevButton, 0, 4);
    findLayout->addWidget(findNextButton, 0, 5);
    findLayout->addWidget(replaceLabel, 1, 0);
    findLayout->addWidget(replaceLineEdit, 1, 1, 1, 3);
    findLayout->addWidget(replaceButton, 1, 4);
    findLayout->addWidget(replaceAllButton, 1, 5);

    findDock->setWidget(findContent);
    addDockWidget(Qt::BottomDockWidgetArea, findDock);
    findDock->hide();

    commentDock = new QDockWidget(QStringLiteral("注释管理"), this);
    commentDock->setObjectName(QStringLiteral("commentDock"));
    commentDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    commentDock->setMinimumWidth(360);

    auto *commentContent = new QWidget(commentDock);
    auto *commentLayout = new QVBoxLayout(commentContent);
    commentLayout->setContentsMargins(14, 14, 14, 14);
    commentLayout->setSpacing(10);
    commentLayout->setAlignment(Qt::AlignTop);

    auto *commentHintLabel = new QLabel(QStringLiteral("这里集中管理已有注释。支持多选后批量折叠或删除。"), commentContent);
    commentHintLabel->setObjectName(QStringLiteral("dockInfoTitle"));
    commentHintLabel->setWordWrap(true);
    commentLineList = new QListWidget(commentContent);
    commentLineList->setObjectName(QStringLiteral("dockList"));
    commentLineList->setUniformItemSizes(true);
    commentLineList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    commentLineList->setMinimumHeight(260);

    auto *commentActionLayout = new QGridLayout;
    commentActionLayout->setHorizontalSpacing(10);
    commentActionLayout->setVerticalSpacing(10);
    auto *insertButton = new QPushButton(QStringLiteral("给当前行插入注释"), commentContent);
    auto *batchInsertButton = new QPushButton(QStringLiteral("按范围批量添加"), commentContent);
    auto *toggleButton = new QPushButton(QStringLiteral("切换所选注释"), commentContent);
    auto *deleteButton = new QPushButton(QStringLiteral("删除所选注释"), commentContent);
    auto *toggleSelectedButton = new QPushButton(QStringLiteral("批量折叠 / 展开"), commentContent);
    auto *deleteSelectedButton = new QPushButton(QStringLiteral("批量删除"), commentContent);
    insertButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    batchInsertButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    deleteButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    toggleSelectedButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    deleteSelectedButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    commentActionLayout->addWidget(insertButton, 0, 0);
    commentActionLayout->addWidget(batchInsertButton, 0, 1);
    commentActionLayout->addWidget(toggleButton, 1, 0);
    commentActionLayout->addWidget(deleteButton, 1, 1);
    commentActionLayout->addWidget(toggleSelectedButton, 2, 0);
    commentActionLayout->addWidget(deleteSelectedButton, 2, 1);

    commentLayout->addWidget(commentHintLabel);
    commentLayout->addWidget(commentLineList, 1);
    commentLayout->addLayout(commentActionLayout);
    commentLayout->addStretch();
    commentDock->setWidget(createDockScrollArea(commentContent, commentDock));
    addDockWidget(Qt::RightDockWidgetArea, commentDock);

    translationDock = new QDockWidget(QStringLiteral("翻译工具"), this);
    translationDock->setObjectName(QStringLiteral("translationDock"));
    translationDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    translationDock->setMinimumWidth(360);

    auto *translationContent = new QWidget(translationDock);
    auto *translationLayout = new QVBoxLayout(translationContent);
    translationLayout->setContentsMargins(14, 14, 14, 14);
    translationLayout->setSpacing(12);
    translationLayout->setAlignment(Qt::AlignTop);

    auto *translationHintLabel = new QLabel(QStringLiteral("翻译会写入为批注。支持当前行/所选行的上下文翻译，也支持按全文、当前章节或未批注章节批量翻译。"), translationContent);
    translationHintLabel->setObjectName(QStringLiteral("dockInfoTitle"));
    translationHintLabel->setWordWrap(true);

    auto *translateLinesButton = new QPushButton(QStringLiteral("翻译当前行 / 所选行"), translationContent);
    auto *translateDocumentButton = new QPushButton(QStringLiteral("翻译整篇范围..."), translationContent);
    auto *translationProgressRow = new QWidget(translationContent);
    auto *translationProgressRowLayout = new QHBoxLayout(translationProgressRow);
    translationProgressRowLayout->setContentsMargins(0, 0, 0, 0);
    translationProgressRowLayout->setSpacing(8);
    translationProgressBar = new QProgressBar(translationProgressRow);
    translationProgressBar->setTextVisible(true);
    translationProgressBar->setMinimumHeight(20);
    translationProgressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    translationProgressTimeLabel = new QLabel(translationProgressRow);
    translationProgressTimeLabel->setObjectName(QStringLiteral("dockInfoValue"));
    translationProgressTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    translationProgressTimeLabel->setMinimumWidth(110);
    translationCancelButton = new QPushButton(QStringLiteral("取消"), translationProgressRow);
    translationCancelButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    translationProgressRowLayout->addWidget(translationProgressBar, 1);
    translationProgressRowLayout->addWidget(translationProgressTimeLabel);
    translationProgressRowLayout->addWidget(translationCancelButton);
    const QKeySequence translateLinesDefaultShortcut(QStringLiteral("Ctrl+Alt+T"));
    const QKeySequence translateDocumentDefaultShortcut(QStringLiteral("Ctrl+Alt+Shift+T"));
    auto *translationShortcutLabel = new QLabel(
        QStringLiteral("快捷键：%1 / %2")
            .arg(translateLinesDefaultShortcut.toString(QKeySequence::NativeText))
            .arg(translateDocumentDefaultShortcut.toString(QKeySequence::NativeText)),
        translationContent);
    translationShortcutLabel->setObjectName(QStringLiteral("dockInfoValue"));
    translationShortcutLabel->setWordWrap(true);

    translationLayout->addWidget(translationHintLabel);
    translationLayout->addWidget(translateLinesButton);
    translationLayout->addWidget(translateDocumentButton);
    translationLayout->addWidget(translationProgressRow);
    translationLayout->addWidget(translationShortcutLabel);
    translationLayout->addStretch();
    refreshTranslationProgressUi(false);

    translationDock->setWidget(createDockScrollArea(translationContent, translationDock));
    addDockWidget(Qt::RightDockWidgetArea, translationDock);

    tabifyDockWidget(navigationDock, inspectorDock);
    tabifyDockWidget(inspectorDock, commentDock);
    tabifyDockWidget(commentDock, translationDock);
    navigationDock->raise();

    connect(findPrevButton, &QPushButton::clicked, this, [this]() { findText(false); });
    connect(findNextButton, &QPushButton::clicked, this, [this]() { findText(true); });
    connect(replaceButton, &QPushButton::clicked, this, &MainWindow::replaceCurrentSelection);
    connect(replaceAllButton, &QPushButton::clicked, this, &MainWindow::replaceAllMatches);
    connect(insertButton, &QPushButton::clicked, this, &MainWindow::insertCommentForManagedLine);
    connect(batchInsertButton, &QPushButton::clicked, this, &MainWindow::batchInsertCommentsByRange);
    connect(toggleButton, &QPushButton::clicked, this, &MainWindow::toggleCommentForManagedLine);
    connect(deleteButton, &QPushButton::clicked, this, &MainWindow::deleteCommentForManagedLine);
    connect(toggleSelectedButton, &QPushButton::clicked, this, &MainWindow::toggleSelectedComments);
    connect(deleteSelectedButton, &QPushButton::clicked, this, &MainWindow::deleteSelectedComments);
    connect(translateLinesButton, &QPushButton::clicked, this, &MainWindow::translateCurrentOrSelectedLines);
    connect(translateDocumentButton, &QPushButton::clicked, this, &MainWindow::translateWholeDocument);
    connect(translationCancelButton, &QPushButton::clicked, this, [this]() {
        if (!translationWatcher->isRunning() || !translationCancelFlag) {
            return;
        }
        translationCancelFlag->store(true);
        if (translationCancelButton) {
            translationCancelButton->setEnabled(false);
            translationCancelButton->setText(QStringLiteral("正在取消..."));
        }
        statusBar()->showMessage(QStringLiteral("正在取消翻译任务..."), 0);
    });
}

void MainWindow::setupMenus()
{
    newAction = new QAction(QStringLiteral("新建"), this);
    openAction = new QAction(QStringLiteral("打开..."), this);
    saveAction = new QAction(QStringLiteral("保存"), this);
    saveAsAction = new QAction(QStringLiteral("另存为..."), this);
    exportTxtAction = new QAction(QStringLiteral("导出 TXT..."), this);
    exportDocxAction = new QAction(QStringLiteral("导出 DOCX..."), this);
    exportTrxAction = new QAction(QStringLiteral("导出 TPX..."), this);
    autosaveNowAction = new QAction(QStringLiteral("立即自动保存"), this);
    exitAction = new QAction(QStringLiteral("退出"), this);
    undoAction = new QAction(QStringLiteral("撤销"), this);
    redoAction = new QAction(QStringLiteral("重做"), this);
    cutAction = new QAction(QStringLiteral("剪切"), this);
    copyAction = new QAction(QStringLiteral("复制"), this);
    pasteAction = new QAction(QStringLiteral("粘贴"), this);
    selectAllAction = new QAction(QStringLiteral("全选"), this);
    showFindAction = new QAction(QStringLiteral("查找 / 替换"), this);
    findNextAction = new QAction(QStringLiteral("查找下一个"), this);
    findPreviousAction = new QAction(QStringLiteral("查找上一个"), this);
    replaceAction = new QAction(QStringLiteral("替换当前"), this);
    replaceAllAction = new QAction(QStringLiteral("全部替换"), this);
    focusModeAction = new QAction(QStringLiteral("专注模式"), this);
    insertCommentAction = new QAction(QStringLiteral("插入注释行"), this);
    batchInsertCommentsAction = new QAction(QStringLiteral("按范围批量添加批注..."), this);
    translateLinesAction = new QAction(QStringLiteral("翻译当前行或所选行并添加批注"), this);
    translateDocumentAction = new QAction(QStringLiteral("翻译全文并添加批注"), this);
    deleteCommentAction = new QAction(QStringLiteral("删除注释行"), this);
    toggleCommentAction = new QAction(QStringLiteral("折叠 / 展开注释"), this);
    deleteSelectedCommentsAction = new QAction(QStringLiteral("批量删除所选注释"), this);
    toggleSelectedCommentsAction = new QAction(QStringLiteral("批量折叠 / 展开所选注释"), this);
    previousCommentAction = new QAction(QStringLiteral("上一条注释"), this);
    nextCommentAction = new QAction(QStringLiteral("下一条注释"), this);
    sourceAppearanceAction = new QAction(QStringLiteral("设置原文字体"), this);
    commentAppearanceAction = new QAction(QStringLiteral("设置注释字体"), this);
    settingsAction = new QAction(QStringLiteral("设置..."), this);

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    undoAction->setShortcut(QKeySequence::Undo);
    redoAction->setShortcut(QKeySequence::Redo);
    cutAction->setShortcut(QKeySequence::Cut);
    copyAction->setShortcut(QKeySequence::Copy);
    pasteAction->setShortcut(QKeySequence::Paste);
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    showFindAction->setShortcut(QKeySequence::Find);
    findNextAction->setShortcut(QKeySequence::FindNext);
    findPreviousAction->setShortcut(QKeySequence::FindPrevious);
    replaceAction->setShortcut(QKeySequence::Replace);
    insertCommentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+M")));
    batchInsertCommentsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+M")));
    translateLinesAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+T")));
    translateDocumentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Shift+T")));
    toggleCommentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+/")));
    deleteCommentAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Alt+Delete")));
    previousCommentAction->setShortcut(QKeySequence(QStringLiteral("Alt+Shift+Up")));
    nextCommentAction->setShortcut(QKeySequence(QStringLiteral("Alt+Shift+Down")));
    focusModeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
    settingsAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+,")));
    focusModeAction->setCheckable(true);

    auto *fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    fileMenu->addAction(newAction);
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addAction(saveAsAction);
    recentFilesMenu = fileMenu->addMenu(QStringLiteral("最近文件"));
    fileMenu->addSeparator();
    fileMenu->addAction(exportTxtAction);
    fileMenu->addAction(exportDocxAction);
    fileMenu->addAction(exportTrxAction);
    fileMenu->addSeparator();
    fileMenu->addAction(autosaveNowAction);
    fileMenu->addSeparator();
    fileMenu->addAction(settingsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);

    auto *editMenu = menuBar()->addMenu(QStringLiteral("编辑"));
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(cutAction);
    editMenu->addAction(copyAction);
    editMenu->addAction(pasteAction);
    editMenu->addAction(selectAllAction);
    editMenu->addSeparator();
    editMenu->addAction(showFindAction);
    editMenu->addAction(findNextAction);
    editMenu->addAction(findPreviousAction);
    editMenu->addAction(replaceAction);
    editMenu->addAction(replaceAllAction);
    editMenu->addSeparator();
    editMenu->addAction(insertCommentAction);
    editMenu->addAction(batchInsertCommentsAction);
    editMenu->addAction(translateLinesAction);
    editMenu->addAction(translateDocumentAction);
    editMenu->addAction(toggleCommentAction);
    editMenu->addAction(deleteCommentAction);
    editMenu->addAction(previousCommentAction);
    editMenu->addAction(nextCommentAction);
    editMenu->addAction(toggleSelectedCommentsAction);
    editMenu->addAction(deleteSelectedCommentsAction);

    auto *viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(navigationDock->toggleViewAction());
    viewMenu->addAction(inspectorDock->toggleViewAction());
    viewMenu->addAction(findDock->toggleViewAction());
    viewMenu->addAction(commentDock->toggleViewAction());
    viewMenu->addAction(translationDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(focusModeAction);

    auto *formatMenu = menuBar()->addMenu(QStringLiteral("样式"));
    formatMenu->addAction(sourceAppearanceAction);
    formatMenu->addAction(commentAppearanceAction);

    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("设置"));
    settingsMenu->addAction(settingsAction);

    recentFileActions.clear();
    for (int i = 0; i < 8; ++i) {
        auto *action = new QAction(this);
        action->setVisible(false);
        recentFilesMenu->addAction(action);
        recentFileActions.append(action);
        connect(action, &QAction::triggered, this, [this, action]() {
            openRecentFile(action->data().toString());
        });
    }
}

void MainWindow::setupToolBar()
{
    mainToolBar = addToolBar(QStringLiteral("MainToolbar"));
    mainToolBar->setObjectName(QStringLiteral("mainToolBar"));
    mainToolBar->setMovable(false);
    mainToolBar->setFloatable(false);
    mainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    mainToolBar->addAction(newAction);
    mainToolBar->addAction(openAction);
    mainToolBar->addAction(saveAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(showFindAction);
    mainToolBar->addAction(insertCommentAction);
    mainToolBar->addAction(batchInsertCommentsAction);
    mainToolBar->addAction(toggleCommentAction);
    mainToolBar->addAction(nextCommentAction);
    mainToolBar->addAction(focusModeAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(navigationDock->toggleViewAction());
    mainToolBar->addAction(inspectorDock->toggleViewAction());
    mainToolBar->addAction(commentDock->toggleViewAction());
    mainToolBar->addAction(translationDock->toggleViewAction());
}

void MainWindow::setupStatusBarWidgets()
{
    statusSummaryLabel = new QLabel(this);
    statusDetailLabel = new QLabel(this);
    statusSummaryLabel->setObjectName(QStringLiteral("statusSummaryLabel"));
    statusDetailLabel->setObjectName(QStringLiteral("statusDetailLabel"));
    statusBar()->addWidget(statusSummaryLabel, 1);
    statusBar()->addPermanentWidget(statusDetailLabel);
}

void MainWindow::applyFluentTheme()
{
    resize(1480, 920);
    setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks | QMainWindow::AnimatedDocks | QMainWindow::GroupedDragging);

    qApp->setStyle(QStringLiteral("Fusion"));
    qApp->setStyleSheet(QStringLiteral(
        "QMainWindow { background: #f6f8fb; }"
        "QMenuBar { background: rgba(248,250,252,0.96); border: none; padding: 6px 10px; color: #16202a; }"
        "QMenuBar::item { padding: 8px 12px; border-radius: 8px; }"
        "QMenuBar::item:selected { background: rgba(0,120,212,0.12); }"
        "QMenu { background: #ffffff; border: 1px solid #d9e2ec; padding: 8px; border-radius: 10px; }"
        "QMenu::item { padding: 8px 18px; border-radius: 8px; }"
        "QMenu::item:selected { background: rgba(0,120,212,0.12); }"
        "QToolBar#mainToolBar { background: rgba(248,250,252,0.96); border: none; spacing: 6px; padding: 6px 10px; }"
        "QToolBar#mainToolBar QToolButton { background: transparent; border: 1px solid transparent; border-radius: 10px; padding: 8px 12px; color: #1e2933; }"
        "QToolBar#mainToolBar QToolButton:hover { background: rgba(0,120,212,0.10); border-color: rgba(0,120,212,0.14); }"
        "QToolBar#mainToolBar QToolButton:checked { background: rgba(0,120,212,0.16); border-color: rgba(0,120,212,0.22); }"
        "QToolBar#mainToolBar QToolButton:pressed { background: rgba(15,108,189,0.14); }"
        "QStatusBar { background: rgba(248,250,252,0.98); border-top: 1px solid #d9e2ec; color: #51606f; }"
        "QDockWidget { color: #192734; font: 600 13px 'Segoe UI'; }"
        "QDockWidget::title { background: rgba(255,255,255,0.82); border: 1px solid #d9e2ec; border-bottom: none; padding: 10px 14px; text-align: left; }"
        "QDockWidget > QWidget { background: #ffffff; border: 1px solid #d9e2ec; border-top: none; }"
        "QScrollArea#dockScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 12px; margin: 8px 2px; }"
        "QScrollBar::handle:vertical { background: rgba(100,116,139,0.35); border-radius: 6px; min-height: 28px; }"
        "QScrollBar::handle:vertical:hover { background: rgba(15,108,189,0.45); }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; border: none; }"
        "QLabel#dockSectionTitle { color: #0f6cbd; font: 700 14px 'Segoe UI'; padding-top: 4px; }"
        "QLabel#dockInfoTitle { color: #64748b; font: 11px 'Segoe UI'; margin-top: 4px; }"
        "QLabel#dockInfoValue { color: #172432; font: 600 13px 'Segoe UI'; padding-bottom: 4px; }"
        "QListWidget#dockList { background: #fbfdff; border: 1px solid #dde7f0; border-radius: 12px; padding: 6px; outline: none; }"
        "QListWidget#dockList::item { padding: 9px 10px; border-radius: 9px; }"
        "QListWidget#dockList::item:selected { background: rgba(0,120,212,0.14); color: #0f1720; }"
        "QListWidget#dockList::item:hover { background: rgba(15,108,189,0.08); }"
        "QLineEdit { background: #fbfdff; border: 1px solid #d6e0ea; border-radius: 10px; padding: 8px 10px; color: #172432; }"
        "QLineEdit:focus { border-color: #0f6cbd; }"
        "QKeySequenceEdit { background: #fbfdff; border: 1px solid #d6e0ea; border-radius: 10px; padding: 8px 10px; color: #172432; }"
        "QKeySequenceEdit:focus { border-color: #0f6cbd; }"
        "QSpinBox { background: #fbfdff; border: 1px solid #d6e0ea; border-radius: 10px; padding: 6px 10px; color: #172432; }"
        "QSpinBox:focus { border-color: #0f6cbd; }"
        "QPushButton { background: #f8fafc; border: 1px solid #d6e0ea; border-radius: 10px; padding: 8px 12px; color: #172432; }"
        "QPushButton:hover { background: #eef5fb; border-color: #b6d3ef; }"
        "QPushButton#settingsColorButton { text-align: left; padding-left: 12px; }"
        "QCheckBox { color: #172432; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #b7c5d3; background: #ffffff; }"
        "QCheckBox::indicator:checked { background: #0f6cbd; border-color: #0f6cbd; }"
        "QGroupBox { border: 1px solid #d9e2ec; border-radius: 14px; margin-top: 14px; padding: 12px; background: rgba(255,255,255,0.84); }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #0f172a; }"
        "QPlainTextEdit#editorSurface { background: #ffffff; border: none; color: #172432; selection-background-color: rgba(0,120,212,0.20); font: 16px 'Cascadia Mono'; }"
        "QWidget#lineNumberArea { background: #f4f7fb; border-right: 1px solid #d7e0ea; }"
        "QPlainTextEdit#editorSurface[focusMode='true'] { background: #fffdf8; }"
        "QTabWidget::pane { border: 1px solid #d9e2ec; border-radius: 14px; background: rgba(255,255,255,0.92); top: -1px; }"
        "QTabBar::tab { background: rgba(255,255,255,0.8); border: 1px solid #d9e2ec; padding: 8px 12px; margin-right: 4px; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
        "QTabBar::tab:selected { background: #ffffff; border-bottom-color: #ffffff; }"
    ));
}

void MainWindow::connectSignals()
{
    autosaveTimer->setSingleShot(true);
    autosaveTimer->setInterval(autosaveIntervalMs);
    statsRefreshTimer->setSingleShot(true);
    statsRefreshTimer->setInterval(160);
    chapterRefreshTimer->setSingleShot(true);
    chapterRefreshTimer->setInterval(900);

    connect(newAction, &QAction::triggered, this, &MainWindow::newDocument);
    connect(openAction, &QAction::triggered, this, &MainWindow::openDocument);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveDocument);
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveDocumentAs);
    connect(exportTxtAction, &QAction::triggered, this, [this]() { exportDocument(DocumentFormat::Txt); });
    connect(exportDocxAction, &QAction::triggered, this, [this]() { exportDocument(DocumentFormat::Docx); });
    connect(exportTrxAction, &QAction::triggered, this, [this]() { exportDocument(DocumentFormat::Trx); });
    connect(autosaveNowAction, &QAction::triggered, this, &MainWindow::performAutosave);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    connect(undoAction, &QAction::triggered, this, &MainWindow::triggerUndo);
    connect(redoAction, &QAction::triggered, this, &MainWindow::triggerRedo);
    connect(cutAction, &QAction::triggered, editor, &AnnotatedTextEdit::cut);
    connect(copyAction, &QAction::triggered, editor, &AnnotatedTextEdit::copy);
    connect(pasteAction, &QAction::triggered, editor, &AnnotatedTextEdit::paste);
    connect(selectAllAction, &QAction::triggered, editor, &AnnotatedTextEdit::selectAll);
    connect(showFindAction, &QAction::triggered, this, &MainWindow::showFindPanel);
    connect(findNextAction, &QAction::triggered, this, [this]() { findText(true); });
    connect(findPreviousAction, &QAction::triggered, this, [this]() { findText(false); });
    connect(replaceAction, &QAction::triggered, this, &MainWindow::replaceCurrentSelection);
    connect(replaceAllAction, &QAction::triggered, this, &MainWindow::replaceAllMatches);
    connect(focusModeAction, &QAction::toggled, this, &MainWindow::updateFocusMode);
    connect(insertCommentAction, &QAction::triggered, this, &MainWindow::insertCommentForCurrentLine);
    connect(batchInsertCommentsAction, &QAction::triggered, this, &MainWindow::batchInsertCommentsByRange);
    connect(translateLinesAction, &QAction::triggered, this, &MainWindow::translateCurrentOrSelectedLines);
    connect(translateDocumentAction, &QAction::triggered, this, &MainWindow::translateWholeDocument);
    connect(deleteCommentAction, &QAction::triggered, this, &MainWindow::deleteCommentForCurrentLine);
    connect(toggleCommentAction, &QAction::triggered, this, &MainWindow::toggleCommentForCurrentLine);
    connect(deleteSelectedCommentsAction, &QAction::triggered, this, &MainWindow::deleteSelectedComments);
    connect(toggleSelectedCommentsAction, &QAction::triggered, this, &MainWindow::toggleSelectedComments);
    connect(previousCommentAction, &QAction::triggered, this, &MainWindow::goToPreviousComment);
    connect(nextCommentAction, &QAction::triggered, this, &MainWindow::goToNextComment);
    connect(sourceAppearanceAction, &QAction::triggered, this, &MainWindow::chooseSourceAppearance);
    connect(commentAppearanceAction, &QAction::triggered, this, &MainWindow::chooseCommentAppearance);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);

    connect(editor, &AnnotatedTextEdit::undoAvailable, undoAction, &QAction::setEnabled);
    connect(editor, &AnnotatedTextEdit::redoAvailable, redoAction, &QAction::setEnabled);

    connect(editor->document(), &QTextDocument::contentsChanged, this, [this]() {
        if (suppressDocumentRefresh) {
            return;
        }

        const bool commentEdit = editor->currentBlockIsComment();
        scheduleDocumentStatsRefresh();
        if (!commentEdit) {
            scheduleChapterRefresh();
        }
        scheduleAutosave();
    });
    connect(editor->document(), &QTextDocument::modificationChanged, this, [this](bool) {
        refreshWindowTitle();
        scheduleDocumentStatsRefresh();
    });

    connect(statsRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshDocumentStats);
    connect(chapterRefreshTimer, &QTimer::timeout, this, &MainWindow::startChapterRebuildAsync);
    connect(autosaveTimer, &QTimer::timeout, this, &MainWindow::performAutosave);
    translationUiFlushTimer->setSingleShot(true);
    translationUiFlushTimer->setInterval(16);
    connect(translationUiFlushTimer, &QTimer::timeout, this, &MainWindow::flushPendingTranslationUpdates);
    translationElapsedUiTimer->setInterval(1000);
    connect(translationElapsedUiTimer, &QTimer::timeout, this, [this]() {
        refreshTranslationProgressUi(true);
    });
    connect(editor, &AnnotatedTextEdit::commentsChanged, this, [this]() {
        if (translationWatcher && translationWatcher->isRunning()) {
            commentManagerDirty = true;
            return;
        }
        if (commentDock && !commentDock->isVisible()) {
            commentManagerDirty = true;
            return;
        }
        refreshCommentManager();
    });
    connect(chapterIndexWatcher, &QFutureWatcher<QVector<ChapterEntry>>::finished, this, [this]() {
        const QVector<ChapterEntry> entries = chapterIndexWatcher->result();
        const quint64 finishedRevision = activeChapterRevision;
        if (finishedRevision == chapterRebuildRevision) {
            applyChapterEntries(entries);
        }

        if (chapterRefreshPending || finishedRevision != chapterRebuildRevision) {
            chapterRefreshPending = false;
            startChapterRebuildAsync();
        }
    });
    connect(translationWatcher, &QFutureWatcher<TranslationTaskResult>::finished, this, [this]() {
        const TranslationTaskResult result = translationWatcher->result();

        translateLinesAction->setEnabled(true);
        translateDocumentAction->setEnabled(true);
        translationCancelFlag.reset();
        flushPendingTranslationUpdates();
        translationUiFlushTimer->stop();
        translationElapsedUiTimer->stop();
        {
            QMutexLocker locker(&translationUiUpdateMutex);
            translationUiFlushScheduled = false;
        }

        if (result.canceled) {
            if (translationInsertedCount > 0) {
                statusBar()->showMessage(QStringLiteral("翻译已取消，已先显示 %1/%2 行结果").arg(translationInsertedCount).arg(translationRequestedCount), 4000);
            } else {
                statusBar()->showMessage(QStringLiteral("翻译已取消"), 2500);
            }
            translationProgressTitle.clear();
            translationSuccessLabelTemplate.clear();
            translationSkippedExistingCount = 0;
            translationSkippedEmptyCount = 0;
            translationRequestedCount = 0;
            translationCompletedCount = 0;
            translationInsertedCount = 0;
            translationFailedCount = 0;
            translationSkippedConflictCount = 0;
            translationAbortedCount = 0;
            {
                QMutexLocker locker(&translationUiUpdateMutex);
                translationPendingUiUpdates.clear();
            }
            translationPendingSourceTexts.clear();
            translationPendingSourceLineCount = 0;
            if (commentManagerDirty) {
                commentManagerDirty = false;
                refreshCommentManager();
            }
            refreshTranslationProgressUi(false);
            return;
        }

        if (result.timedOut) {
            const int abortedCount = markPendingTranslationsAborted();
            translationAbortedCount = abortedCount;
            QStringList parts;
            parts.append(translationSuccessLabelTemplate.arg(translationInsertedCount));
            if (abortedCount > 0) {
                parts.append(QStringLiteral("已中止 %1 行").arg(abortedCount));
            }
            if (translationSkippedExistingCount > 0) {
                parts.append(QStringLiteral("跳过已有批注 %1 行").arg(translationSkippedExistingCount));
            }
            if (translationSkippedEmptyCount > 0) {
                parts.append(QStringLiteral("跳过空行 %1 行").arg(translationSkippedEmptyCount));
            }
            showError(QStringLiteral("翻译超时"), result.errorMessage.isEmpty() ? QStringLiteral("翻译总耗时已超时。") : result.errorMessage);
            statusBar()->showMessage(parts.join(QStringLiteral("，")), 6000);
            translationProgressTitle.clear();
            translationSuccessLabelTemplate.clear();
            translationSkippedExistingCount = 0;
            translationSkippedEmptyCount = 0;
            translationRequestedCount = 0;
            translationCompletedCount = 0;
            translationInsertedCount = 0;
            translationFailedCount = 0;
            translationSkippedConflictCount = 0;
            translationAbortedCount = 0;
            {
                QMutexLocker locker(&translationUiUpdateMutex);
                translationPendingUiUpdates.clear();
            }
            translationPendingSourceTexts.clear();
            translationPendingSourceLineCount = 0;
            if (commentManagerDirty) {
                commentManagerDirty = false;
                refreshCommentManager();
            }
            refreshTranslationProgressUi(false);
            return;
        }

        if (translationInsertedCount <= 0 && translationFailedCount > 0 && translationSkippedConflictCount <= 0) {
            showError(QStringLiteral("翻译失败"), result.errorMessage.isEmpty() ? QStringLiteral("未能获取翻译结果。") : result.errorMessage);
            statusBar()->showMessage(QStringLiteral("翻译失败"), 4000);
            translationProgressTitle.clear();
            translationSuccessLabelTemplate.clear();
            translationSkippedExistingCount = 0;
            translationSkippedEmptyCount = 0;
            translationRequestedCount = 0;
            translationCompletedCount = 0;
            translationInsertedCount = 0;
            translationFailedCount = 0;
            translationSkippedConflictCount = 0;
            translationAbortedCount = 0;
            {
                QMutexLocker locker(&translationUiUpdateMutex);
                translationPendingUiUpdates.clear();
            }
            translationPendingSourceTexts.clear();
            translationPendingSourceLineCount = 0;
            if (commentManagerDirty) {
                commentManagerDirty = false;
                refreshCommentManager();
            }
            refreshTranslationProgressUi(false);
            return;
        }

        if (translationInsertedCount <= 0 && translationSkippedConflictCount > 0) {
            showError(QStringLiteral("翻译结果未插入"), QStringLiteral("翻译已完成，但目标行在翻译期间发生修改或已经有批注，结果未自动插入。请重新发起翻译。"));
            statusBar()->showMessage(QStringLiteral("翻译结果未插入"), 4000);
            translationProgressTitle.clear();
            translationSuccessLabelTemplate.clear();
            translationSkippedExistingCount = 0;
            translationSkippedEmptyCount = 0;
            translationRequestedCount = 0;
            translationCompletedCount = 0;
            translationInsertedCount = 0;
            translationFailedCount = 0;
            translationSkippedConflictCount = 0;
            translationAbortedCount = 0;
            {
                QMutexLocker locker(&translationUiUpdateMutex);
                translationPendingUiUpdates.clear();
            }
            translationPendingSourceTexts.clear();
            translationPendingSourceLineCount = 0;
            if (commentManagerDirty) {
                commentManagerDirty = false;
                refreshCommentManager();
            }
            refreshTranslationProgressUi(false);
            return;
        }

        QStringList parts;
        parts.append(translationSuccessLabelTemplate.arg(translationInsertedCount));
        if (translationSkippedExistingCount > 0) {
            parts.append(QStringLiteral("跳过已有批注 %1 行").arg(translationSkippedExistingCount));
        }
        if (translationSkippedEmptyCount > 0) {
            parts.append(QStringLiteral("跳过空行 %1 行").arg(translationSkippedEmptyCount));
        }
        if (translationFailedCount > 0) {
            parts.append(QStringLiteral("翻译失败 %1 行").arg(translationFailedCount));
        }
        if (translationSkippedConflictCount > 0) {
            parts.append(QStringLiteral("跳过已变更/已有批注 %1 行").arg(translationSkippedConflictCount));
        }
        if (translationAbortedCount > 0) {
            parts.append(QStringLiteral("已中止 %1 行").arg(translationAbortedCount));
        }
        statusBar()->showMessage(parts.join(QStringLiteral("，")), 5000);

        translationProgressTitle.clear();
        translationSuccessLabelTemplate.clear();
        translationSkippedExistingCount = 0;
        translationSkippedEmptyCount = 0;
        translationRequestedCount = 0;
        translationCompletedCount = 0;
        translationInsertedCount = 0;
        translationFailedCount = 0;
        translationSkippedConflictCount = 0;
        translationAbortedCount = 0;
        {
            QMutexLocker locker(&translationUiUpdateMutex);
            translationPendingUiUpdates.clear();
        }
        translationPendingSourceTexts.clear();
        translationPendingSourceLineCount = 0;
        if (commentManagerDirty) {
            commentManagerDirty = false;
            refreshCommentManager();
        }
        refreshTranslationProgressUi(false);
    });

    connect(autosaveCheckBox, &QCheckBox::toggled, this, &MainWindow::setAutosaveEnabled);
    connect(recentFilesList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openRecentFile(item->data(Qt::UserRole).toString());
    });
    connect(chapterList, &QListWidget::currentRowChanged, this, &MainWindow::jumpToChapterRow);
    connect(commentLineList, &QListWidget::currentRowChanged, this, &MainWindow::jumpToManagedLineRow);
    connect(findLineEdit, &QLineEdit::returnPressed, this, [this]() { findText(true); });
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        if (!commentLineList || commentLineList->hasFocus()) {
            return;
        }

        if (commentLineList->selectedItems().size() > 1) {
            return;
        }

        const int lineNumber = editor->currentLineNumber();
        syncCommentManagerSelection(lineNumber, !editor->hasCommentAtLine(lineNumber));
    });

    connect(commentDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && commentManagerDirty) {
            commentManagerDirty = false;
            refreshCommentManager();
        }
    });
    connect(inspectorDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && inspectorPanelDirty) {
            inspectorPanelDirty = false;
            refreshInspectorPanel();
        }
    });
    connect(navigationDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible && chapterIndexDirty) {
            chapterIndexDirty = false;
            scheduleChapterRefresh();
        }
    });

    editor->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(editor, &QWidget::customContextMenuRequested, this, &MainWindow::showEditorContextMenu);
}

void MainWindow::restorePersistentState()
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    rememberWindowLayout = settings.value(QStringLiteral("rememberWindowLayout"), true).toBool();
    if (rememberWindowLayout) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        restoreState(settings.value(QStringLiteral("windowState")).toByteArray());
    }
    autosaveEnabled = settings.value(QStringLiteral("autosaveEnabled"), true).toBool();
    autosaveIntervalMs = qMax(1000, settings.value(QStringLiteral("autosaveIntervalMs"), autosaveIntervalMs).toInt());
    focusModeEnabled = settings.value(QStringLiteral("focusModeEnabled"), false).toBool();
    translationUseOllama = settings.value(QStringLiteral("translationUseOllama"), translationUseOllama).toBool();
    translationFallbackToOnline = settings.value(QStringLiteral("translationFallbackToOnline"), translationFallbackToOnline).toBool();
    translationDisableThinking = settings.value(QStringLiteral("translationDisableThinking"), translationDisableThinking).toBool();
    translationEnableCustomPrompt = settings.value(QStringLiteral("translationEnableCustomPrompt"), translationEnableCustomPrompt).toBool();
    translationCustomPromptTemplate = settings.value(QStringLiteral("translationCustomPromptTemplate"), translationCustomPromptTemplate).toString();
    translationCustomContextPromptTemplate = settings.value(QStringLiteral("translationCustomContextPromptTemplate"), translationCustomContextPromptTemplate).toString();
    translationLargeModelApiEndpoint = settings.value(QStringLiteral("translationLargeModelApiEndpoint"), translationLargeModelApiEndpoint).toString().trimmed();
    translationLargeModelApiKey = settings.value(QStringLiteral("translationLargeModelApiKey"), translationLargeModelApiKey).toString().trimmed();
    translationConfigPreset = settings.value(QStringLiteral("translationConfigPreset"), translationConfigPreset).toString();
    translationMaxChunkTargetLines = qMax(1, settings.value(QStringLiteral("translationMaxChunkTargetLines"), translationMaxChunkTargetLines).toInt());
    translationMaxChunkChars = qMax(1000, settings.value(QStringLiteral("translationMaxChunkChars"), translationMaxChunkChars).toInt());
    translationStrictOutputParsing = settings.value(QStringLiteral("translationStrictOutputParsing"), translationStrictOutputParsing).toBool();
    translationLocalStrategy = static_cast<LocalTranslationStrategy>(qBound(
        0,
        settings.value(QStringLiteral("translationLocalStrategy"), static_cast<int>(translationLocalStrategy)).toInt(),
        1));
    translationOllamaEndpoint = settings.value(QStringLiteral("translationOllamaEndpoint"), translationOllamaEndpoint).toString().trimmed();
    translationOllamaModel = settings.value(QStringLiteral("translationOllamaModel"), translationOllamaModel).toString().trimmed();
    translationContextRadius = qBound(0, settings.value(QStringLiteral("translationContextRadius"), translationContextRadius).toInt(), 20);
    translationTimeoutMs = qMax(0, settings.value(QStringLiteral("translationTimeoutMs"), translationTimeoutMs).toInt());
    editorDocumentMargin = qMax(0, settings.value(QStringLiteral("editorDocumentMargin"), editorDocumentMargin).toInt());
    editorTabStopDistance = qMax(16, settings.value(QStringLiteral("editorTabStopDistance"), editorTabStopDistance).toInt());
    editorWrapEnabled = settings.value(QStringLiteral("editorWrapEnabled"), editorWrapEnabled).toBool();
    defaultSourceAppearance = appearanceFromSettingsString(settings.value(QStringLiteral("defaultSourceAppearance")), defaultSourceAppearance);
    defaultCommentAppearance = appearanceFromSettingsString(settings.value(QStringLiteral("defaultCommentAppearance")), defaultCommentAppearance);
    autosaveCheckBox->setChecked(autosaveEnabled);
    focusModeAction->setChecked(focusModeEnabled);
    settings.endGroup();

    autosaveTimer->setInterval(autosaveIntervalMs);
    restoreShortcutSettings();
    applyEditorUiPreferences();
    applyConfiguredEditorDefaults();

    refreshRecentFilesUi();
    refreshRecentFileActions();
    refreshInspectorPanel();
    refreshCommentManager();
    updateFocusMode(focusModeEnabled);
}

void MainWindow::savePersistentState()
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    settings.setValue(QStringLiteral("rememberWindowLayout"), rememberWindowLayout);
    if (rememberWindowLayout) {
        settings.setValue(QStringLiteral("geometry"), saveGeometry());
        settings.setValue(QStringLiteral("windowState"), saveState());
    } else {
        settings.remove(QStringLiteral("geometry"));
        settings.remove(QStringLiteral("windowState"));
    }
    settings.setValue(QStringLiteral("autosaveEnabled"), autosaveEnabled);
    settings.setValue(QStringLiteral("autosaveIntervalMs"), autosaveIntervalMs);
    settings.setValue(QStringLiteral("focusModeEnabled"), focusModeEnabled);
    settings.setValue(QStringLiteral("translationUseOllama"), translationUseOllama);
    settings.setValue(QStringLiteral("translationFallbackToOnline"), translationFallbackToOnline);
    settings.setValue(QStringLiteral("translationDisableThinking"), translationDisableThinking);
    settings.setValue(QStringLiteral("translationEnableCustomPrompt"), translationEnableCustomPrompt);
    settings.setValue(QStringLiteral("translationCustomPromptTemplate"), translationCustomPromptTemplate);
    settings.setValue(QStringLiteral("translationCustomContextPromptTemplate"), translationCustomContextPromptTemplate);
    settings.setValue(QStringLiteral("translationLargeModelApiEndpoint"), translationLargeModelApiEndpoint);
    settings.setValue(QStringLiteral("translationLargeModelApiKey"), translationLargeModelApiKey);
    settings.setValue(QStringLiteral("translationConfigPreset"), translationConfigPreset);
    settings.setValue(QStringLiteral("translationMaxChunkTargetLines"), translationMaxChunkTargetLines);
    settings.setValue(QStringLiteral("translationMaxChunkChars"), translationMaxChunkChars);
    settings.setValue(QStringLiteral("translationStrictOutputParsing"), translationStrictOutputParsing);
    settings.setValue(QStringLiteral("translationLocalStrategy"), static_cast<int>(translationLocalStrategy));
    settings.setValue(QStringLiteral("translationOllamaEndpoint"), translationOllamaEndpoint);
    settings.setValue(QStringLiteral("translationOllamaModel"), translationOllamaModel);
    settings.setValue(QStringLiteral("translationContextRadius"), translationContextRadius);
    settings.setValue(QStringLiteral("translationTimeoutMs"), translationTimeoutMs);
    settings.setValue(QStringLiteral("editorDocumentMargin"), editorDocumentMargin);
    settings.setValue(QStringLiteral("editorTabStopDistance"), editorTabStopDistance);
    settings.setValue(QStringLiteral("editorWrapEnabled"), editorWrapEnabled);
    settings.setValue(QStringLiteral("defaultSourceAppearance"), appearanceToSettingsString(defaultSourceAppearance));
    settings.setValue(QStringLiteral("defaultCommentAppearance"), appearanceToSettingsString(defaultCommentAppearance));
    QJsonObject shortcutObject;
    for (const ShortcutBinding &binding : shortcutBindings()) {
        shortcutObject.insert(binding.id, binding.action ? binding.action->shortcut().toString(QKeySequence::PortableText) : QString());
    }
    settings.setValue(shortcutSettingsKey(), QString::fromUtf8(QJsonDocument(shortcutObject).toJson(QJsonDocument::Compact)));
    settings.endGroup();
}

void MainWindow::saveTranslationDefaults(const QString &preset,
                                         bool enableCustomPrompt,
                                         const QString &customPrompt,
                                         const QString &customContextPrompt,
                                         const QString &largeModelApiEndpoint,
                                         const QString &largeModelApiKey,
                                         int maxChunkLines,
                                         int maxChunkChars,
                                         bool strictOutput) const
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    settings.beginGroup(QStringLiteral("translationDefaults"));
    settings.setValue(QStringLiteral("translationConfigPreset"), preset);
    settings.setValue(QStringLiteral("translationEnableCustomPrompt"), enableCustomPrompt);
    settings.setValue(QStringLiteral("translationCustomPromptTemplate"), customPrompt);
    settings.setValue(QStringLiteral("translationCustomContextPromptTemplate"), customContextPrompt);
    settings.setValue(QStringLiteral("translationLargeModelApiEndpoint"), largeModelApiEndpoint);
    settings.setValue(QStringLiteral("translationLargeModelApiKey"), largeModelApiKey);
    settings.setValue(QStringLiteral("translationMaxChunkTargetLines"), maxChunkLines);
    settings.setValue(QStringLiteral("translationMaxChunkChars"), maxChunkChars);
    settings.setValue(QStringLiteral("translationStrictOutputParsing"), strictOutput);
    settings.endGroup();
    settings.endGroup();
}

bool MainWindow::loadTranslationDefaults(QString *preset,
                                         bool *enableCustomPrompt,
                                         QString *customPrompt,
                                         QString *customContextPrompt,
                                         QString *largeModelApiEndpoint,
                                         QString *largeModelApiKey,
                                         int *maxChunkLines,
                                         int *maxChunkChars,
                                         bool *strictOutput) const
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    settings.beginGroup(QStringLiteral("translationDefaults"));
    if (!settings.contains(QStringLiteral("translationConfigPreset"))) {
        settings.endGroup();
        settings.endGroup();
        return false;
    }

    if (preset) {
        *preset = settings.value(QStringLiteral("translationConfigPreset")).toString();
    }
    if (enableCustomPrompt) {
        *enableCustomPrompt = settings.value(QStringLiteral("translationEnableCustomPrompt"), false).toBool();
    }
    if (customPrompt) {
        *customPrompt = settings.value(QStringLiteral("translationCustomPromptTemplate"), QString()).toString();
    }
    if (customContextPrompt) {
        *customContextPrompt = settings.value(QStringLiteral("translationCustomContextPromptTemplate"), QString()).toString();
    }
    if (largeModelApiEndpoint) {
        *largeModelApiEndpoint = settings.value(QStringLiteral("translationLargeModelApiEndpoint"), QString()).toString().trimmed();
    }
    if (largeModelApiKey) {
        *largeModelApiKey = settings.value(QStringLiteral("translationLargeModelApiKey"), QString()).toString().trimmed();
    }
    if (maxChunkLines) {
        *maxChunkLines = settings.value(QStringLiteral("translationMaxChunkTargetLines"), translationMaxChunkTargetLines).toInt();
    }
    if (maxChunkChars) {
        *maxChunkChars = settings.value(QStringLiteral("translationMaxChunkChars"), translationMaxChunkChars).toInt();
    }
    if (strictOutput) {
        *strictOutput = settings.value(QStringLiteral("translationStrictOutputParsing"), translationStrictOutputParsing).toBool();
    }

    settings.endGroup();
    settings.endGroup();
    return true;
}

void MainWindow::refreshWindowTitle()
{
    const QString marker = editor->document()->isModified() ? QStringLiteral(" *") : QString();
    setWindowTitle(QStringLiteral("%1%2 - TPX Writer").arg(currentDisplayName(), marker));
}

void MainWindow::refreshDocumentStats()
{
    const int charCount = qMax(0, editor->document()->characterCount() - 1);
    const int lineCount = qMax(1, editor->lineCount());
    statusSummaryLabel->setText(QStringLiteral("%1 字符  ·  %2 行").arg(charCount).arg(lineCount));
    statusDetailLabel->setText(editor->document()->isModified() ? QStringLiteral("未保存") : QStringLiteral("已保存"));
    if (inspectorDock && !inspectorDock->isVisible()) {
        inspectorPanelDirty = true;
    } else {
        refreshInspectorPanel();
    }
    refreshWindowTitle();
}

void MainWindow::refreshInspectorPanel()
{
    if (inspectorDock && !inspectorDock->isVisible()) {
        inspectorPanelDirty = true;
        return;
    }

    inspectorPanelDirty = false;
    const QFileInfo info(currentFilePath);
    fileNameValueLabel->setText(currentDisplayName());
    filePathValueLabel->setText(currentFilePath.isEmpty() ? QStringLiteral("尚未保存") : QDir::toNativeSeparators(currentFilePath));
    formatValueLabel->setText(formatLabel(currentFormat));
    charValueLabel->setText(QString::number(qMax(0, editor->document()->characterCount() - 1)));
    lineValueLabel->setText(QString::number(qMax(1, editor->lineCount())));
    savedAtValueLabel->setText(lastSavedAtUtc.isEmpty() ? QStringLiteral("尚未保存") : lastSavedAtUtc);
    autosaveValueLabel->setText(autosaveStateLabel(autosaveEnabled));
}

void MainWindow::scheduleDocumentStatsRefresh()
{
    statsRefreshTimer->start();
}

void MainWindow::scheduleChapterRefresh()
{
    if (navigationDock && !navigationDock->isVisible()) {
        chapterIndexDirty = true;
        chapterRefreshTimer->stop();
        return;
    }

    ++chapterRebuildRevision;
    chapterIndexDirty = false;
    chapterRefreshTimer->start();
}

void MainWindow::refreshRecentFilesUi()
{
    if (!recentFilesList) {
        return;
    }

    recentFilesList->clear();
    const QStringList files = recentFiles();
    for (const QString &filePath : files) {
        const QFileInfo info(filePath);
        auto *item = new QListWidgetItem(info.fileName(), recentFilesList);
        item->setToolTip(QDir::toNativeSeparators(filePath));
        item->setData(Qt::UserRole, filePath);
    }
}

void MainWindow::refreshRecentFileActions()
{
    const QStringList files = recentFiles();
    for (int i = 0; i < recentFileActions.size(); ++i) {
        QAction *action = recentFileActions[i];
        if (i < files.size()) {
            const QFileInfo info(files[i]);
            action->setText(QStringLiteral("%1. %2").arg(i + 1).arg(info.fileName()));
            action->setData(files[i]);
            action->setToolTip(QDir::toNativeSeparators(files[i]));
            action->setVisible(true);
        } else {
            action->setVisible(false);
        }
    }
}

void MainWindow::refreshCommentManager()
{
    if (!commentLineList) {
        return;
    }

    if (commentDock && !commentDock->isVisible()) {
        commentManagerDirty = true;
        return;
    }

    commentManagerDirty = false;

    QList<int> preservedLines;
    const QList<QListWidgetItem *> selectedItems = commentLineList->selectedItems();
    preservedLines.reserve(selectedItems.size());
    for (QListWidgetItem *item : selectedItems) {
        preservedLines.append(item->data(Qt::UserRole).toInt());
    }
    const int preservedCurrentLine = commentLineList->currentItem()
        ? commentLineList->currentItem()->data(Qt::UserRole).toInt()
        : editor->currentLineNumber();

    commentLineList->blockSignals(true);
    commentLineList->clear();

    const QList<AnnotatedTextEdit::CommentEntry> comments = editor->commentEntries();
    for (const AnnotatedTextEdit::CommentEntry &entry : comments) {
        auto *item = new QListWidgetItem(commentLineList);
        const int lineNumber = entry.lineNumber;
        const QString sourceSummary = summarizeSourceLine(entry.sourceText.isEmpty() ? editor->lineText(lineNumber) : entry.sourceText);
        const QString label = QStringLiteral("L%1  %2  ·  %3  ·  %4")
                                  .arg(lineNumber + 1, 4)
                                  .arg(sourceSummary)
                                  .arg(entry.collapsed ? QStringLiteral("已折叠") : QStringLiteral("已展开"))
                                  .arg(summarizeHtml(entry.html));
        item->setText(label);
        item->setData(Qt::UserRole, lineNumber);
        item->setToolTip(editor->commentPlainTextAtLine(lineNumber));
    }

    for (int row = 0; row < commentLineList->count(); ++row) {
        QListWidgetItem *item = commentLineList->item(row);
        if (!item) {
            continue;
        }

        const int lineNumber = item->data(Qt::UserRole).toInt();
        if (preservedLines.contains(lineNumber)) {
            item->setSelected(true);
        }
        if (lineNumber == preservedCurrentLine) {
            commentLineList->setCurrentRow(row);
        }
    }

    commentLineList->blockSignals(false);
}

void MainWindow::rebuildChapterIndex()
{
    ++chapterRebuildRevision;
    chapterRefreshTimer->stop();
    startChapterRebuildAsync();
}

void MainWindow::startChapterRebuildAsync()
{
    if (navigationDock && !navigationDock->isVisible()) {
        chapterIndexDirty = true;
        return;
    }

    if (chapterIndexWatcher->isRunning()) {
        chapterRefreshPending = true;
        return;
    }

    activeChapterRevision = chapterRebuildRevision;
    const QStringList sourceLines = editor->sourceLines();
    chapterIndexWatcher->setFuture(QtConcurrent::run([sourceLines]() {
        return MainWindow::buildChapterEntriesFromSourceLines(sourceLines);
    }));
}

QVector<MainWindow::ChapterEntry> MainWindow::buildChapterEntriesFromSourceLines(const QStringList &sourceLines)
{
    QVector<ChapterEntry> entries;
    entries.reserve(qMin(sourceLines.size(), 256));

    const QRegularExpression chapterPattern(QStringLiteral(R"(^\s*(#{1,6}\s+.+|第.{1,20}[章节回卷篇部].*|[0-9]{1,3}[\.|、]\s*.+)$)"));
    int fallbackIndex = 1;
    for (int lineNumber = 0; lineNumber < sourceLines.size(); ++lineNumber) {
        const QString text = sourceLines.at(lineNumber).trimmed();
        if (text.isEmpty()) {
            continue;
        }
        if (chapterPattern.match(text).hasMatch()) {
            entries.append(ChapterEntry { text, lineNumber });
            continue;
        }
        if (lineNumber == 0 || (text.size() <= 24 && fallbackIndex <= 12 && !text.contains(QChar(' ')))) {
            entries.append(ChapterEntry { QStringLiteral("段落 %1 · %2").arg(fallbackIndex).arg(text), lineNumber });
            ++fallbackIndex;
        }
    }

    return entries;
}

void MainWindow::applyChapterEntries(const QVector<ChapterEntry> &entries)
{
    chapterEntries.clear();
    chapterEntries = entries;

    const int previousRow = chapterList->currentRow();
    chapterList->blockSignals(true);
    chapterList->clear();
    for (const ChapterEntry &entry : chapterEntries) {
        chapterList->addItem(entry.title);
    }
    if (!chapterEntries.isEmpty()) {
        chapterList->setCurrentRow(qBound(0, previousRow, chapterEntries.size() - 1));
    }
    chapterList->blockSignals(false);
}

void MainWindow::setCurrentFile(const QString &filePath, DocumentFormat format)
{
    currentFilePath = filePath;
    currentFormat = format;
    refreshInspectorPanel();
    refreshWindowTitle();
}

QString MainWindow::currentDisplayName() const
{
    return currentFilePath.isEmpty() ? QStringLiteral("未命名文档") : QFileInfo(currentFilePath).fileName();
}

QPair<int, int> MainWindow::chapterLineRangeForIndex(int chapterIndex) const
{
    if (editor->lineCount() <= 0) {
        return qMakePair(0, -1);
    }

    if (chapterEntries.isEmpty() || chapterIndex < 0 || chapterIndex >= chapterEntries.size()) {
        return qMakePair(0, editor->lineCount() - 1);
    }

    const int startLine = qBound(0, chapterEntries.at(chapterIndex).blockNumber, editor->lineCount() - 1);
    const int endLine = chapterIndex + 1 < chapterEntries.size()
        ? qBound(startLine, chapterEntries.at(chapterIndex + 1).blockNumber - 1, editor->lineCount() - 1)
        : editor->lineCount() - 1;
    return qMakePair(startLine, endLine);
}

int MainWindow::chapterIndexForLine(int lineNumber) const
{
    if (chapterEntries.isEmpty()) {
        return -1;
    }

    int index = 0;
    for (int candidate = 0; candidate < chapterEntries.size(); ++candidate) {
        if (chapterEntries.at(candidate).blockNumber > lineNumber) {
            break;
        }
        index = candidate;
    }
    return index;
}

QList<int> MainWindow::sourceLinesForRange(int startLine, int endLine) const
{
    QList<int> lines;
    if (startLine < 0 || endLine < startLine) {
        return lines;
    }

    lines.reserve(endLine - startLine + 1);
    for (int lineNumber = startLine; lineNumber <= endLine; ++lineNumber) {
        lines.append(lineNumber);
    }
    return lines;
}

void MainWindow::syncCommentManagerSelection(int lineNumber, bool clearWhenMissing)
{
    if (!commentLineList) {
        return;
    }

    bool found = false;
    const QSignalBlocker blocker(commentLineList);
    commentLineList->clearSelection();
    for (int row = 0; row < commentLineList->count(); ++row) {
        QListWidgetItem *item = commentLineList->item(row);
        if (!item || item->data(Qt::UserRole).toInt() != lineNumber) {
            continue;
        }

        item->setSelected(true);
        commentLineList->setCurrentItem(item);
        commentLineList->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        found = true;
        break;
    }

    if (!found && clearWhenMissing) {
        commentLineList->setCurrentItem(nullptr);
    }
}

int MainWindow::adjacentCommentLine(bool forward) const
{
    const QList<AnnotatedTextEdit::CommentEntry> comments = editor->commentEntries();
    if (comments.isEmpty()) {
        return -1;
    }

    const int currentLine = editor->currentLineNumber();
    if (forward) {
        for (const AnnotatedTextEdit::CommentEntry &entry : comments) {
            if (entry.lineNumber > currentLine) {
                return entry.lineNumber;
            }
        }
        return comments.constFirst().lineNumber;
    }

    for (auto it = comments.crbegin(); it != comments.crend(); ++it) {
        if (it->lineNumber < currentLine) {
            return it->lineNumber;
        }
    }
    return comments.constLast().lineNumber;
}

QVector<MainWindow::ShortcutBinding> MainWindow::shortcutBindings() const
{
    return {
        { QStringLiteral("newDocument"), QStringLiteral("新建文档"), QStringLiteral("文件"), newAction, QKeySequence::New },
        { QStringLiteral("openDocument"), QStringLiteral("打开文档"), QStringLiteral("文件"), openAction, QKeySequence::Open },
        { QStringLiteral("saveDocument"), QStringLiteral("保存文档"), QStringLiteral("文件"), saveAction, QKeySequence::Save },
        { QStringLiteral("saveDocumentAs"), QStringLiteral("另存为"), QStringLiteral("文件"), saveAsAction, QKeySequence::SaveAs },
        { QStringLiteral("autosaveNow"), QStringLiteral("立即自动保存"), QStringLiteral("文件"), autosaveNowAction, QKeySequence(QStringLiteral("Ctrl+Alt+S")) },
        { QStringLiteral("findPanel"), QStringLiteral("打开查找替换"), QStringLiteral("编辑"), showFindAction, QKeySequence::Find },
        { QStringLiteral("findNext"), QStringLiteral("查找下一个"), QStringLiteral("编辑"), findNextAction, QKeySequence::FindNext },
        { QStringLiteral("findPrevious"), QStringLiteral("查找上一个"), QStringLiteral("编辑"), findPreviousAction, QKeySequence::FindPrevious },
        { QStringLiteral("replaceCurrent"), QStringLiteral("替换当前"), QStringLiteral("编辑"), replaceAction, QKeySequence::Replace },
        { QStringLiteral("replaceAll"), QStringLiteral("全部替换"), QStringLiteral("编辑"), replaceAllAction, QKeySequence(QStringLiteral("Ctrl+Shift+H")) },
        { QStringLiteral("insertComment"), QStringLiteral("插入注释行"), QStringLiteral("批注"), insertCommentAction, QKeySequence(QStringLiteral("Ctrl+Alt+M")) },
        { QStringLiteral("batchInsertComments"), QStringLiteral("按范围批量添加批注"), QStringLiteral("批注"), batchInsertCommentsAction, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+M")) },
        { QStringLiteral("translateLines"), QStringLiteral("翻译当前行或所选行并添加批注"), QStringLiteral("批注"), translateLinesAction, QKeySequence(QStringLiteral("Ctrl+Alt+T")) },
        { QStringLiteral("translateDocument"), QStringLiteral("翻译全文并添加批注"), QStringLiteral("批注"), translateDocumentAction, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+T")) },
        { QStringLiteral("toggleComment"), QStringLiteral("折叠或展开当前注释"), QStringLiteral("批注"), toggleCommentAction, QKeySequence(QStringLiteral("Ctrl+Alt+/")) },
        { QStringLiteral("deleteComment"), QStringLiteral("删除当前注释"), QStringLiteral("批注"), deleteCommentAction, QKeySequence(QStringLiteral("Ctrl+Alt+Delete")) },
        { QStringLiteral("previousComment"), QStringLiteral("跳到上一条注释"), QStringLiteral("批注"), previousCommentAction, QKeySequence(QStringLiteral("Alt+Shift+Up")) },
        { QStringLiteral("nextComment"), QStringLiteral("跳到下一条注释"), QStringLiteral("批注"), nextCommentAction, QKeySequence(QStringLiteral("Alt+Shift+Down")) },
        { QStringLiteral("toggleSelectedComments"), QStringLiteral("批量折叠或展开所选注释"), QStringLiteral("批注"), toggleSelectedCommentsAction, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+/")) },
        { QStringLiteral("deleteSelectedComments"), QStringLiteral("批量删除所选注释"), QStringLiteral("批注"), deleteSelectedCommentsAction, QKeySequence(QStringLiteral("Ctrl+Alt+Shift+Delete")) },
        { QStringLiteral("toggleNavigationDock"), QStringLiteral("显示或隐藏导航"), QStringLiteral("界面"), navigationDock ? navigationDock->toggleViewAction() : nullptr, QKeySequence(QStringLiteral("Ctrl+1")) },
        { QStringLiteral("toggleInspectorDock"), QStringLiteral("显示或隐藏文档信息"), QStringLiteral("界面"), inspectorDock ? inspectorDock->toggleViewAction() : nullptr, QKeySequence(QStringLiteral("Ctrl+2")) },
        { QStringLiteral("toggleFindDock"), QStringLiteral("显示或隐藏查找面板"), QStringLiteral("界面"), findDock ? findDock->toggleViewAction() : nullptr, QKeySequence(QStringLiteral("Ctrl+3")) },
        { QStringLiteral("toggleCommentDock"), QStringLiteral("显示或隐藏注释管理"), QStringLiteral("界面"), commentDock ? commentDock->toggleViewAction() : nullptr, QKeySequence(QStringLiteral("Ctrl+4")) },
        { QStringLiteral("toggleTranslationDock"), QStringLiteral("显示或隐藏翻译工具"), QStringLiteral("界面"), translationDock ? translationDock->toggleViewAction() : nullptr, QKeySequence(QStringLiteral("Ctrl+5")) },
        { QStringLiteral("focusMode"), QStringLiteral("切换专注模式"), QStringLiteral("界面"), focusModeAction, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F) },
        { QStringLiteral("openSettings"), QStringLiteral("打开设置"), QStringLiteral("界面"), settingsAction, QKeySequence(QStringLiteral("Ctrl+,")) },
    };
}

void MainWindow::restoreShortcutSettings()
{
    QJsonObject shortcutObject;
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    const QByteArray shortcutJson = settings.value(shortcutSettingsKey()).toString().toUtf8();
    settings.endGroup();
    if (!shortcutJson.isEmpty()) {
        const QJsonDocument shortcutDocument = QJsonDocument::fromJson(shortcutJson);
        if (shortcutDocument.isObject()) {
            shortcutObject = shortcutDocument.object();
        }
    }

    for (const ShortcutBinding &binding : shortcutBindings()) {
        if (!binding.action) {
            continue;
        }

        QKeySequence sequence = binding.defaultSequence;
        if (shortcutObject.contains(binding.id)) {
            sequence = QKeySequence::fromString(shortcutObject.value(binding.id).toString(), QKeySequence::PortableText);
        }

        binding.action->setShortcut(sequence);
        binding.action->setShortcutContext(binding.id == QStringLiteral("focusMode") ? Qt::ApplicationShortcut : Qt::WindowShortcut);
    }
}

QList<int> MainWindow::selectedManagedLines() const
{
    QList<int> lines;
    if (!commentLineList) {
        return lines;
    }

    const QList<QListWidgetItem *> items = commentLineList->selectedItems();
    lines.reserve(items.size());
    for (QListWidgetItem *item : items) {
        const int lineNumber = item->data(Qt::UserRole).toInt();
        if (!lines.contains(lineNumber)) {
            lines.append(lineNumber);
        }
    }
    std::sort(lines.begin(), lines.end());
    return lines;
}

QList<int> MainWindow::selectedSourceLinesForTranslation() const
{
    QList<int> lines = editor->selectedSourceLines();
    if (lines.isEmpty()) {
        lines.append(editor->currentLineNumber());
    }
    return lines;
}

MainWindow::TranslationSelection MainWindow::chooseWholeDocumentTranslationSelection(bool *accepted) const
{
    if (accepted) {
        *accepted = false;
    }

    TranslationSelection selection;
    QDialog dialog(const_cast<MainWindow *>(this));
    dialog.setWindowTitle(QStringLiteral("选择翻译范围"));
    dialog.resize(520, 240);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *hintLabel = new QLabel(QStringLiteral("全文翻译会自动跳过已有批注行和空行。你可以限定为当前章节，或只处理还没有任何批注的章节。"), &dialog);
    hintLabel->setObjectName(QStringLiteral("dockInfoTitle"));
    hintLabel->setWordWrap(true);

    auto *scopeCombo = new QComboBox(&dialog);
    scopeCombo->addItem(QStringLiteral("全文"), static_cast<int>(TranslationDocumentScope::WholeDocument));
    scopeCombo->addItem(QStringLiteral("当前章节"), static_cast<int>(TranslationDocumentScope::CurrentChapter));
    scopeCombo->addItem(QStringLiteral("仅未批注章节"), static_cast<int>(TranslationDocumentScope::UnannotatedChapters));

    auto *summaryLabel = new QLabel(&dialog);
    summaryLabel->setWordWrap(true);
    summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto linesForScope = [this](TranslationDocumentScope scope) {
        if (editor->lineCount() <= 0) {
            return QList<int>();
        }

        if (scope == TranslationDocumentScope::CurrentChapter) {
            const int currentChapter = chapterIndexForLine(editor->currentLineNumber());
            const QPair<int, int> range = chapterLineRangeForIndex(currentChapter);
            return sourceLinesForRange(range.first, range.second);
        }

        if (scope == TranslationDocumentScope::UnannotatedChapters) {
            QList<int> lines;
            if (chapterEntries.isEmpty()) {
                bool hasComment = false;
                for (int lineNumber = 0; lineNumber < editor->lineCount(); ++lineNumber) {
                    if (editor->hasCommentAtLine(lineNumber)) {
                        hasComment = true;
                        break;
                    }
                }
                return hasComment ? QList<int>() : sourceLinesForRange(0, editor->lineCount() - 1);
            }

            for (int chapterIndex = 0; chapterIndex < chapterEntries.size(); ++chapterIndex) {
                const QPair<int, int> range = chapterLineRangeForIndex(chapterIndex);
                bool hasComment = false;
                for (int lineNumber = range.first; lineNumber <= range.second; ++lineNumber) {
                    if (editor->hasCommentAtLine(lineNumber)) {
                        hasComment = true;
                        break;
                    }
                }
                if (!hasComment) {
                    const QList<int> chapterLines = sourceLinesForRange(range.first, range.second);
                    for (int lineNumber : chapterLines) {
                        lines.append(lineNumber);
                    }
                }
            }
            return lines;
        }

        return sourceLinesForRange(0, editor->lineCount() - 1);
    };

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    auto *okButton = buttonBox->button(QDialogButtonBox::Ok);

    auto updateSummary = [this, scopeCombo, summaryLabel, okButton, &linesForScope]() {
        const auto scope = static_cast<TranslationDocumentScope>(scopeCombo->currentData().toInt());
        const QList<int> lines = linesForScope(scope);
        QString description;
        if (scope == TranslationDocumentScope::CurrentChapter) {
            const int currentChapter = chapterIndexForLine(editor->currentLineNumber());
            const QString chapterTitle = currentChapter >= 0 && currentChapter < chapterEntries.size()
                ? chapterEntries.at(currentChapter).title
                : QStringLiteral("当前所在范围");
            description = QStringLiteral("当前将只翻译章节：%1").arg(chapterTitle);
        } else if (scope == TranslationDocumentScope::UnannotatedChapters) {
            int chapterCount = 0;
            if (chapterEntries.isEmpty()) {
                chapterCount = lines.isEmpty() ? 0 : 1;
            } else {
                for (int chapterIndex = 0; chapterIndex < chapterEntries.size(); ++chapterIndex) {
                    const QPair<int, int> range = chapterLineRangeForIndex(chapterIndex);
                    bool hasComment = false;
                    for (int lineNumber = range.first; lineNumber <= range.second; ++lineNumber) {
                        if (editor->hasCommentAtLine(lineNumber)) {
                            hasComment = true;
                            break;
                        }
                    }
                    if (!hasComment) {
                        ++chapterCount;
                    }
                }
            }
            description = chapterCount > 0
                ? QStringLiteral("将只翻译完全未批注的 %1 个章节。" ).arg(chapterCount)
                : QStringLiteral("当前没有完全未批注的章节可翻译。");
        } else {
            description = QStringLiteral("将扫描整篇文档，并自动跳过已有批注行和空行。");
        }

        summaryLabel->setText(QStringLiteral("%1\n预计覆盖原文 %2 行。" ).arg(description).arg(lines.size()));
        okButton->setEnabled(!lines.isEmpty());
    };

    layout->addWidget(hintLabel);
    layout->addWidget(scopeCombo);
    layout->addWidget(summaryLabel);
    layout->addStretch();
    layout->addWidget(buttonBox);

    connect(scopeCombo, &QComboBox::currentIndexChanged, &dialog, updateSummary);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    updateSummary();

    if (dialog.exec() != QDialog::Accepted) {
        return selection;
    }

    const auto scope = static_cast<TranslationDocumentScope>(scopeCombo->currentData().toInt());
    selection.lines = linesForScope(scope);
    if (scope == TranslationDocumentScope::CurrentChapter) {
        selection.progressTitle = QStringLiteral("正在翻译当前章节并生成批注...");
        selection.successLabel = QStringLiteral("已为当前章节中的 %1 行添加翻译批注");
    } else if (scope == TranslationDocumentScope::UnannotatedChapters) {
        selection.progressTitle = QStringLiteral("正在翻译未批注章节并生成批注...");
        selection.successLabel = QStringLiteral("已为未批注章节中的 %1 行添加翻译批注");
    } else {
        selection.progressTitle = QStringLiteral("正在翻译全文并生成批注...");
        selection.successLabel = QStringLiteral("已为全文中的 %1 行添加翻译批注");
    }

    if (accepted) {
        *accepted = true;
    }
    return selection;
}

void MainWindow::beginTranslationTask(
    const QStringList &sourceLines,
    const QList<int> &requestedLines,
    const QString &progressTitle,
    const QString &successLabel,
    int skippedExistingCount,
    int skippedEmptyCount)
{
    if (translationWatcher->isRunning()) {
        statusBar()->showMessage(QStringLiteral("翻译任务仍在进行中"), 2500);
        return;
    }

    QList<int> normalizedRequestedLines = requestedLines;
    std::sort(normalizedRequestedLines.begin(), normalizedRequestedLines.end());
    normalizedRequestedLines.erase(std::unique(normalizedRequestedLines.begin(), normalizedRequestedLines.end()), normalizedRequestedLines.end());

    translationProgressTitle = progressTitle;
    translationSuccessLabelTemplate = successLabel;
    translationSkippedExistingCount = skippedExistingCount;
    translationSkippedEmptyCount = skippedEmptyCount;
    translationRequestedCount = normalizedRequestedLines.size();
    translationCompletedCount = 0;
    translationInsertedCount = 0;
    translationFailedCount = 0;
    translationSkippedConflictCount = 0;
    translationAbortedCount = 0;
    translationCancelFlag = std::make_shared<std::atomic_bool>(false);
    translationUiFlushTimer->stop();
    translationElapsedUiTimer->stop();
    {
        QMutexLocker locker(&translationUiUpdateMutex);
        translationPendingUiUpdates.clear();
        translationUiFlushScheduled = false;
    }
    translationPendingSourceTexts.clear();
    translationPendingSourceLineCount = sourceLines.size();
    for (int lineNumber : normalizedRequestedLines) {
        if (lineNumber >= 0 && lineNumber < sourceLines.size()) {
            translationPendingSourceTexts.insert(lineNumber, sourceLines.at(lineNumber));
        }
    }

    translateLinesAction->setEnabled(false);
    translateDocumentAction->setEnabled(false);
    translationElapsedTimer.restart();
    translationElapsedUiTimer->start();
    refreshTranslationProgressUi(true);
    statusBar()->showMessage(QStringLiteral("%1 你可以继续编辑，译文会逐行显示到批注区。").arg(progressTitle), 0);

    translationWatcher->setFuture(QtConcurrent::run([this, sourceLines, normalizedRequestedLines, cancelFlag = translationCancelFlag]() {
        return runTranslationTask(sourceLines, normalizedRequestedLines, cancelFlag);
    }));
    refreshTranslationProgressUi(true);
}

QString MainWindow::buildTranslationProgressMessage() const
{
    QStringList parts;
    parts.append(QStringLiteral("已完成 %1/%2").arg(translationCompletedCount).arg(qMax(translationRequestedCount, 0)));
    parts.append(QStringLiteral("已显示 %1 行").arg(translationInsertedCount));
    if (translationFailedCount > 0) {
        parts.append(QStringLiteral("失败 %1 行").arg(translationFailedCount));
    }
    if (translationSkippedConflictCount > 0) {
        parts.append(QStringLiteral("跳过变更/已有批注 %1 行").arg(translationSkippedConflictCount));
    }
    return QStringLiteral("%1 %2").arg(translationProgressTitle, parts.join(QStringLiteral("，")));
}

QString MainWindow::buildTranslationElapsedLabel() const
{
    if (!translationElapsedTimer.isValid()) {
        return QStringLiteral("已耗时 00:00");
    }

    const qint64 elapsedSeconds = qMax<qint64>(0, translationElapsedTimer.elapsed() / 1000);
    if (translationTimeoutMs > 0) {
        const qint64 remainingSeconds = qMax<qint64>(0, (translationTimeoutMs / 1000) - elapsedSeconds);
        return QStringLiteral("%1 / 剩 %2")
            .arg(formatDurationLabel(elapsedSeconds))
            .arg(formatDurationLabel(remainingSeconds));
    }
    return QStringLiteral("已耗时 %1").arg(formatDurationLabel(elapsedSeconds));
}

void MainWindow::refreshTranslationProgressUi(bool active)
{
    if (!translationProgressBar || !translationCancelButton || !translationProgressTimeLabel) {
        return;
    }

    if (!active) {
        translationProgressBar->setEnabled(false);
        translationProgressBar->setRange(0, 1);
        translationProgressBar->setValue(0);
        translationProgressBar->setFormat(QStringLiteral("空闲"));
        translationProgressTimeLabel->setText(QStringLiteral("空闲"));
        translationCancelButton->setEnabled(false);
        translationCancelButton->setText(QStringLiteral("取消"));
        return;
    }

    translationProgressBar->setEnabled(true);
    translationProgressBar->setRange(0, qMax(1, translationRequestedCount));
    translationProgressBar->setValue(qBound(0, translationCompletedCount, qMax(1, translationRequestedCount)));
    translationProgressBar->setFormat(QStringLiteral("%1/%2 · 已显示 %3")
                                          .arg(translationCompletedCount)
                                          .arg(qMax(1, translationRequestedCount))
                                          .arg(translationInsertedCount));
    translationProgressTimeLabel->setText(buildTranslationElapsedLabel());
    translationCancelButton->setEnabled(static_cast<bool>(translationCancelFlag));
    if (translationCancelButton->text() != QStringLiteral("正在取消...")) {
        translationCancelButton->setText(QStringLiteral("取消"));
    }
}

int MainWindow::markPendingTranslationsAborted()
{
    QList<QPair<int, QString>> abortedComments;
    for (auto it = translationPendingSourceTexts.constBegin(); it != translationPendingSourceTexts.constEnd(); ++it) {
        const int lineNumber = it.key();
        if (lineNumber < 0 || lineNumber >= editor->lineCount()) {
            continue;
        }
        if (editor->lineText(lineNumber) != it.value() || editor->hasCommentAtLine(lineNumber)) {
            continue;
        }
        abortedComments.append(qMakePair(lineNumber, QStringLiteral("已中止")));
    }
    if (abortedComments.isEmpty()) {
        return 0;
    }
    return editor->addCommentsToLines(abortedComments, false);
}

void MainWindow::flushPendingTranslationUpdates()
{
    constexpr int maxUpdatesPerFlush = 8;
    QList<TranslationUiUpdate> updatesToProcess;
    bool hasMorePending = false;

    {
        QMutexLocker locker(&translationUiUpdateMutex);
        if (translationPendingUiUpdates.isEmpty()) {
            translationUiFlushScheduled = false;
            return;
        }

        const int updatesToTake = qMin(maxUpdatesPerFlush, translationPendingUiUpdates.size());
        updatesToProcess.reserve(updatesToTake);
        for (int index = 0; index < updatesToTake; ++index) {
            updatesToProcess.append(translationPendingUiUpdates.takeFirst());
        }
        hasMorePending = !translationPendingUiUpdates.isEmpty();
        if (!hasMorePending) {
            translationUiFlushScheduled = false;
        }
    }

    QList<QPair<int, QString>> insertions;
    int localConflicts = 0;

    for (const TranslationUiUpdate &update : updatesToProcess) {
        ++translationCompletedCount;

        if (!update.success) {
            ++translationFailedCount;
            continue;
        }

        const bool canInsert = update.lineNumber >= 0
            && update.lineNumber < editor->lineCount()
            && translationPendingSourceTexts.contains(update.lineNumber)
            && editor->lineText(update.lineNumber) == translationPendingSourceTexts.value(update.lineNumber)
            && !editor->hasCommentAtLine(update.lineNumber);

        if (!canInsert) {
            ++localConflicts;
            continue;
        }

        insertions.append(qMakePair(update.lineNumber, update.translatedText));
    }

    if (!insertions.isEmpty()) {
        const int insertedNow = editor->addCommentsToLines(insertions, false);
        translationInsertedCount += insertedNow;
        for (const auto &item : insertions) {
            translationPendingSourceTexts.remove(item.first);
        }
        if (insertedNow < insertions.size()) {
            localConflicts += insertions.size() - insertedNow;
        }
    }
    translationSkippedConflictCount += localConflicts;

    refreshTranslationProgressUi(true);
    statusBar()->showMessage(buildTranslationProgressMessage(), 0);

    if (hasMorePending) {
        translationUiFlushTimer->start();
    }
}

MainWindow::TranslationTaskResult MainWindow::runTranslationTask(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const std::shared_ptr<std::atomic_bool> &cancelFlag)
{
    TranslationTaskResult result;
    QList<int> sortedTargets = targetLines;
    std::sort(sortedTargets.begin(), sortedTargets.end());
    sortedTargets.erase(std::unique(sortedTargets.begin(), sortedTargets.end()), sortedTargets.end());
    result.requestedCount = sortedTargets.size();

    if (sortedTargets.isEmpty() || isTranslationCanceled(cancelFlag)) {
        result.canceled = isTranslationCanceled(cancelFlag);
        return result;
    }

    const auto onLineProcessed = [this, cancelFlag](int lineNumber, const QString &translatedText, bool success) {
        if (isTranslationCanceled(cancelFlag)) {
            return;
        }

        bool shouldScheduleFlush = false;
        {
            QMutexLocker locker(&translationUiUpdateMutex);
            translationPendingUiUpdates.append({ lineNumber, translatedText, success });
            if (!translationUiFlushScheduled) {
                translationUiFlushScheduled = true;
                shouldScheduleFlush = true;
            }
        }

        if (shouldScheduleFlush) {
            QMetaObject::invokeMethod(this, [this]() {
                flushPendingTranslationUpdates();
            }, Qt::QueuedConnection);
        }
    };

    requestContextualTranslations(sourceLines, sortedTargets, cancelFlag, onLineProcessed, &result.errorMessage);
    QMetaObject::invokeMethod(this, [this]() {
        flushPendingTranslationUpdates();
    }, Qt::BlockingQueuedConnection);
    if (isTranslationCanceled(cancelFlag)) {
        result.canceled = true;
    } else if (isTranslationTimeoutMessage(result.errorMessage)) {
        result.timedOut = true;
    }
    return result;
}

MainWindow::ExportContentMode MainWindow::chooseExportContentMode(DocumentFormat format, bool *accepted) const
{
    if (accepted) {
        *accepted = false;
    }

    if (format != DocumentFormat::Txt && format != DocumentFormat::Docx) {
        if (accepted) {
            *accepted = true;
        }
        return ExportContentMode::SourceOnly;
    }

    QMessageBox box(const_cast<MainWindow *>(this));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(format == DocumentFormat::Txt ? QStringLiteral("TXT 导出内容") : QStringLiteral("DOCX 导出内容"));
    box.setText(QStringLiteral("选择要导出的内容范围。"));
    QPushButton *sourceOnlyButton = box.addButton(QStringLiteral("仅原文"), QMessageBox::AcceptRole);
    QPushButton *withCommentsButton = box.addButton(QStringLiteral("原文 + 注释"), QMessageBox::AcceptRole);
    QPushButton *commentsOnlyButton = box.addButton(QStringLiteral("仅注释"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == sourceOnlyButton) {
        if (accepted) {
            *accepted = true;
        }
        return ExportContentMode::SourceOnly;
    }
    if (box.clickedButton() == withCommentsButton) {
        if (accepted) {
            *accepted = true;
        }
        return ExportContentMode::SourceWithComments;
    }
    if (box.clickedButton() == commentsOnlyButton) {
        if (accepted) {
            *accepted = true;
        }
        return ExportContentMode::CommentsOnly;
    }

    return ExportContentMode::SourceOnly;
}

QString MainWindow::buildExportText(ExportContentMode mode) const
{
    if (mode == ExportContentMode::SourceOnly) {
        return editor->sourcePlainText();
    }

    const QList<AnnotatedTextEdit::CommentEntry> comments = editor->commentEntries();
    QHash<int, AnnotatedTextEdit::CommentEntry> commentMap;
    for (const AnnotatedTextEdit::CommentEntry &entry : comments) {
        commentMap.insert(entry.lineNumber, entry);
    }

    QStringList output;
    if (mode == ExportContentMode::SourceWithComments) {
        for (int lineNumber = 0; lineNumber < editor->lineCount(); ++lineNumber) {
            output.append(editor->lineText(lineNumber));
            if (!commentMap.contains(lineNumber)) {
                continue;
            }

            const QStringList commentLines = editor->commentPlainTextAtLine(lineNumber).split(QChar('\n'), Qt::KeepEmptyParts);
            for (const QString &commentLine : commentLines) {
                output.append(QStringLiteral("    [注释] %1").arg(commentLine));
            }
        }
        return output.join(QChar('\n'));
    }

    for (const AnnotatedTextEdit::CommentEntry &entry : comments) {
        output.append(QStringLiteral("L%1 %2").arg(entry.lineNumber + 1).arg(summarizeSourceLine(editor->lineText(entry.lineNumber))));
        const QStringList commentLines = editor->commentPlainTextAtLine(entry.lineNumber).split(QChar('\n'), Qt::KeepEmptyParts);
        for (const QString &commentLine : commentLines) {
            output.append(QStringLiteral("    %1").arg(commentLine));
        }
        output.append(QString());
    }
    while (!output.isEmpty() && output.constLast().isEmpty()) {
        output.removeLast();
    }
    return output.join(QChar('\n'));
}

void MainWindow::newDocument()
{
    if (!maybeSave()) {
        return;
    }

    autosaveTimer->stop();
    statsRefreshTimer->stop();
    chapterRefreshTimer->stop();
    const QString previousAutosavePath = autosaveFilePath(currentFilePath);
    sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    applyConfiguredEditorDefaults();
    writePlainTextToEditor(QString());
    resetTransientUiState();
    editor->document()->setModified(false);
    lastSavedAtUtc.clear();
    setCreatedAtUtc(createdAtUtc());
    setCurrentFile(QString(), DocumentFormat::Trx);
    chapterEntries.clear();
    chapterList->clear();
    rebuildChapterIndex();
    refreshDocumentStats();
    clearAutosaveSnapshot(previousAutosavePath);
    clearAutosaveSnapshot();
    statusBar()->showMessage(QStringLiteral("已新建文档"), 3000);
}

bool MainWindow::maybeSave()
{
    if (!editor->document()->isModified()) {
        return true;
    }

    const auto result = QMessageBox::warning(
        this,
        QStringLiteral("保存更改"),
        QStringLiteral("当前文档有未保存更改，是否先保存？"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Cancel) {
        return false;
    }
    if (result == QMessageBox::Save) {
        return saveDocument();
    }
    return true;
}

bool MainWindow::openDocument()
{
    if (!maybeSave()) {
        return false;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("打开文档"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("支持的文档 (*.txt *.docx *.tpx *.trx);;Text Files (*.txt);;Word Documents (*.docx);;TPX Documents (*.tpx *.trx)"));

    if (filePath.isEmpty()) {
        return false;
    }

    return loadFromPath(filePath);
}

bool MainWindow::openRecentFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return false;
    }
    if (!maybeSave()) {
        return false;
    }
    return loadFromPath(filePath);
}

bool MainWindow::saveDocument()
{
    if (currentFilePath.isEmpty()) {
        return saveDocumentAs();
    }

    ExportContentMode exportMode = ExportContentMode::SourceOnly;
    bool accepted = true;
    if (currentFormat == DocumentFormat::Txt || currentFormat == DocumentFormat::Docx) {
        exportMode = chooseExportContentMode(currentFormat, &accepted);
        if (!accepted) {
            return false;
        }
    }

    return saveToPath(currentFilePath, currentFormat, false, exportMode);

}

void MainWindow::applyEditorUiPreferences()
{
    const QPlainTextEdit::LineWrapMode requestedWrapMode = editorWrapEnabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap;
    const bool wrapChanged = editor->lineWrapMode() != requestedWrapMode;
    const bool tabChanged = !qFuzzyCompare(editor->tabStopDistance() + 1.0, static_cast<qreal>(editorTabStopDistance) + 1.0);
    const bool marginChanged = !qFuzzyCompare(editor->document()->documentMargin() + 1.0, static_cast<qreal>(editorDocumentMargin) + 1.0);
    if (!wrapChanged && !tabChanged && !marginChanged) {
        return;
    }

    const QTextCursor preservedCursor = editor->textCursor();
    const int scrollValue = editor->verticalScrollBar() ? editor->verticalScrollBar()->value() : 0;
    editor->setUpdatesEnabled(false);
    editor->viewport()->setUpdatesEnabled(false);
    editor->setLineWrapMode(editorWrapEnabled ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    editor->setTabStopDistance(editorTabStopDistance);
    editor->document()->setDocumentMargin(editorDocumentMargin);
    editor->setTextCursor(preservedCursor);
    if (editor->verticalScrollBar()) {
        editor->verticalScrollBar()->setValue(scrollValue);
    }
    editor->viewport()->setUpdatesEnabled(true);
    editor->setUpdatesEnabled(true);
    editor->viewport()->update();
}

void MainWindow::applyConfiguredEditorDefaults()
{
    editor->setSourceAppearance(defaultSourceAppearance);
    editor->setCommentAppearance(defaultCommentAppearance);
    refreshCommentManager();
}

void MainWindow::openSettings()
{
    openSettingsDialog(0);
}

void MainWindow::openSettingsDialog(int initialPage)
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("设置"));
    dialog.resize(900, 680);
    dialog.setMinimumSize(780, 560);

    auto *dialogLayout = new QVBoxLayout(&dialog);
    auto *tabs = new QTabWidget(&dialog);
    dialogLayout->addWidget(tabs);
    auto wrapSettingsPage = [tabs](QWidget *page) {
        auto *scrollArea = new QScrollArea(tabs);
        scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setWidget(page);
        return scrollArea;
    };

    auto updateColorButton = [](QPushButton *button, const QColor &color) {
        const QColor foreground = color.lightnessF() < 0.55 ? QColor(QStringLiteral("#ffffff")) : QColor(QStringLiteral("#172432"));
        button->setText(color.name(QColor::HexRgb).toUpper());
        button->setStyleSheet(
            QStringLiteral("QPushButton { background:%1; color:%2; border:1px solid #b7c5d3; border-radius:10px; padding:8px 12px; text-align:left; }")
                .arg(color.name(QColor::HexRgb), foreground.name(QColor::HexRgb)));
    };

    AnnotatedTextEdit::TextAppearance sourceAppearance = editor->sourceAppearance();
    AnnotatedTextEdit::TextAppearance commentAppearance = editor->commentAppearance();

    auto *generalPage = new QWidget(tabs);
    auto *generalLayout = new QVBoxLayout(generalPage);
    auto *autosaveGroup = new QGroupBox(QStringLiteral("自动保存"), generalPage);
    auto *autosaveLayout = new QFormLayout(autosaveGroup);
    auto *autosaveEnabledBox = new QCheckBox(QStringLiteral("启用自动保存"), autosaveGroup);
    autosaveEnabledBox->setChecked(autosaveEnabled);
    auto *autosaveIntervalBox = new QSpinBox(autosaveGroup);
    autosaveIntervalBox->setRange(1, 600);
    autosaveIntervalBox->setSuffix(QStringLiteral(" 秒"));
    autosaveIntervalBox->setValue(qMax(1, autosaveIntervalMs / 1000));
    autosaveLayout->addRow(autosaveEnabledBox);
    autosaveLayout->addRow(QStringLiteral("保存间隔"), autosaveIntervalBox);
    generalLayout->addWidget(autosaveGroup);

    auto *windowGroup = new QGroupBox(QStringLiteral("界面与启动"), generalPage);
    auto *windowLayout = new QFormLayout(windowGroup);
    auto *rememberWindowLayoutBox = new QCheckBox(QStringLiteral("记住窗口大小、停靠布局和分栏状态"), windowGroup);
    rememberWindowLayoutBox->setChecked(rememberWindowLayout);
    auto *startupFocusModeBox = new QCheckBox(QStringLiteral("启动时进入专注模式"), windowGroup);
    startupFocusModeBox->setChecked(focusModeEnabled);
    windowLayout->addRow(rememberWindowLayoutBox);
    windowLayout->addRow(startupFocusModeBox);
    generalLayout->addWidget(windowGroup);

    auto *editorUiGroup = new QGroupBox(QStringLiteral("编辑器界面"), generalPage);
    auto *editorUiLayout = new QFormLayout(editorUiGroup);
    auto *documentMarginBox = new QSpinBox(editorUiGroup);
    documentMarginBox->setRange(0, 120);
    documentMarginBox->setSuffix(QStringLiteral(" px"));
    documentMarginBox->setValue(editorDocumentMargin);
    auto *tabStopBox = new QSpinBox(editorUiGroup);
    tabStopBox->setRange(16, 160);
    tabStopBox->setSingleStep(4);
    tabStopBox->setSuffix(QStringLiteral(" px"));
    tabStopBox->setValue(editorTabStopDistance);
    auto *wrapEnabledBox = new QCheckBox(QStringLiteral("按窗口宽度自动换行"), editorUiGroup);
    wrapEnabledBox->setChecked(editorWrapEnabled);
    editorUiLayout->addRow(QStringLiteral("正文留白"), documentMarginBox);
    editorUiLayout->addRow(QStringLiteral("制表位宽度"), tabStopBox);
    editorUiLayout->addRow(wrapEnabledBox);
    generalLayout->addWidget(editorUiGroup);

    auto *generalHint = new QLabel(QStringLiteral("这些设置会写入应用配置，影响之后新建和打开的普通文档。TPX 文档仍会保存当前文档自己的样式。"), generalPage);
    generalHint->setWordWrap(true);
    generalHint->setStyleSheet(QStringLiteral("color:#546173;"));
    generalLayout->addWidget(generalHint);
    generalLayout->addStretch(1);
    tabs->addTab(wrapSettingsPage(generalPage), QStringLiteral("常规"));

    auto *translationPage = new QWidget(tabs);
    auto *translationPageLayout = new QVBoxLayout(translationPage);

    auto *translationGroup = new QGroupBox(QStringLiteral("翻译后端"), translationPage);
    auto *translationLayout = new QFormLayout(translationGroup);
    auto *translationModeBox = new QComboBox(translationGroup);
    translationModeBox->addItem(QStringLiteral("本地 Ollama"), true);
    translationModeBox->addItem(QStringLiteral("云端翻译服务"), false);
    translationModeBox->setCurrentIndex(translationUseOllama ? 0 : 1);
    auto *ollamaEndpointEdit = new QLineEdit(translationOllamaEndpoint, translationGroup);
    auto *ollamaScanButton = new QPushButton(QStringLiteral("自动扫描"), translationGroup);
    auto *ollamaEndpointRow = new QWidget(translationGroup);
    auto *ollamaEndpointRowLayout = new QHBoxLayout(ollamaEndpointRow);
    ollamaEndpointRowLayout->setContentsMargins(0, 0, 0, 0);
    ollamaEndpointRowLayout->setSpacing(8);
    ollamaEndpointRowLayout->addWidget(ollamaEndpointEdit, 1);
    ollamaEndpointRowLayout->addWidget(ollamaScanButton);
    auto *ollamaModelBox = new QComboBox(translationGroup);
    ollamaModelBox->setEditable(true);
    ollamaModelBox->setInsertPolicy(QComboBox::NoInsert);
    if (!translationOllamaModel.trimmed().isEmpty()) {
        ollamaModelBox->addItem(translationOllamaModel.trimmed());
    }
    ollamaModelBox->setCurrentText(translationOllamaModel.trimmed());
    auto *translationFallbackBox = new QCheckBox(QStringLiteral("本地失败时自动回退云端"), translationGroup);
    translationFallbackBox->setChecked(translationFallbackToOnline);
    auto *translationStrategyBox = new QComboBox(translationGroup);
    translationStrategyBox->addItem(QStringLiteral("响应优先"), static_cast<int>(LocalTranslationStrategy::Responsive));
    translationStrategyBox->addItem(QStringLiteral("一致性优先"), static_cast<int>(LocalTranslationStrategy::Consistent));
    translationStrategyBox->setCurrentIndex(translationLocalStrategy == LocalTranslationStrategy::Consistent ? 1 : 0);
    auto *translationDisableThinkingBox = new QCheckBox(QStringLiteral("关闭 Thinking"), translationGroup);
    translationDisableThinkingBox->setChecked(translationDisableThinking);
    translationDisableThinkingBox->setToolTip(QStringLiteral("勾选后会向 Ollama 发送 think=false，减少思考输出和额外耗时。"));
    auto *translationTimeoutBox = new QSpinBox(translationGroup);
    translationTimeoutBox->setRange(0, 3600);
    translationTimeoutBox->setSpecialValueText(QStringLiteral("不限"));
    translationTimeoutBox->setSuffix(QStringLiteral(" 秒"));
    translationTimeoutBox->setValue(qMax(0, translationTimeoutMs / 1000));
    translationTimeoutBox->setToolTip(QStringLiteral("设为 0 表示本地 Ollama 不限时等待；云端翻译仍会使用内置超时。"));
    auto *translationContextBox = new QSpinBox(translationGroup);
    translationContextBox->setRange(0, 20);
    translationContextBox->setSuffix(QStringLiteral(" 行 / 侧"));
    translationContextBox->setValue(translationContextRadius);
    auto *translationContextHint = new QLabel(QStringLiteral("上下文大小表示目标行前后各参考多少行。设为 0 表示只翻译目标行本身。"), translationGroup);
    translationContextHint->setWordWrap(true);
    translationContextHint->setStyleSheet(QStringLiteral("color:#546173;"));
    auto *translationCloudHint = new QLabel(QStringLiteral("云端模式当前使用内置在线翻译服务，支持基础逐行翻译；上下文窗口主要用于本地 Ollama 上下文翻译。"), translationGroup);
    translationCloudHint->setWordWrap(true);
    translationCloudHint->setStyleSheet(QStringLiteral("color:#546173;"));
    auto *translationStrategyHint = new QLabel(QStringLiteral("响应优先会把大范围本地翻译拆成更小请求，更快看到首条结果；一致性优先会保留更大的分块，术语统一性更强。单行翻译始终走专用快路径。"), translationGroup);
    translationStrategyHint->setWordWrap(true);
    translationStrategyHint->setStyleSheet(QStringLiteral("color:#546173;"));
    auto *ollamaScanStatus = new QLabel(QStringLiteral("将自动扫描当前 Ollama 地址下的可用模型。"), translationGroup);
    ollamaScanStatus->setWordWrap(true);
    ollamaScanStatus->setStyleSheet(QStringLiteral("color:#546173;"));
    auto *translationThinkingHint = new QLabel(QStringLiteral("部分支持 Thinking 的模型在开启思考后会更慢；关闭可减少额外推理文本和等待时间。"), translationGroup);
    translationThinkingHint->setWordWrap(true);
    translationThinkingHint->setStyleSheet(QStringLiteral("color:#546173;"));
    auto *translationTimeoutHint = new QLabel(QStringLiteral("慢模型可将超时设为“不限”，避免本地生成尚未完成就被提前中断。"), translationGroup);
    translationTimeoutHint->setWordWrap(true);
    translationTimeoutHint->setStyleSheet(QStringLiteral("color:#546173;"));
    auto refreshOllamaModels = [&dialog, translationModeBox, ollamaEndpointEdit, ollamaScanButton, ollamaModelBox, ollamaScanStatus]() {
        if (!translationModeBox->currentData().toBool()) {
            return;
        }

        const QString normalizedEndpoint = normalizeOllamaEndpoint(ollamaEndpointEdit->text());
        ollamaEndpointEdit->setText(normalizedEndpoint);
        const QString currentModel = ollamaModelBox->currentText().trimmed();

        ollamaScanButton->setEnabled(false);
        ollamaScanStatus->setStyleSheet(QStringLiteral("color:#546173;"));
        ollamaScanStatus->setText(QStringLiteral("正在扫描可用模型..."));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        QString scanError;
        const QStringList availableModels = requestAvailableOllamaModels(normalizedEndpoint, &scanError);

        ollamaScanButton->setEnabled(true);
        if (availableModels.isEmpty()) {
            ollamaScanStatus->setStyleSheet(QStringLiteral("color:#b42318;"));
            ollamaScanStatus->setText(scanError.isEmpty()
                                          ? QStringLiteral("未扫描到可用模型。")
                                          : scanError);
            return;
        }

        {
            const QSignalBlocker blocker(ollamaModelBox);
            ollamaModelBox->clear();
            ollamaModelBox->addItems(availableModels);
            if (!currentModel.isEmpty() && !availableModels.contains(currentModel)) {
                ollamaModelBox->insertItem(0, currentModel);
            }
            ollamaModelBox->setCurrentText(currentModel.isEmpty() ? availableModels.constFirst() : currentModel);
        }

        if (!currentModel.isEmpty() && !availableModels.contains(currentModel)) {
            ollamaScanStatus->setStyleSheet(QStringLiteral("color:#9a6700;"));
            ollamaScanStatus->setText(QStringLiteral("已扫描到 %1 个模型；当前配置不在结果中，已为你保留手填值。").arg(availableModels.size()));
        } else {
            ollamaScanStatus->setStyleSheet(QStringLiteral("color:#0f766e;"));
            ollamaScanStatus->setText(QStringLiteral("已扫描到 %1 个可用模型。").arg(availableModels.size()));
        }
    };
    auto updateTranslationControls = [translationModeBox, ollamaEndpointEdit, ollamaScanButton, ollamaModelBox, translationFallbackBox, translationStrategyBox, translationDisableThinkingBox, translationCloudHint, translationStrategyHint, ollamaScanStatus, translationThinkingHint]() {
        const bool useOllama = translationModeBox->currentData().toBool();
        ollamaEndpointEdit->setEnabled(useOllama);
        ollamaScanButton->setEnabled(useOllama);
        ollamaModelBox->setEnabled(useOllama);
        translationFallbackBox->setEnabled(useOllama);
        translationStrategyBox->setEnabled(useOllama);
        translationDisableThinkingBox->setEnabled(useOllama);
        translationCloudHint->setVisible(!useOllama);
        translationStrategyHint->setVisible(useOllama);
        ollamaScanStatus->setVisible(useOllama);
        translationThinkingHint->setVisible(useOllama);
    };
    QObject::connect(translationModeBox, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, updateTranslationControls);
    QObject::connect(translationModeBox, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&dialog, translationModeBox, refreshOllamaModels](int) {
        if (translationModeBox->currentData().toBool()) {
            QTimer::singleShot(0, &dialog, refreshOllamaModels);
        }
    });
    QObject::connect(ollamaScanButton, &QPushButton::clicked, &dialog, refreshOllamaModels);
    QObject::connect(ollamaEndpointEdit, &QLineEdit::editingFinished, &dialog, refreshOllamaModels);
    updateTranslationControls();
    translationLayout->addRow(QStringLiteral("翻译模式"), translationModeBox);
    translationLayout->addRow(QStringLiteral("Ollama 地址"), ollamaEndpointRow);
    translationLayout->addRow(QStringLiteral("模型"), ollamaModelBox);
    translationLayout->addRow(QStringLiteral("本地策略"), translationStrategyBox);
    translationLayout->addRow(QStringLiteral("参考上下文"), translationContextBox);
    translationLayout->addRow(QStringLiteral("请求超时"), translationTimeoutBox);
    translationLayout->addRow(QString(), translationFallbackBox);
    translationLayout->addRow(QString(), translationDisableThinkingBox);
    translationLayout->addRow(QString(), translationStrategyHint);
    translationLayout->addRow(QString(), ollamaScanStatus);
    translationLayout->addRow(QString(), translationThinkingHint);
    translationLayout->addRow(QString(), translationContextHint);
    translationLayout->addRow(QString(), translationTimeoutHint);
    translationLayout->addRow(QString(), translationCloudHint);
    translationPageLayout->addWidget(translationGroup);
    translationPageLayout->addStretch(1);
    tabs->addTab(wrapSettingsPage(translationPage), QStringLiteral("翻译"));

    auto *advancedPage = new QWidget(tabs);
    auto *advancedLayout = new QVBoxLayout(advancedPage);
    auto *advancedGroup = new QGroupBox(QStringLiteral("高级翻译设置"), advancedPage);
    auto *advancedForm = new QFormLayout(advancedGroup);
    advancedForm->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *translationPresetCombo = new QComboBox(advancedGroup);
    translationPresetCombo->addItem(QStringLiteral("默认"));
    translationPresetCombo->addItem(QStringLiteral("DeepSeek v3.2"));
    translationPresetCombo->setCurrentText(translationConfigPreset);

    auto *translationCustomPromptBox = new QCheckBox(QStringLiteral("启用自定义 AI 提示词"), advancedGroup);
    translationCustomPromptBox->setChecked(translationEnableCustomPrompt);
    auto *translationCustomPromptEdit = new QPlainTextEdit(advancedGroup);
    translationCustomPromptEdit->setPlainText(translationCustomPromptTemplate);
    translationCustomPromptEdit->setMinimumHeight(120);
    auto *translationCustomContextPromptEdit = new QPlainTextEdit(advancedGroup);
    translationCustomContextPromptEdit->setPlainText(translationCustomContextPromptTemplate);
    translationCustomContextPromptEdit->setMinimumHeight(120);
    auto *translationLargeApiEdit = new QLineEdit(translationLargeModelApiEndpoint, advancedGroup);
    translationLargeApiEdit->setPlaceholderText(QStringLiteral("https://api.deepseek.com/chat/completions"));
    auto *translationLargeApiKeyEdit = new QLineEdit(translationLargeModelApiKey, advancedGroup);
    translationLargeApiKeyEdit->setEchoMode(QLineEdit::Password);
    translationLargeApiKeyEdit->setPlaceholderText(QStringLiteral("Bearer API Key"));
    auto *translationMaxChunkLinesSpin = new QSpinBox(advancedGroup);
    translationMaxChunkLinesSpin->setRange(1, 100);
    translationMaxChunkLinesSpin->setValue(translationMaxChunkTargetLines);
    auto *translationMaxChunkCharsSpin = new QSpinBox(advancedGroup);
    translationMaxChunkCharsSpin->setRange(1000, 20000);
    translationMaxChunkCharsSpin->setSingleStep(500);
    translationMaxChunkCharsSpin->setValue(translationMaxChunkChars);
    auto *translationStrictOutputBox = new QCheckBox(QStringLiteral("启用严格输出提取（仅保留翻译结果，不输出说明）"), advancedGroup);
    translationStrictOutputBox->setChecked(translationStrictOutputParsing);

    auto *translationAdvancedHint = new QLabel(QStringLiteral("提示：%1 表示目标原文；%2 表示上下文。DeepSeek v3.2 预设适合大模型和大段翻译，可适当增加合并行数、字符数并开启严格输出。"), advancedGroup);
    translationAdvancedHint->setWordWrap(true);
    translationAdvancedHint->setStyleSheet(QStringLiteral("color:#546173;"));

    advancedForm->addRow(QStringLiteral("保存配置预设"), translationPresetCombo);
    advancedForm->addRow(translationCustomPromptBox);
    advancedForm->addRow(QStringLiteral("普通翻译提示词"), translationCustomPromptEdit);
    advancedForm->addRow(QStringLiteral("上下文翻译提示词"), translationCustomContextPromptEdit);
    advancedForm->addRow(QStringLiteral("网络大模型 API 地址"), translationLargeApiEdit);
    advancedForm->addRow(QStringLiteral("网络大模型 API 密钥"), translationLargeApiKeyEdit);
    advancedForm->addRow(QStringLiteral("最大合并目标行数"), translationMaxChunkLinesSpin);
    advancedForm->addRow(QStringLiteral("最大合并字符数"), translationMaxChunkCharsSpin);
    advancedForm->addRow(translationStrictOutputBox);
    auto *defaultsButtonRow = new QHBoxLayout;
    defaultsButtonRow->addStretch(1);
    auto *saveDefaultsButton = new QPushButton(QStringLiteral("设为默认"), advancedGroup);
    auto *restoreDefaultsButton = new QPushButton(QStringLiteral("恢复默认设置"), advancedGroup);
    defaultsButtonRow->addWidget(saveDefaultsButton);
    defaultsButtonRow->addWidget(restoreDefaultsButton);
    advancedLayout->addWidget(advancedGroup);
    advancedLayout->addLayout(defaultsButtonRow);
    advancedLayout->addWidget(translationAdvancedHint);
    advancedLayout->addStretch(1);
    tabs->addTab(wrapSettingsPage(advancedPage), QStringLiteral("高级"));

    auto restoreTranslationDefaultsToControls = [&]() {
        QString savedPreset;
        bool savedEnableCustomPrompt = false;
        QString savedCustomPrompt;
        QString savedCustomContextPrompt;
        QString savedLargeModelApiEndpoint;
        QString savedLargeModelApiKey;
        int savedMaxChunkLines = translationMaxChunkTargetLines;
        int savedMaxChunkChars = translationMaxChunkChars;
        bool savedStrictOutput = translationStrictOutputParsing;

        if (!loadTranslationDefaults(&savedPreset,
                                     &savedEnableCustomPrompt,
                                     &savedCustomPrompt,
                                     &savedCustomContextPrompt,
                                     &savedLargeModelApiEndpoint,
                                     &savedLargeModelApiKey,
                                     &savedMaxChunkLines,
                                     &savedMaxChunkChars,
                                     &savedStrictOutput)) {
            return false;
        }

        translationPresetCombo->setCurrentText(savedPreset);
        translationCustomPromptBox->setChecked(savedEnableCustomPrompt);
        translationCustomPromptEdit->setPlainText(savedCustomPrompt);
        translationCustomContextPromptEdit->setPlainText(savedCustomContextPrompt);
        translationLargeApiEdit->setText(savedLargeModelApiEndpoint);
        translationLargeApiKeyEdit->setText(savedLargeModelApiKey);
        translationMaxChunkLinesSpin->setValue(savedMaxChunkLines);
        translationMaxChunkCharsSpin->setValue(savedMaxChunkChars);
        translationStrictOutputBox->setChecked(savedStrictOutput);
        return true;
    };

    QObject::connect(saveDefaultsButton, &QPushButton::clicked, &dialog, [this, translationPresetCombo, translationCustomPromptBox, translationCustomPromptEdit, translationCustomContextPromptEdit, translationLargeApiEdit, translationLargeApiKeyEdit, translationMaxChunkLinesSpin, translationMaxChunkCharsSpin, translationStrictOutputBox]() {
        saveTranslationDefaults(
            translationPresetCombo->currentText(),
            translationCustomPromptBox->isChecked(),
            translationCustomPromptEdit->toPlainText(),
            translationCustomContextPromptEdit->toPlainText(),
            translationLargeApiEdit->text().trimmed(),
            translationLargeApiKeyEdit->text().trimmed(),
            translationMaxChunkLinesSpin->value(),
            translationMaxChunkCharsSpin->value(),
            translationStrictOutputBox->isChecked());
        showInfo(QStringLiteral("默认设置已保存"), QStringLiteral("当前翻译设置已保存为默认配置。"));
    });

    QObject::connect(restoreDefaultsButton, &QPushButton::clicked, &dialog, [this, &restoreTranslationDefaultsToControls]() {
        if (restoreTranslationDefaultsToControls()) {
            showInfo(QStringLiteral("默认设置已恢复"), QStringLiteral("已从默认配置恢复翻译设置。"));
        } else {
            showInfo(QStringLiteral("未找到默认设置"), QStringLiteral("当前尚未保存任何默认翻译设置。"));
        }
    });

    QObject::connect(translationCustomPromptBox, &QCheckBox::toggled, &dialog, [translationCustomPromptEdit, translationCustomContextPromptEdit](bool enabled) {
        translationCustomPromptEdit->setEnabled(enabled);
        translationCustomContextPromptEdit->setEnabled(enabled);
    });
    translationCustomPromptEdit->setEnabled(translationEnableCustomPrompt);
    translationCustomContextPromptEdit->setEnabled(translationEnableCustomPrompt);

    QObject::connect(translationPresetCombo, &QComboBox::currentTextChanged, &dialog,
        [translationCustomPromptEdit, translationCustomContextPromptEdit, translationLargeApiEdit, translationMaxChunkLinesSpin, translationMaxChunkCharsSpin, translationStrictOutputBox](const QString &preset) {
            if (preset == QStringLiteral("DeepSeek v3.2")) {
                translationCustomPromptEdit->setPlainText(QStringLiteral("你是专业翻译。请将下面的原文翻译成中文，只输出译文，不要解释，不要附加说明，不要保留原文。\n原文：\n%1"));
                translationCustomContextPromptEdit->setPlainText(QStringLiteral("你是专业翻译。请结合上下文，将标记为[目标]的这一行翻译成中文。要求：\n1. 只输出目标行的中文译文。\n2. 不要解释，不要附加说明，不要重复原文。\n3. 参考上下文统一代词、时态、语气和术语。\n4. 如果上下文不足，就按目标行原意自然翻译。\n5. 对长段落保持流畅，避免句子碎片化。\n上下文：\n%2"));
                translationMaxChunkLinesSpin->setValue(6);
                translationMaxChunkCharsSpin->setValue(14000);
                translationStrictOutputBox->setChecked(true);
            }
        });

    if (translationModeBox->currentData().toBool()) {
        QTimer::singleShot(0, &dialog, refreshOllamaModels);
    }

    auto createAppearancePage = [&](const QString &title,
                                    AnnotatedTextEdit::TextAppearance &appearance,
                                    bool showMargins) {
        auto *page = new QWidget(tabs);
        auto *pageLayout = new QVBoxLayout(page);
        auto *appearanceGroup = new QGroupBox(title, page);
        auto *formLayout = new QFormLayout(appearanceGroup);

        auto *fontButton = new QPushButton(appearanceGroup);
        auto *textColorButton = new QPushButton(appearanceGroup);
        auto *backgroundColorButton = new QPushButton(appearanceGroup);
        auto *boldBox = new QCheckBox(QStringLiteral("加粗"), appearanceGroup);
        auto *italicBox = new QCheckBox(QStringLiteral("斜体"), appearanceGroup);
        auto *underlineBox = new QCheckBox(QStringLiteral("下划线"), appearanceGroup);
        auto *styleRow = new QWidget(appearanceGroup);
        auto *styleRowLayout = new QHBoxLayout(styleRow);
        styleRowLayout->setContentsMargins(0, 0, 0, 0);
        styleRowLayout->addWidget(boldBox);
        styleRowLayout->addWidget(italicBox);
        styleRowLayout->addWidget(underlineBox);
        styleRowLayout->addStretch(1);
        textColorButton->setObjectName(QStringLiteral("settingsColorButton"));
        backgroundColorButton->setObjectName(QStringLiteral("settingsColorButton"));

        auto *marginRow = new QWidget(appearanceGroup);
        auto *marginLayout = new QHBoxLayout(marginRow);
        marginLayout->setContentsMargins(0, 0, 0, 0);
        auto *leftMarginBox = new QSpinBox(marginRow);
        auto *topMarginBox = new QSpinBox(marginRow);
        auto *bottomMarginBox = new QSpinBox(marginRow);
        leftMarginBox->setRange(0, 120);
        topMarginBox->setRange(0, 32);
        bottomMarginBox->setRange(0, 32);
        leftMarginBox->setSuffix(QStringLiteral(" 左缩进"));
        topMarginBox->setSuffix(QStringLiteral(" 上"));
        bottomMarginBox->setSuffix(QStringLiteral(" 下"));
        leftMarginBox->setValue(appearance.leftMargin);
        topMarginBox->setValue(appearance.topMargin);
        bottomMarginBox->setValue(appearance.bottomMargin);
        marginLayout->addWidget(leftMarginBox);
        marginLayout->addWidget(topMarginBox);
        marginLayout->addWidget(bottomMarginBox);

        auto updateFontButton = [&appearance, fontButton]() {
            fontButton->setText(QStringLiteral("%1, %2 pt").arg(appearance.family).arg(appearance.pointSize));
        };
        auto *previewLabel = new QLabel(QStringLiteral("示例文本 Preview 123"), appearanceGroup);
        previewLabel->setMinimumHeight(92);
        previewLabel->setAlignment(Qt::AlignCenter);
        previewLabel->setFrameShape(QFrame::StyledPanel);

        auto updatePreview = [&appearance, previewLabel]() {
            QFont previewFont(appearance.family, appearance.pointSize);
            previewFont.setWeight(static_cast<QFont::Weight>(appearance.fontWeight));
            previewFont.setItalic(appearance.italic);
            previewFont.setUnderline(appearance.underline);
            previewLabel->setFont(previewFont);
            previewLabel->setStyleSheet(
                QStringLiteral("QLabel { border:1px solid #d0dae5; border-radius:12px; background:%1; color:%2; padding:%3px 14px %4px 14px; }")
                    .arg(appearance.backgroundColor.name(QColor::HexRgb),
                         appearance.textColor.name(QColor::HexRgb),
                         QString::number(qMax(6, 10 + appearance.topMargin)),
                         QString::number(qMax(6, 10 + appearance.bottomMargin))));
        };

        boldBox->setChecked(appearance.fontWeight >= QFont::DemiBold);
        italicBox->setChecked(appearance.italic);
        underlineBox->setChecked(appearance.underline);
        updateFontButton();
        updateColorButton(textColorButton, appearance.textColor);
        updateColorButton(backgroundColorButton, appearance.backgroundColor);
        updatePreview();

        QObject::connect(fontButton, &QPushButton::clicked, &dialog, [&dialog, &appearance, updateFontButton, updatePreview]() {
            bool ok = false;
            QFont currentFont(appearance.family, appearance.pointSize);
            currentFont.setWeight(static_cast<QFont::Weight>(appearance.fontWeight));
            currentFont.setItalic(appearance.italic);
            currentFont.setUnderline(appearance.underline);
            const QFont font = QFontDialog::getFont(&ok, currentFont, &dialog, QStringLiteral("选择字体"));
            if (!ok) {
                return;
            }

            appearance.family = font.family();
            appearance.pointSize = qMax(8, font.pointSize());
            appearance.fontWeight = font.weight();
            appearance.italic = font.italic();
            appearance.underline = font.underline();
            updateFontButton();
            updatePreview();
        });
        QObject::connect(textColorButton, &QPushButton::clicked, &dialog, [&dialog, &appearance, textColorButton, updateColorButton, updatePreview]() {
            const QColor color = QColorDialog::getColor(appearance.textColor, &dialog, QStringLiteral("选择文字颜色"));
            if (!color.isValid()) {
                return;
            }
            appearance.textColor = color;
            updateColorButton(textColorButton, appearance.textColor);
            updatePreview();
        });
        QObject::connect(backgroundColorButton, &QPushButton::clicked, &dialog, [&dialog, &appearance, backgroundColorButton, updateColorButton, updatePreview]() {
            const QColor color = QColorDialog::getColor(appearance.backgroundColor, &dialog, QStringLiteral("选择背景颜色"));
            if (!color.isValid()) {
                return;
            }
            appearance.backgroundColor = color;
            updateColorButton(backgroundColorButton, appearance.backgroundColor);
            updatePreview();
        });
        QObject::connect(boldBox, &QCheckBox::toggled, &dialog, [&appearance, updatePreview](bool checked) {
            appearance.fontWeight = checked ? QFont::DemiBold : QFont::Normal;
            updatePreview();
        });
        QObject::connect(italicBox, &QCheckBox::toggled, &dialog, [&appearance, updatePreview](bool checked) {
            appearance.italic = checked;
            updatePreview();
        });
        QObject::connect(underlineBox, &QCheckBox::toggled, &dialog, [&appearance, updatePreview](bool checked) {
            appearance.underline = checked;
            updatePreview();
        });
        QObject::connect(leftMarginBox, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&appearance, updatePreview](int value) {
            appearance.leftMargin = value;
            updatePreview();
        });
        QObject::connect(topMarginBox, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&appearance, updatePreview](int value) {
            appearance.topMargin = value;
            updatePreview();
        });
        QObject::connect(bottomMarginBox, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&appearance, updatePreview](int value) {
            appearance.bottomMargin = value;
            updatePreview();
        });

        formLayout->addRow(QStringLiteral("字体"), fontButton);
        formLayout->addRow(QStringLiteral("字形"), styleRow);
        formLayout->addRow(QStringLiteral("文字颜色"), textColorButton);
        formLayout->addRow(QStringLiteral("背景颜色"), backgroundColorButton);
        if (showMargins) {
            formLayout->addRow(QStringLiteral("边距"), marginRow);
        }

        pageLayout->addWidget(appearanceGroup);
        pageLayout->addWidget(previewLabel);
        pageLayout->addStretch(1);
        return page;
    };

    tabs->addTab(wrapSettingsPage(createAppearancePage(QStringLiteral("原文默认样式"), sourceAppearance, false)), QStringLiteral("原文"));
    tabs->addTab(wrapSettingsPage(createAppearancePage(QStringLiteral("注释默认样式"), commentAppearance, true)), QStringLiteral("注释"));

    auto *highlightPage = new QWidget(tabs);
    auto *highlightLayout = new QVBoxLayout(highlightPage);
    auto *highlightGroup = new QGroupBox(QStringLiteral("带注释原文高亮"), highlightPage);
    auto *highlightForm = new QFormLayout(highlightGroup);
    auto *highlightEnabledBox = new QCheckBox(QStringLiteral("启用注释源行高亮"), highlightGroup);
    auto *highlightFullWidthBox = new QCheckBox(QStringLiteral("整行高亮（关闭时仅高亮文本区域）"), highlightGroup);
    auto *highlightFillButton = new QPushButton(highlightGroup);
    auto *highlightBorderButton = new QPushButton(highlightGroup);
    auto *highlightPaddingBox = new QSpinBox(highlightGroup);
    highlightFillButton->setObjectName(QStringLiteral("settingsColorButton"));
    highlightBorderButton->setObjectName(QStringLiteral("settingsColorButton"));
    highlightEnabledBox->setChecked(sourceAppearance.highlightAnnotatedLines);
    highlightFullWidthBox->setChecked(sourceAppearance.highlightFullWidth);
    highlightPaddingBox->setRange(0, 32);
    highlightPaddingBox->setSuffix(QStringLiteral(" px"));
    highlightPaddingBox->setValue(sourceAppearance.annotatedLinePadding);
    updateColorButton(highlightFillButton, sourceAppearance.annotatedLineColor);
    updateColorButton(highlightBorderButton, sourceAppearance.annotatedLineBorderColor);
    highlightFillButton->setEnabled(sourceAppearance.highlightAnnotatedLines);
    highlightBorderButton->setEnabled(sourceAppearance.highlightAnnotatedLines);
    highlightFullWidthBox->setEnabled(sourceAppearance.highlightAnnotatedLines);
    highlightPaddingBox->setEnabled(sourceAppearance.highlightAnnotatedLines);

    auto *highlightPreview = new QLabel(QStringLiteral("这里预览带注释源行的高亮效果"), highlightGroup);
    highlightPreview->setMinimumHeight(96);
    highlightPreview->setAlignment(Qt::AlignCenter);
    auto updateHighlightPreview = [&sourceAppearance, highlightPreview]() {
        const QColor fill = sourceAppearance.highlightAnnotatedLines ? sourceAppearance.annotatedLineColor : sourceAppearance.backgroundColor;
        const QColor border = sourceAppearance.highlightAnnotatedLines ? sourceAppearance.annotatedLineBorderColor : QColor(QStringLiteral("#d0dae5"));
        highlightPreview->setStyleSheet(
            QStringLiteral("QLabel { border:2px solid %1; border-radius:12px; background:%2; color:#172432; padding:12px %3px; }")
                .arg(border.name(QColor::HexRgb), fill.name(QColor::HexArgb), QString::number(12 + sourceAppearance.annotatedLinePadding)));
        highlightPreview->setText(sourceAppearance.highlightFullWidth
                                      ? QStringLiteral("整行高亮预览")
                                      : QStringLiteral("文本区域高亮预览"));
    };
    updateHighlightPreview();

    QObject::connect(highlightEnabledBox, &QCheckBox::toggled, &dialog, [&sourceAppearance, highlightFillButton, highlightBorderButton, highlightFullWidthBox, highlightPaddingBox, updateHighlightPreview](bool checked) {
        sourceAppearance.highlightAnnotatedLines = checked;
        highlightFillButton->setEnabled(checked);
        highlightBorderButton->setEnabled(checked);
        highlightFullWidthBox->setEnabled(checked);
        highlightPaddingBox->setEnabled(checked);
        updateHighlightPreview();
    });
    QObject::connect(highlightFullWidthBox, &QCheckBox::toggled, &dialog, [&sourceAppearance, updateHighlightPreview](bool checked) {
        sourceAppearance.highlightFullWidth = checked;
        updateHighlightPreview();
    });
    QObject::connect(highlightPaddingBox, qOverload<int>(&QSpinBox::valueChanged), &dialog, [&sourceAppearance, updateHighlightPreview](int value) {
        sourceAppearance.annotatedLinePadding = value;
        updateHighlightPreview();
    });
    QObject::connect(highlightFillButton, &QPushButton::clicked, &dialog, [&dialog, &sourceAppearance, highlightFillButton, updateColorButton, updateHighlightPreview]() {
        const QColor color = QColorDialog::getColor(sourceAppearance.annotatedLineColor, &dialog, QStringLiteral("选择注释源行高亮填充色"));
        if (!color.isValid()) {
            return;
        }
        sourceAppearance.annotatedLineColor = color;
        updateColorButton(highlightFillButton, sourceAppearance.annotatedLineColor);
        updateHighlightPreview();
    });
    QObject::connect(highlightBorderButton, &QPushButton::clicked, &dialog, [&dialog, &sourceAppearance, highlightBorderButton, updateColorButton, updateHighlightPreview]() {
        const QColor color = QColorDialog::getColor(sourceAppearance.annotatedLineBorderColor, &dialog, QStringLiteral("选择注释源行高亮边框色"));
        if (!color.isValid()) {
            return;
        }
        sourceAppearance.annotatedLineBorderColor = color;
        updateColorButton(highlightBorderButton, sourceAppearance.annotatedLineBorderColor);
        updateHighlightPreview();
    });
    highlightForm->addRow(highlightEnabledBox);
    highlightForm->addRow(highlightFullWidthBox);
    highlightForm->addRow(QStringLiteral("填充颜色"), highlightFillButton);
    highlightForm->addRow(QStringLiteral("边框颜色"), highlightBorderButton);
    highlightForm->addRow(QStringLiteral("左右留白"), highlightPaddingBox);
    highlightLayout->addWidget(highlightGroup);
    highlightLayout->addWidget(highlightPreview);
    highlightLayout->addStretch(1);
    tabs->addTab(wrapSettingsPage(highlightPage), QStringLiteral("高亮"));

    struct ShortcutEditor {
        QString id;
        QString label;
        QKeySequenceEdit *editor;
    };
    QVector<ShortcutEditor> shortcutEditors;
    auto *shortcutPage = new QWidget(tabs);
    auto *shortcutLayout = new QVBoxLayout(shortcutPage);
    shortcutLayout->setSpacing(14);
    const QVector<ShortcutBinding> bindings = shortcutBindings();
    const QStringList sections = { QStringLiteral("文件"), QStringLiteral("编辑"), QStringLiteral("批注"), QStringLiteral("界面") };
    for (const QString &section : sections) {
        auto *group = new QGroupBox(section, shortcutPage);
        auto *groupLayout = new QFormLayout(group);
        groupLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        bool hasAnyBinding = false;
        for (const ShortcutBinding &binding : bindings) {
            if (binding.section != section || !binding.action) {
                continue;
            }

            hasAnyBinding = true;
            auto *editor = new QKeySequenceEdit(binding.action->shortcut(), group);
            editor->setClearButtonEnabled(true);
            editor->setToolTip(QStringLiteral("默认：%1").arg(binding.defaultSequence.toString(QKeySequence::NativeText)));
            shortcutEditors.append(ShortcutEditor { binding.id, binding.label, editor });
            groupLayout->addRow(binding.label, editor);
        }
        if (hasAnyBinding) {
            shortcutLayout->addWidget(group);
        } else {
            delete group;
        }
    }
    auto *shortcutHint = new QLabel(QStringLiteral("快捷键会作用于整个窗口。留空表示禁用该快捷键。建议避免重复设置相同组合键。"), shortcutPage);
    shortcutHint->setWordWrap(true);
    shortcutHint->setStyleSheet(QStringLiteral("color:#546173;"));
    shortcutLayout->addWidget(shortcutHint);

    auto *shortcutButtonRow = new QHBoxLayout;
    shortcutButtonRow->addStretch(1);
    auto *resetShortcutsButton = new QPushButton(QStringLiteral("恢复默认快捷键"), shortcutPage);
    shortcutButtonRow->addWidget(resetShortcutsButton);
    shortcutLayout->addLayout(shortcutButtonRow);
    shortcutLayout->addStretch(1);
    QObject::connect(resetShortcutsButton, &QPushButton::clicked, &dialog, [bindings, shortcutEditors]() {
        for (const ShortcutEditor &item : shortcutEditors) {
            for (const ShortcutBinding &binding : bindings) {
                if (binding.id == item.id) {
                    item.editor->setKeySequence(binding.defaultSequence);
                    break;
                }
            }
        }
    });
    tabs->addTab(wrapSettingsPage(shortcutPage), QStringLiteral("快捷键"));

    int tabIndex = 0;
    tabIndex = qBound(0, initialPage, tabs->count() - 1);
    tabs->setCurrentIndex(tabIndex);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    dialogLayout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QStringList duplicateMessages;
    QHash<QString, QStringList> shortcutConflicts;
    for (const ShortcutEditor &item : shortcutEditors) {
        const QString portable = item.editor->keySequence().toString(QKeySequence::PortableText);
        if (portable.isEmpty()) {
            continue;
        }
        shortcutConflicts[portable].append(item.label);
    }
    for (auto it = shortcutConflicts.constBegin(); it != shortcutConflicts.constEnd(); ++it) {
        if (it.value().size() > 1) {
            duplicateMessages.append(QStringLiteral("%1：%2").arg(it.key(), it.value().join(QStringLiteral("、"))));
        }
    }
    if (!duplicateMessages.isEmpty()) {
        QMessageBox::warning(
            &dialog,
            QStringLiteral("快捷键冲突"),
            QStringLiteral("以下快捷键重复，请调整后再保存：\n%1").arg(duplicateMessages.join(QChar('\n'))));
        return;
    }

    const QString previousSourceAppearance = appearanceToSettingsString(editor->sourceAppearance());
    const QString previousCommentAppearance = appearanceToSettingsString(editor->commentAppearance());
    const int previousDocumentMargin = editorDocumentMargin;
    const int previousTabStopDistance = editorTabStopDistance;
    const bool previousWrapEnabled = editorWrapEnabled;

    rememberWindowLayout = rememberWindowLayoutBox->isChecked();
    autosaveIntervalMs = autosaveIntervalBox->value() * 1000;
    autosaveTimer->setInterval(autosaveIntervalMs);
    translationUseOllama = translationModeBox->currentData().toBool();
    translationFallbackToOnline = translationFallbackBox->isChecked();
    translationDisableThinking = translationDisableThinkingBox->isChecked();
    translationEnableCustomPrompt = translationCustomPromptBox->isChecked();
    translationCustomPromptTemplate = translationCustomPromptEdit->toPlainText();
    translationCustomContextPromptTemplate = translationCustomContextPromptEdit->toPlainText();
    translationLargeModelApiEndpoint = translationLargeApiEdit->text().trimmed();
    translationLargeModelApiKey = translationLargeApiKeyEdit->text().trimmed();
    translationConfigPreset = translationPresetCombo->currentText();
    translationMaxChunkTargetLines = translationMaxChunkLinesSpin->value();
    translationMaxChunkChars = translationMaxChunkCharsSpin->value();
    translationStrictOutputParsing = translationStrictOutputBox->isChecked();
    translationLocalStrategy = static_cast<LocalTranslationStrategy>(translationStrategyBox->currentData().toInt());
    translationOllamaEndpoint = ollamaEndpointEdit->text().trimmed();
    translationOllamaModel = ollamaModelBox->currentText().trimmed();
    translationContextRadius = translationContextBox->value();
    translationTimeoutMs = translationTimeoutBox->value() * 1000;
    if (translationOllamaEndpoint.isEmpty()) {
        translationOllamaEndpoint = QStringLiteral("http://127.0.0.1:11434");
    }
    if (translationOllamaModel.isEmpty()) {
        translationOllamaModel = QStringLiteral("qwen3:14b-q4_K_M");
    }
    editorDocumentMargin = documentMarginBox->value();
    editorTabStopDistance = tabStopBox->value();
    editorWrapEnabled = wrapEnabledBox->isChecked();

    const bool newAutosaveEnabled = autosaveEnabledBox->isChecked();
    {
        const QSignalBlocker blocker(autosaveCheckBox);
        autosaveCheckBox->setChecked(newAutosaveEnabled);
    }
    setAutosaveEnabled(newAutosaveEnabled);

    const bool newFocusMode = startupFocusModeBox->isChecked();
    {
        const QSignalBlocker blocker(focusModeAction);
        focusModeAction->setChecked(newFocusMode);
    }
    updateFocusMode(newFocusMode);

    defaultSourceAppearance = sourceAppearance;
    defaultCommentAppearance = commentAppearance;
    const bool sourceAppearanceChanged = previousSourceAppearance != appearanceToSettingsString(sourceAppearance);
    const bool commentAppearanceChanged = previousCommentAppearance != appearanceToSettingsString(commentAppearance);
    const bool uiChanged = previousDocumentMargin != editorDocumentMargin
                           || previousTabStopDistance != editorTabStopDistance
                           || previousWrapEnabled != editorWrapEnabled;

    editor->setUpdatesEnabled(false);
    editor->viewport()->setUpdatesEnabled(false);
    if (uiChanged) {
        applyEditorUiPreferences();
    }
    if (sourceAppearanceChanged) {
        editor->setSourceAppearance(sourceAppearance);
    }
    if (commentAppearanceChanged) {
        editor->setCommentAppearance(commentAppearance);
    }
    editor->viewport()->setUpdatesEnabled(true);
    editor->setUpdatesEnabled(true);
    editor->viewport()->update();
    for (const ShortcutEditor &item : shortcutEditors) {
        for (const ShortcutBinding &binding : bindings) {
            if (binding.id == item.id && binding.action) {
                binding.action->setShortcut(item.editor->keySequence());
                binding.action->setShortcutContext(binding.id == QStringLiteral("focusMode") ? Qt::ApplicationShortcut : Qt::WindowShortcut);
                break;
            }
        }
    }

    const bool appearanceChanged = sourceAppearanceChanged || commentAppearanceChanged;
    if (appearanceChanged) {
        editor->document()->setModified(true);
        scheduleAutosave();
    }
    if (uiChanged) {
        statusBar()->showMessage(QStringLiteral("编辑器界面设置已更新"), 2500);
    }

    savePersistentState();
    refreshInspectorPanel();
    refreshWindowTitle();
}

bool MainWindow::saveDocumentAs()
{
    const QString defaultPath = currentFilePath.isEmpty() ? QStringLiteral("document.tpx") : currentFilePath;
    QString selectedFilter = saveFilterForFormat(currentFormat == DocumentFormat::Unknown ? DocumentFormat::Trx : currentFormat);
    const QString rawFilePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("另存为"),
        defaultPath,
        QStringLiteral("TPX Documents (*.tpx);;Text Files (*.txt);;Word Documents (*.docx)"),
        &selectedFilter);

    if (rawFilePath.isEmpty()) {
        return false;
    }

    DocumentFormat format = formatFromPath(rawFilePath);
    if (format == DocumentFormat::Unknown) {
        format = formatFromSelectedFilter(selectedFilter);
    }

    const QString filePath = normalizeSavePath(rawFilePath, format, selectedFilter);
    format = formatFromPath(filePath);
    ExportContentMode exportMode = ExportContentMode::SourceOnly;
    bool accepted = true;
    if (format == DocumentFormat::Txt || format == DocumentFormat::Docx) {
        exportMode = chooseExportContentMode(format, &accepted);
        if (!accepted) {
            return false;
        }
    }

    return saveToPath(filePath, format, false, exportMode);
}

bool MainWindow::exportDocument(DocumentFormat forcedFormat)
{
    QString suggested = currentFilePath;
    if (suggested.isEmpty()) {
        switch (forcedFormat) {
        case DocumentFormat::Txt:
            suggested = QStringLiteral("document.txt");
            break;
        case DocumentFormat::Docx:
            suggested = QStringLiteral("document.docx");
            break;
        case DocumentFormat::Trx:
            suggested = QStringLiteral("document.tpx");
            break;
        case DocumentFormat::Unknown:
            break;
        }
    }

    if (forcedFormat == DocumentFormat::Unknown) {
        return false;
    }

    QString selectedFilter = saveFilterForFormat(forcedFormat);
    const QString rawFilePath = QFileDialog::getSaveFileName(
        this,
        QStringLiteral("导出文档"),
        suggested,
        selectedFilter,
        &selectedFilter);
    if (rawFilePath.isEmpty()) {
        return false;
    }

    const QString filePath = normalizeSavePath(rawFilePath, forcedFormat, selectedFilter);
    ExportContentMode exportMode = ExportContentMode::SourceOnly;
    bool accepted = true;
    if (forcedFormat == DocumentFormat::Txt || forcedFormat == DocumentFormat::Docx) {
        exportMode = chooseExportContentMode(forcedFormat, &accepted);
        if (!accepted) {
            return false;
        }
    }

    return saveToPath(filePath, forcedFormat, false, exportMode);
}

bool MainWindow::saveToPath(const QString &filePath, DocumentFormat format, bool isAutosave, ExportContentMode exportMode)
{
    bool ok = false;
    switch (format) {
    case DocumentFormat::Txt:
        ok = saveTxtFile(filePath, exportMode);
        break;
    case DocumentFormat::Docx:
        ok = saveDocxFile(filePath, exportMode);
        break;
    case DocumentFormat::Trx:
        ok = saveTrxFile(filePath, isAutosave);
        break;
    case DocumentFormat::Unknown:
        showError(QStringLiteral("无法保存"), QStringLiteral("不支持该文件格式。"));
        return false;
    }

    if (!ok) {
        return false;
    }

    if (isAutosave) {
        autosaveValueLabel->setText(QStringLiteral("已自动保存 %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
        statusBar()->showMessage(QStringLiteral("自动保存已更新"), 2500);
        return true;
    }

    lastSavedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    setCurrentFile(filePath, format);
    editor->document()->setModified(false);
    addRecentFile(filePath);
    clearAutosaveSnapshot();
    refreshDocumentStats();
    statusBar()->showMessage(QStringLiteral("已保存到 %1").arg(QFileInfo(filePath).fileName()), 3500);
    return true;
}

bool MainWindow::loadFromPath(const QString &filePath)
{
    const DocumentFormat format = formatFromPath(filePath);
    bool ok = false;

    switch (format) {
    case DocumentFormat::Txt:
        ok = loadTxtFile(filePath);
        break;
    case DocumentFormat::Docx:
        ok = loadDocxFile(filePath);
        break;
    case DocumentFormat::Trx:
        ok = loadTrxFile(filePath);
        break;
    case DocumentFormat::Unknown:
        showError(QStringLiteral("无法打开"), QStringLiteral("仅支持 .txt、.docx、.tpx（兼容旧 .trx）文件。"));
        return false;
    }

    if (!ok) {
        return false;
    }

    setCurrentFile(filePath, format);
    editor->document()->setModified(false);
    addRecentFile(filePath);
    resetTransientUiState();
    chapterEntries.clear();
    chapterList->clear();
    rebuildChapterIndex();
    refreshDocumentStats();
    statusBar()->showMessage(QStringLiteral("已打开 %1").arg(QFileInfo(filePath).fileName()), 3500);
    return true;
}

bool MainWindow::loadTxtFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError(QStringLiteral("打开失败"), file.errorString());
        return false;
    }

    applyConfiguredEditorDefaults();
    const QString text = QString::fromUtf8(file.readAll());
    if (!writePlainTextToEditor(text)) {
        return false;
    }
    setCreatedAtUtc(createdAtUtc());
    lastSavedAtUtc.clear();
    return true;
}

bool MainWindow::saveTxtFile(const QString &filePath, ExportContentMode exportMode)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        showError(QStringLiteral("保存失败"), file.errorString());
        return false;
    }

    file.write(buildExportText(exportMode).toUtf8());

    if (!file.commit()) {
        showError(QStringLiteral("保存失败"), file.errorString());
        return false;
    }
    return true;
}

bool MainWindow::loadTrxFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(QStringLiteral("打开失败"), file.errorString());
        return false;
    }

    const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
    const QJsonObject root = json.object();
    const QString magic = root.value(QStringLiteral("magic")).toString();
    if (magic != tpxPayloadMagic() && magic != legacyTrxPayloadMagic()) {
        showError(QStringLiteral("格式错误"), QStringLiteral("该 TPX 文件不是当前编辑器可识别的格式。"));
        return false;
    }

    const QByteArray compressed = QByteArray::fromBase64(root.value(QStringLiteral("payload")).toString().toUtf8());
    const QByteArray plain = qUncompress(compressed);
    const int expectedChars = root.value(QStringLiteral("metadata")).toObject().value(QStringLiteral("charCount")).toInt(-1);
    if (compressed.isEmpty() || (plain.isEmpty() && expectedChars > 0)) {
        showError(QStringLiteral("读取失败"), QStringLiteral("TPX 内容损坏或无法解压。"));
        return false;
    }

    const QJsonObject metadata = root.value(QStringLiteral("metadata")).toObject();
    setCreatedAtUtc(metadata.value(QStringLiteral("createdAtUtc")).toString(createdAtUtc()));
    lastSavedAtUtc = metadata.value(QStringLiteral("updatedAtUtc")).toString();
    if (!writePlainTextToEditor(QString::fromUtf8(plain))) {
        return false;
    }

    editor->setSourceAppearance(appearanceFromJson(metadata.value(QStringLiteral("sourceAppearance")).toObject(), editor->sourceAppearance()));
    editor->setCommentAppearance(appearanceFromJson(metadata.value(QStringLiteral("commentAppearance")).toObject(), editor->commentAppearance()));

    QList<AnnotatedTextEdit::CommentEntry> entries;
    const QJsonArray commentArray = root.value(QStringLiteral("comments")).toArray();
    entries.reserve(commentArray.size());
    for (const QJsonValue &value : commentArray) {
        const QJsonObject object = value.toObject();
        entries.append(AnnotatedTextEdit::CommentEntry {
            object.value(QStringLiteral("lineNumber")).toInt(),
            object.value(QStringLiteral("visualLineIndex")).toInt(0),
            object.value(QStringLiteral("sourceText")).toString(),
            object.value(QStringLiteral("html")).toString(),
            object.value(QStringLiteral("collapsed")).toBool(false),
        });
    }
    editor->loadCommentEntries(entries);
    refreshCommentManager();
    return true;
}

bool MainWindow::saveTrxFile(const QString &filePath, bool isAutosave)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        showError(QStringLiteral("保存失败"), file.errorString());
        return false;
    }

    const QString plainText = readPlainTextFromEditor();
    const QByteArray compressed = qCompress(plainText.toUtf8(), 9);
    const QDateTime nowUtc = QDateTime::currentDateTimeUtc();

    QJsonObject metadata;
    metadata.insert(QStringLiteral("title"), buildTrxTitle());
    metadata.insert(QStringLiteral("theme"), buildTrxThemeName());
    metadata.insert(QStringLiteral("createdAtUtc"), documentCreatedAtUtc);
    metadata.insert(QStringLiteral("updatedAtUtc"), nowUtc.toString(Qt::ISODate));
    metadata.insert(QStringLiteral("charCount"), plainText.size());
    metadata.insert(QStringLiteral("lineCount"), qMax(1, editor->lineCount()));
    metadata.insert(QStringLiteral("sessionId"), sessionId);
    metadata.insert(QStringLiteral("autosave"), isAutosave);
    metadata.insert(QStringLiteral("sourceAppearance"), appearanceToJson(editor->sourceAppearance()));
    metadata.insert(QStringLiteral("commentAppearance"), appearanceToJson(editor->commentAppearance()));

    QJsonArray chapters;
    for (const ChapterEntry &entry : chapterEntries) {
        QJsonObject chapter;
        chapter.insert(QStringLiteral("title"), entry.title);
        chapter.insert(QStringLiteral("blockNumber"), entry.blockNumber);
        chapters.append(chapter);
    }
    metadata.insert(QStringLiteral("chapters"), chapters);

    QJsonObject root;
    QJsonArray comments;
    for (const AnnotatedTextEdit::CommentEntry &entry : editor->commentEntries()) {
        QJsonObject comment;
        comment.insert(QStringLiteral("lineNumber"), entry.lineNumber);
        comment.insert(QStringLiteral("visualLineIndex"), entry.visualLineIndex);
        comment.insert(QStringLiteral("sourceText"), entry.sourceText);
        comment.insert(QStringLiteral("html"), entry.html);
        comment.insert(QStringLiteral("collapsed"), entry.collapsed);
        comments.append(comment);
    }

    root.insert(QStringLiteral("magic"), tpxPayloadMagic());
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("metadata"), metadata);
    root.insert(QStringLiteral("payload"), QString::fromUtf8(compressed.toBase64()));
    root.insert(QStringLiteral("comments"), comments);

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        showError(QStringLiteral("保存失败"), file.errorString());
        return false;
    }
    return true;
}

bool MainWindow::loadDocxFile(const QString &filePath)
{
    QString errorMessage;
    const QString xml = extractDocxDocumentXml(filePath, &errorMessage);
    if (xml.isEmpty()) {
        showError(QStringLiteral("DOCX 打开失败"), errorMessage.isEmpty() ? QStringLiteral("无法读取 DOCX 内容。") : errorMessage);
        return false;
    }

    setCreatedAtUtc(createdAtUtc());
    lastSavedAtUtc.clear();
    applyConfiguredEditorDefaults();
    return writePlainTextToEditor(stripDocxXml(xml));
}

bool MainWindow::saveDocxFile(const QString &filePath, ExportContentMode exportMode)
{
    return createDocxArchive(filePath, buildDocxDocumentXml(buildExportText(exportMode)));
}

bool MainWindow::writePlainTextToEditor(const QString &text)
{
    suppressDocumentRefresh = true;
    editor->setUpdatesEnabled(false);
    editor->setUndoRedoEnabled(false);
    editor->replaceSourceText(text);
    editor->setUndoRedoEnabled(true);
    editor->setUpdatesEnabled(true);
    suppressDocumentRefresh = false;
    refreshCommentManager();
    return true;
}

QString MainWindow::readPlainTextFromEditor() const
{
    return editor->sourcePlainText();
}

MainWindow::DocumentFormat MainWindow::formatFromPath(const QString &filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("txt")) {
        return DocumentFormat::Txt;
    }
    if (suffix == QStringLiteral("docx")) {
        return DocumentFormat::Docx;
    }
    if (suffix == QStringLiteral("tpx") || suffix == QStringLiteral("trx")) {
        return DocumentFormat::Trx;
    }
    return DocumentFormat::Unknown;
}

MainWindow::DocumentFormat MainWindow::formatFromSelectedFilter(const QString &selectedFilter) const
{
    const QString filter = selectedFilter.toLower();
    if (filter.contains(QStringLiteral("*.txt"))) {
        return DocumentFormat::Txt;
    }
    if (filter.contains(QStringLiteral("*.docx"))) {
        return DocumentFormat::Docx;
    }
    if (filter.contains(QStringLiteral("*.tpx")) || filter.contains(QStringLiteral("*.trx"))) {
        return DocumentFormat::Trx;
    }
    return DocumentFormat::Unknown;
}

QString MainWindow::defaultSuffixForFormat(DocumentFormat format) const
{
    switch (format) {
    case DocumentFormat::Txt:
        return QStringLiteral("txt");
    case DocumentFormat::Docx:
        return QStringLiteral("docx");
    case DocumentFormat::Trx:
        return QStringLiteral("tpx");
    case DocumentFormat::Unknown:
        return QString();
    }
    return QString();
}

QString MainWindow::saveFilterForFormat(DocumentFormat format) const
{
    switch (format) {
    case DocumentFormat::Txt:
        return QStringLiteral("Text Files (*.txt)");
    case DocumentFormat::Docx:
        return QStringLiteral("Word Documents (*.docx)");
    case DocumentFormat::Trx:
        return QStringLiteral("TPX Documents (*.tpx)");
    case DocumentFormat::Unknown:
        return QString();
    }
    return QString();
}

QString MainWindow::normalizeSavePath(const QString &filePath, DocumentFormat format, const QString &selectedFilter) const
{
    QString normalized = filePath.trimmed();
    if (normalized.isEmpty()) {
        return normalized;
    }

    const DocumentFormat resolvedFormat = format == DocumentFormat::Unknown
        ? formatFromSelectedFilter(selectedFilter)
        : format;
    const QString expectedSuffix = defaultSuffixForFormat(resolvedFormat);
    if (expectedSuffix.isEmpty()) {
        return normalized;
    }

    if (QFileInfo(normalized).suffix().compare(expectedSuffix, Qt::CaseInsensitive) == 0) {
        return normalized;
    }

    const int separatorIndex = qMax(normalized.lastIndexOf('/'), normalized.lastIndexOf('\\'));
    const int dotIndex = normalized.lastIndexOf('.');
    if (dotIndex > separatorIndex) {
        normalized = normalized.left(dotIndex + 1) + expectedSuffix;
    } else {
        normalized += QStringLiteral(".") + expectedSuffix;
    }
    return normalized;
}

QString MainWindow::formatLabel(DocumentFormat format) const
{
    switch (format) {
    case DocumentFormat::Txt:
        return QStringLiteral("TXT 纯文本");
    case DocumentFormat::Docx:
        return QStringLiteral("DOCX 文档");
    case DocumentFormat::Trx:
        return QStringLiteral("TPX 专有文档");
    case DocumentFormat::Unknown:
        return QStringLiteral("未知");
    }
    return QStringLiteral("未知");
}

QString MainWindow::stripDocxXml(const QString &xml) const
{
    QXmlStreamReader reader(xml);
    QString output;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto name = reader.name();
            if (name == QLatin1String("t")) {
                output += reader.readElementText(QXmlStreamReader::IncludeChildElements);
                continue;
            }
            if (name == QLatin1String("tab")) {
                output += QChar('\t');
                continue;
            }
            if (name == QLatin1String("br")) {
                output += QChar('\n');
                continue;
            }
        }
        if (reader.isEndElement() && reader.name() == QLatin1String("p")) {
            output += QChar('\n');
        }
    }

    while (output.endsWith('\n')) {
        output.chop(1);
    }
    return output;
}

QString MainWindow::buildDocxDocumentXml(const QString &text) const
{
    QString xml;
    xml += QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>");
    xml += QStringLiteral("<w:document xmlns:wpc=\"http://schemas.microsoft.com/office/word/2010/wordprocessingCanvas\" xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\" xmlns:o=\"urn:schemas-microsoft-com:office:office\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:m=\"http://schemas.openxmlformats.org/officeDocument/2006/math\" xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:wp14=\"http://schemas.microsoft.com/office/word/2010/wordprocessingDrawing\" xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" xmlns:w10=\"urn:schemas-microsoft-com:office:word\" xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" xmlns:w14=\"http://schemas.microsoft.com/office/word/2010/wordml\" xmlns:wpg=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\" xmlns:wpi=\"http://schemas.microsoft.com/office/word/2010/wordprocessingInk\" xmlns:wne=\"http://schemas.microsoft.com/office/2006/wordml\" xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\" mc:Ignorable=\"w14 wp14\"><w:body>");

    const QStringList paragraphs = text.split('\n');
    for (const QString &paragraph : paragraphs) {
        xml += QStringLiteral("<w:p><w:r><w:t xml:space=\"preserve\">");
        xml += escapeXmlText(paragraph);
        xml += QStringLiteral("</w:t></w:r></w:p>");
    }

    xml += QStringLiteral("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/><w:pgMar w:top=\"1440\" w:right=\"1440\" w:bottom=\"1440\" w:left=\"1440\" w:header=\"708\" w:footer=\"708\" w:gutter=\"0\"/></w:sectPr></w:body></w:document>");
    return xml;
}

bool MainWindow::createDocxArchive(const QString &filePath, const QString &documentXml)
{
    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        showError(QStringLiteral("DOCX 导出失败"), QStringLiteral("无法创建临时目录。"));
        return false;
    }

    QDir root(tempDir.path());
    root.mkpath(QStringLiteral("_rels"));
    root.mkpath(QStringLiteral("word"));

    auto writeTextFile = [&](const QString &relativePath, const QString &content) -> bool {
        QFile file(root.filePath(relativePath));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return false;
        }
        file.write(content.toUtf8());
        return true;
    };

    const QString contentTypes = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>");
    const QString rels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>");

    if (!writeTextFile(QStringLiteral("[Content_Types].xml"), contentTypes)
        || !writeTextFile(QStringLiteral("_rels/.rels"), rels)
        || !writeTextFile(QStringLiteral("word/document.xml"), documentXml)) {
        showError(QStringLiteral("DOCX 导出失败"), QStringLiteral("无法生成 DOCX 内容文件。"));
        return false;
    }

    const QString tempZip = QDir::temp().filePath(QStringLiteral("tpx-writer-%1.zip").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QString stdOut;
    QString stdErr;
    const QString script = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "$source = %1; "
        "$destination = %2; "
        "if (Test-Path $destination) { Remove-Item $destination -Force; } "
        "[System.IO.Compression.ZipFile]::CreateFromDirectory($source, $destination, [System.IO.Compression.CompressionLevel]::Optimal, $false)")
                               .arg(quoteForPowerShell(tempDir.path()), quoteForPowerShell(tempZip));

    if (!runPowerShellScript(script, &stdOut, &stdErr)) {
        showError(QStringLiteral("DOCX 导出失败"), stdErr.isEmpty() ? QStringLiteral("PowerShell 压缩步骤执行失败。") : stdErr);
        return false;
    }

    QFile::remove(filePath);
    if (!QFile::copy(tempZip, filePath)) {
        QFile::remove(tempZip);
        showError(QStringLiteral("DOCX 导出失败"), QStringLiteral("无法写入目标 DOCX 文件。"));
        return false;
    }

    QFile::remove(tempZip);
    return true;
}

QString MainWindow::extractDocxDocumentXml(const QString &filePath, QString *errorMessage) const
{
    QString stdOut;
    QString stdErr;
    const QString script = QStringLiteral(
        "Add-Type -AssemblyName System.IO.Compression.FileSystem; "
        "$archive = %1; "
        "$zip = [System.IO.Compression.ZipFile]::OpenRead($archive); "
        "try { "
        "  $entry = $zip.GetEntry('word/document.xml'); "
        "  if ($null -eq $entry) { throw 'DOCX 中缺少 word/document.xml。'; } "
        "  $reader = New-Object System.IO.StreamReader($entry.Open(), [System.Text.Encoding]::UTF8); "
        "  try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8; $reader.ReadToEnd() } "
        "  finally { $reader.Dispose(); } "
        "} finally { $zip.Dispose(); }")
                               .arg(quoteForPowerShell(filePath));

    if (!runPowerShellScript(script, &stdOut, &stdErr)) {
        if (errorMessage) {
            *errorMessage = stdErr.isEmpty() ? QStringLiteral("解压 DOCX 失败。") : stdErr;
        }
        return QString();
    }

    return stdOut;
}

bool MainWindow::runPowerShellScript(const QString &script, QString *stdOut, QString *stdErr) const
{
    QProcess process;
    process.start(QStringLiteral("powershell.exe"), {
        QStringLiteral("-NoProfile"),
        QStringLiteral("-ExecutionPolicy"),
        QStringLiteral("Bypass"),
        QStringLiteral("-Command"),
        script,
    });
    process.waitForFinished(-1);

    if (stdOut) {
        *stdOut = decodeProcessText(process.readAllStandardOutput());
    }
    if (stdErr) {
        *stdErr = decodeProcessText(process.readAllStandardError());
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void MainWindow::addRecentFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return;
    }

    QStringList files = recentFiles();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 8) {
        files.removeLast();
    }
    storeRecentFiles(files);
    refreshRecentFilesUi();
    refreshRecentFileActions();
}

QStringList MainWindow::recentFiles() const
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    const QStringList files = settings.value(QStringLiteral("recentFiles")).toStringList();
    settings.endGroup();
    return files;
}

void MainWindow::storeRecentFiles(const QStringList &files)
{
    QSettings settings;
    settings.beginGroup(appSettingsGroup());
    settings.setValue(QStringLiteral("recentFiles"), files);
    settings.endGroup();
}

void MainWindow::setAutosaveEnabled(bool enabled)
{
    autosaveEnabled = enabled;
    if (!enabled) {
        autosaveTimer->stop();
    } else if (editor->document()->isModified()) {
        scheduleAutosave();
    }
    refreshInspectorPanel();
}

void MainWindow::scheduleAutosave()
{
    if (!autosaveEnabled) {
        return;
    }
    autosaveTimer->start();
}

void MainWindow::performAutosave()
{
    if (!autosaveEnabled) {
        return;
    }
    saveTrxFile(autosaveFilePath(), true);
}

QString MainWindow::autosaveFilePath(const QString &filePath) const
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(folder);
    const QString base = filePath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(filePath).completeBaseName();
    return QDir(folder).filePath(QStringLiteral("%1.autosave.tpx").arg(base));
}

void MainWindow::clearAutosaveSnapshot(const QString &filePath)
{
    QFile::remove(filePath.isEmpty() ? autosaveFilePath() : filePath);
}

void MainWindow::showFindPanel()
{
    findDock->show();
    findDock->raise();
    findLineEdit->setFocus();
    findLineEdit->selectAll();
}

void MainWindow::hideFindPanel()
{
    findDock->hide();
}

bool MainWindow::findText(bool forward)
{
    const QString needle = findLineEdit->text();
    if (needle.isEmpty()) {
        return false;
    }

    QTextDocument::FindFlags flags;
    if (!forward) {
        flags |= QTextDocument::FindBackward;
    }

    const bool found = editor->find(needle, flags);
    if (!found) {
        statusBar()->showMessage(QStringLiteral("未找到匹配内容"), 2500);
    }
    return found;
}

void MainWindow::triggerUndo()
{
    QWidget *focus = QApplication::focusWidget();
    if (auto *annotatedEditor = qobject_cast<AnnotatedTextEdit *>(focus)) {
        annotatedEditor->undo();
        return;
    }
    if (auto *plainTextEdit = qobject_cast<QPlainTextEdit *>(focus)) {
        plainTextEdit->undo();
        return;
    }
    if (auto *lineEdit = qobject_cast<QLineEdit *>(focus)) {
        lineEdit->undo();
        return;
    }
    editor->undo();
}

void MainWindow::triggerRedo()
{
    QWidget *focus = QApplication::focusWidget();
    if (auto *annotatedEditor = qobject_cast<AnnotatedTextEdit *>(focus)) {
        annotatedEditor->redo();
        return;
    }
    if (auto *plainTextEdit = qobject_cast<QPlainTextEdit *>(focus)) {
        plainTextEdit->redo();
        return;
    }
    if (auto *lineEdit = qobject_cast<QLineEdit *>(focus)) {
        lineEdit->redo();
        return;
    }
    editor->redo();
}

void MainWindow::replaceCurrentSelection()
{
    const QString needle = findLineEdit->text();
    if (needle.isEmpty()) {
        return;
    }

    QTextCursor cursor = editor->textCursor();
    if (cursor.hasSelection() && cursor.selectedText() == needle) {
        cursor.insertText(replaceLineEdit->text());
    }
    findText(true);
}

void MainWindow::replaceAllMatches()
{
    const QString needle = findLineEdit->text();
    if (needle.isEmpty()) {
        return;
    }

    const QString replacement = replaceLineEdit->text();
    QString text = readPlainTextFromEditor();
    const int count = text.count(needle);
    if (count == 0) {
        statusBar()->showMessage(QStringLiteral("没有可替换的匹配项"), 2500);
        return;
    }

    text.replace(needle, replacement);
    writePlainTextToEditor(text);
    editor->document()->setModified(true);
    statusBar()->showMessage(QStringLiteral("已替换 %1 处内容").arg(count), 3000);
}

void MainWindow::jumpToChapterRow(int row)
{
    if (row < 0 || row >= chapterEntries.size()) {
        return;
    }

    const ChapterEntry &entry = chapterEntries[row];
    editor->goToLine(entry.blockNumber);
}

void MainWindow::jumpToManagedLineRow(int row)
{
    if (row < 0 || row >= commentLineList->count()) {
        return;
    }

    const QListWidgetItem *item = commentLineList->item(row);
    if (!item) {
        return;
    }

    editor->goToLine(item->data(Qt::UserRole).toInt());
}

void MainWindow::goToPreviousComment()
{
    const int lineNumber = adjacentCommentLine(false);
    if (lineNumber < 0) {
        statusBar()->showMessage(QStringLiteral("当前文档没有注释"), 2500);
        return;
    }

    editor->goToLine(lineNumber);
    syncCommentManagerSelection(lineNumber, false);
    statusBar()->showMessage(QStringLiteral("已跳到上一条注释"), 1800);
}

void MainWindow::goToNextComment()
{
    const int lineNumber = adjacentCommentLine(true);
    if (lineNumber < 0) {
        statusBar()->showMessage(QStringLiteral("当前文档没有注释"), 2500);
        return;
    }

    editor->goToLine(lineNumber);
    syncCommentManagerSelection(lineNumber, false);
    statusBar()->showMessage(QStringLiteral("已跳到下一条注释"), 1800);
}

void MainWindow::insertCommentForCurrentLine()
{
    if (!editor->addCommentToCurrentLine()) {
        statusBar()->showMessage(QStringLiteral("当前行已经存在注释行"), 2500);
        return;
    }

    refreshCommentManager();
    syncCommentManagerSelection(editor->currentLineNumber(), false);
    statusBar()->showMessage(QStringLiteral("已在当前行下方插入注释行"), 2500);
}

void MainWindow::batchInsertCommentsByRange()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("按行号范围批量添加批注"));
    dialog.resize(520, 360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    auto *hintLabel = new QLabel(QStringLiteral("行号只统计原文行，不包含批注行。可为整段范围批量写入同一条批注内容。"), &dialog);
    hintLabel->setObjectName(QStringLiteral("dockInfoTitle"));
    hintLabel->setWordWrap(true);

    auto *formLayout = new QFormLayout;
    auto *startSpinBox = new QSpinBox(&dialog);
    auto *endSpinBox = new QSpinBox(&dialog);
    auto *overwriteBox = new QCheckBox(QStringLiteral("覆盖已有批注"), &dialog);
    const int maxLine = qMax(1, editor->lineCount());
    const int currentLine = qBound(1, editor->currentLineNumber() + 1, maxLine);
    startSpinBox->setRange(1, maxLine);
    endSpinBox->setRange(1, maxLine);
    startSpinBox->setValue(currentLine);
    endSpinBox->setValue(currentLine);
    formLayout->addRow(QStringLiteral("起始行"), startSpinBox);
    formLayout->addRow(QStringLiteral("结束行"), endSpinBox);
    formLayout->addRow(QString(), overwriteBox);

    auto *commentEdit = new QPlainTextEdit(&dialog);
    commentEdit->setPlaceholderText(QStringLiteral("输入要批量写入到每一行的批注内容..."));
    commentEdit->setMinimumHeight(160);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(hintLabel);
    layout->addLayout(formLayout);
    layout->addWidget(commentEdit, 1);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString commentText = commentEdit->toPlainText();
    if (commentText.trimmed().isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先输入批注内容"), 2500);
        return;
    }

    const int startLine = qMin(startSpinBox->value(), endSpinBox->value()) - 1;
    const int endLine = qMax(startSpinBox->value(), endSpinBox->value()) - 1;
    const int changedCount = editor->addCommentRange(startLine, endLine, commentText, overwriteBox->isChecked());
    if (changedCount <= 0) {
        statusBar()->showMessage(QStringLiteral("所选范围内没有可新增的批注行"), 2500);
        return;
    }

    editor->goToLine(startLine);
    refreshCommentManager();
    syncCommentManagerSelection(startLine, false);
    statusBar()->showMessage(QStringLiteral("已为 %1 行添加批注").arg(changedCount), 2500);
}

QString MainWindow::requestTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const
{
    QString primaryError;
    if (translationUseOllama) {
        const QString translated = requestOllamaTranslation(text, cancelFlag, &primaryError);
        if (!translated.isEmpty()) {
            return translated;
        }
        if (isTranslationCanceled(cancelFlag)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("翻译已取消。");
            }
            return QString();
        }
        if (!translationFallbackToOnline) {
            if (errorMessage) {
                *errorMessage = primaryError;
            }
            return QString();
        }
    }

    const QString translated = requestOnlineTranslation(text, cancelFlag, &primaryError);
    if (translated.isEmpty() && errorMessage) {
        *errorMessage = primaryError;
    }
    return translated;
}

QString MainWindow::requestOllamaCompletion(const QString &prompt, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const
{
    const QString endpoint = normalizeOllamaEndpoint(translationOllamaEndpoint);

    const QString model = translationOllamaModel.trimmed().isEmpty()
        ? QStringLiteral("qwen3:14b-q4_K_M")
        : translationOllamaModel.trimmed();

    QUrl url(endpoint + QStringLiteral("/api/generate"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VSCodeQt/1.0"));

    QJsonObject payload;
    payload.insert(QStringLiteral("model"), model);
    payload.insert(QStringLiteral("stream"), true);
    payload.insert(QStringLiteral("think"), !translationDisableThinking);
    payload.insert(QStringLiteral("prompt"), prompt);
    QJsonObject options;
    options.insert(QStringLiteral("temperature"), 0.2);
    payload.insert(QStringLiteral("options"), options);

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer cancelPollTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    bool canceledByUser = false;
    QByteArray streamingBuffer;
    QString responseText;
    bool receivedAnyChunk = false;
    QElapsedTimer elapsedTimer;
    elapsedTimer.start();

    auto appendStreamingChunk = [&](const QByteArray &chunkPayload) {
        const QByteArray trimmedPayload = chunkPayload.trimmed();
        if (trimmedPayload.isEmpty()) {
            return;
        }

        const QJsonDocument chunkDocument = QJsonDocument::fromJson(trimmedPayload);
        if (!chunkDocument.isObject()) {
            return;
        }

        const QJsonObject chunkObject = chunkDocument.object();
        QString chunkText = chunkObject.value(QStringLiteral("response")).toString();
        if (chunkText.isEmpty()) {
            chunkText = chunkObject.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        }
        if (!chunkText.isEmpty()) {
            responseText.append(chunkText);
            receivedAnyChunk = true;
        }
    };

    auto consumeStreamingBuffer = [&](bool flushRemaining) {
        while (true) {
            const int newlineIndex = streamingBuffer.indexOf('\n');
            if (newlineIndex < 0) {
                break;
            }
            const QByteArray chunkPayload = streamingBuffer.left(newlineIndex);
            streamingBuffer.remove(0, newlineIndex + 1);
            appendStreamingChunk(chunkPayload);
        }

        if (flushRemaining && !streamingBuffer.trimmed().isEmpty()) {
            appendStreamingChunk(streamingBuffer);
            streamingBuffer.clear();
        }
    };

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::readyRead, &loop, [&]() {
        streamingBuffer.append(reply->readAll());
        consumeStreamingBuffer(false);
    });
    cancelPollTimer.setInterval(80);
    connect(&cancelPollTimer, &QTimer::timeout, &loop, [&]() {
        if (!isTranslationCanceled(cancelFlag)) {
            return;
        }
        canceledByUser = true;
        reply->abort();
        loop.quit();
    });
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    if (translationTimeoutMs > 0) {
        timeoutTimer.start(translationTimeoutMs);
    }
    cancelPollTimer.start();
    loop.exec();
    timeoutTimer.stop();
    cancelPollTimer.stop();

    streamingBuffer.append(reply->readAll());
    consumeStreamingBuffer(true);

    const QByteArray responsePayload = streamingBuffer;
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (canceledByUser || isTranslationCanceled(cancelFlag)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译已取消。");
        }
        return QString();
    }
    if (timedOut) {
        if (errorMessage) {
            const int timeoutSeconds = qMax(1, translationTimeoutMs / 1000);
            const int elapsedSeconds = qMax(1LL, elapsedTimer.elapsed() / 1000);
            *errorMessage = QStringLiteral("本地 Ollama 翻译总耗时已超过 %1 秒，已停止等待。当前已等待约 %2 秒。你可以在设置里把本地超时调大，或改为“不限”。")
                                .arg(timeoutSeconds)
                                .arg(elapsedSeconds);
        }
        return QString();
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法连接本地 Ollama：%1").arg(networkMessage);
        }
        return QString();
    }

    if (!receivedAnyChunk && !responsePayload.trimmed().isEmpty()) {
        const QJsonDocument jsonDocument = QJsonDocument::fromJson(responsePayload);
        if (jsonDocument.isObject()) {
            const QJsonObject object = jsonDocument.object();
            responseText = object.value(QStringLiteral("response")).toString().trimmed();
            if (responseText.isEmpty()) {
                responseText = object.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString().trimmed();
            }
        }
    }

    responseText = responseText.trimmed();
    if (responseText.isEmpty() && responsePayload.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("本地 Ollama 没有返回任何数据。请检查模型是否正常运行。");
        }
        return QString();
    }
    if (responseText.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("本地 Ollama 返回了空响应。请检查模型设置，或尝试关闭思考模式/更换模型。");
        }
        return QString();
    }

    return responseText;
}

QList<QPair<int, QString>> MainWindow::requestContextualTranslations(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::function<void(int, const QString &, bool)> &onLineProcessed,
    QString *errorMessage) const
{
    if (isTranslationCanceled(cancelFlag)) {
        return {};
    }

    QString primaryError;
    QList<QPair<int, QString>> translations;
    if (translationUseOllama) {
        const auto localLineCallback = translationFallbackToOnline
            ? std::function<void(int, const QString &, bool)>([&onLineProcessed](int lineNumber, const QString &translatedText, bool success) {
                  if (success && onLineProcessed) {
                      onLineProcessed(lineNumber, translatedText, true);
                  }
              })
            : onLineProcessed;
        translations = requestOllamaContextualTranslations(sourceLines, targetLines, cancelFlag, localLineCallback, &primaryError);
        if (!translations.isEmpty()) {
            if (errorMessage && !primaryError.isEmpty()) {
                *errorMessage = primaryError;
            }
            if (translationFallbackToOnline && onLineProcessed) {
                QSet<int> translatedLines;
                translatedLines.reserve(translations.size());
                for (const auto &item : translations) {
                    translatedLines.insert(item.first);
                }
                for (int lineNumber : targetLines) {
                    if (!translatedLines.contains(lineNumber)) {
                        onLineProcessed(lineNumber, QString(), false);
                    }
                }
            }
            return translations;
        }
        if (!translationFallbackToOnline) {
            if (errorMessage) {
                *errorMessage = primaryError;
            }
            return {};
        }
    }

    translations = requestOnlineTranslations(sourceLines, targetLines, cancelFlag, onLineProcessed, &primaryError);
    if (errorMessage && !primaryError.isEmpty()) {
        *errorMessage = primaryError;
    }
    return translations;
}

QList<QPair<int, QString>> MainWindow::requestOllamaContextualTranslations(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::function<void(int, const QString &, bool)> &onLineProcessed,
    QString *errorMessage) const
{
    QList<int> sortedTargets = targetLines;
    std::sort(sortedTargets.begin(), sortedTargets.end());
    sortedTargets.erase(std::unique(sortedTargets.begin(), sortedTargets.end()), sortedTargets.end());
    if (sortedTargets.isEmpty()) {
        return {};
    }

    const bool preferResponsiveSingleLineMode = translationLocalStrategy == LocalTranslationStrategy::Responsive
                                                && sortedTargets.size() >= 12;
    const int maxChunkTargetLines = preferResponsiveSingleLineMode ? 1 : translationMaxChunkTargetLines;
    const int maxChunkChars = preferResponsiveSingleLineMode ? 2600 : translationMaxChunkChars;
    const int contextRadius = qMax(0, translationContextRadius);

    QHash<int, QString> translations;
    QString lastError;
    int index = 0;
    while (index < sortedTargets.size()) {
        if (isTranslationCanceled(cancelFlag)) {
            break;
        }

        int bestEndIndex = index;
        for (int candidateEnd = index; candidateEnd < sortedTargets.size() && candidateEnd < index + maxChunkTargetLines; ++candidateEnd) {
            const int contextStart = qMax(0, sortedTargets.at(index) - contextRadius);
            const int contextEnd = qMin(sourceLines.size() - 1, sortedTargets.at(candidateEnd) + contextRadius);
            int estimatedChars = 0;
            for (int lineNumber = contextStart; lineNumber <= contextEnd; ++lineNumber) {
                estimatedChars += sourceLines.value(lineNumber).size() + 24;
            }
            if (estimatedChars > maxChunkChars && candidateEnd > index) {
                break;
            }
            bestEndIndex = candidateEnd;
            if (estimatedChars > maxChunkChars) {
                break;
            }
        }

        QList<int> chunkTargets;
        chunkTargets.reserve(bestEndIndex - index + 1);
        for (int chunkIndex = index; chunkIndex <= bestEndIndex; ++chunkIndex) {
            chunkTargets.append(sortedTargets.at(chunkIndex));
        }

        const int contextStart = qMax(0, chunkTargets.constFirst() - contextRadius);
        const int contextEnd = qMin(sourceLines.size() - 1, chunkTargets.constLast() + contextRadius);
        QString errorMessage;
        if (chunkTargets.size() == 1) {
            const int lineNumber = chunkTargets.constFirst();
            const QString translatedText = requestOllamaTranslationWithContext(sourceLines, lineNumber, contextRadius, cancelFlag, &errorMessage);
            if (isTranslationCanceled(cancelFlag)) {
                break;
            }
            if (!translatedText.isEmpty()) {
                translations.insert(lineNumber, translatedText);
                if (onLineProcessed) {
                    onLineProcessed(lineNumber, translatedText, true);
                }
            } else if (onLineProcessed) {
                onLineProcessed(lineNumber, QString(), false);
            }
            if (!errorMessage.isEmpty()) {
                lastError = errorMessage;
                if (isTranslationTimeoutMessage(lastError)) {
                    break;
                }
            }
            index = bestEndIndex + 1;
            continue;
        }

        const QString responseText = requestOllamaCompletion(
            buildContextualTranslationPrompt(sourceLines, chunkTargets, contextStart, contextEnd, translationStrictOutputParsing),
            cancelFlag,
            &errorMessage);
        if (isTranslationCanceled(cancelFlag)) {
            break;
        }
        if (responseText.isEmpty() && !errorMessage.isEmpty()) {
            lastError = errorMessage;
            break;
        }
        QHash<int, QString> parsedTranslations = parseTaggedTranslationResponse(responseText);

        for (int lineNumber : chunkTargets) {
            if (isTranslationCanceled(cancelFlag)) {
                break;
            }
            QString translatedText = normalizeTranslatedText(parsedTranslations.value(lineNumber));
            if (translatedText.isEmpty()) {
                translatedText = requestOllamaTranslation(sourceLines.value(lineNumber), cancelFlag, &errorMessage);
            }
            if (translatedText.isEmpty() && !errorMessage.isEmpty() && isTranslationTimeoutMessage(errorMessage)) {
                lastError = errorMessage;
                break;
            }
            if (!translatedText.isEmpty()) {
                translations.insert(lineNumber, translatedText);
                if (onLineProcessed) {
                    onLineProcessed(lineNumber, translatedText, true);
                }
            } else if (onLineProcessed) {
                onLineProcessed(lineNumber, QString(), false);
            }
        }
        if (translations.size() < chunkTargets.size()) {
            lastError = errorMessage;
        }
        if (isTranslationTimeoutMessage(lastError)) {
            break;
        }
        index = bestEndIndex + 1;
    }

    QList<QPair<int, QString>> results;
    results.reserve(translations.size());
    for (int lineNumber : sortedTargets) {
        if (translations.contains(lineNumber)) {
            results.append(qMakePair(lineNumber, translations.value(lineNumber)));
        }
    }

    if (errorMessage && !lastError.isEmpty()) {
        *errorMessage = lastError;
    }
    return results;
}

QString MainWindow::requestOllamaTranslationWithContext(
    const QStringList &sourceLines,
    int targetLine,
    int contextRadius,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    QString *errorMessage) const
{
    if (targetLine < 0 || targetLine >= sourceLines.size()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("目标行超出范围。");
        }
        return QString();
    }

    const int contextStart = qMax(0, targetLine - qMax(0, contextRadius));
    const int contextEnd = qMin(sourceLines.size() - 1, targetLine + qMax(0, contextRadius));
    QString prompt;
    if (translationEnableCustomPrompt && !translationCustomContextPromptTemplate.trimmed().isEmpty()) {
        QStringList contextLines;
        contextLines.reserve(contextEnd - contextStart + 1);
        for (int lineNumber = contextStart; lineNumber <= contextEnd; ++lineNumber) {
            contextLines.append(sourceLines.value(lineNumber));
        }
        prompt = translationCustomContextPromptTemplate.arg(sourceLines.value(targetLine), contextLines.join(QChar('\n')));
    } else {
        prompt = buildSingleLineContextualTranslationPrompt(sourceLines, targetLine, contextStart, contextEnd);
    }
    const QString translatedText = requestOllamaCompletion(prompt, cancelFlag, errorMessage);
    const QString normalizedText = normalizeTranslatedText(translatedText);
    if (normalizedText.isEmpty()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("本地 Ollama 未返回可用译文，请检查模型输出格式或更换模型。");
        }
        return QString();
    }

    return normalizedText;
}

QList<QPair<int, QString>> MainWindow::requestNetworkLargeModelContextualTranslations(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::function<void(int, const QString &, bool)> &onLineProcessed,
    QString *errorMessage) const
{
    QList<int> sortedTargets = targetLines;
    std::sort(sortedTargets.begin(), sortedTargets.end());
    sortedTargets.erase(std::unique(sortedTargets.begin(), sortedTargets.end()), sortedTargets.end());
    if (sortedTargets.isEmpty()) {
        return {};
    }

    const int maxChunkTargetLines = translationMaxChunkTargetLines;
    const int maxChunkChars = translationMaxChunkChars;
    const int contextRadius = qMax(0, translationContextRadius);

    QHash<int, QString> translations;
    QString lastError;
    int index = 0;
    while (index < sortedTargets.size()) {
        if (isTranslationCanceled(cancelFlag)) {
            break;
        }

        int bestEndIndex = index;
        for (int candidateEnd = index; candidateEnd < sortedTargets.size() && candidateEnd < index + maxChunkTargetLines; ++candidateEnd) {
            const int contextStart = qMax(0, sortedTargets.at(index) - contextRadius);
            const int contextEnd = qMin(sourceLines.size() - 1, sortedTargets.at(candidateEnd) + contextRadius);
            int estimatedChars = 0;
            for (int lineNumber = contextStart; lineNumber <= contextEnd; ++lineNumber) {
                estimatedChars += sourceLines.value(lineNumber).size() + 24;
            }
            if (estimatedChars > maxChunkChars && candidateEnd > index) {
                break;
            }
            bestEndIndex = candidateEnd;
            if (estimatedChars > maxChunkChars) {
                break;
            }
        }

        QList<int> chunkTargets;
        chunkTargets.reserve(bestEndIndex - index + 1);
        for (int chunkIndex = index; chunkIndex <= bestEndIndex; ++chunkIndex) {
            chunkTargets.append(sortedTargets.at(chunkIndex));
        }

        const int contextStart = qMax(0, chunkTargets.constFirst() - contextRadius);
        const int contextEnd = qMin(sourceLines.size() - 1, chunkTargets.constLast() + contextRadius);
        QString chunkError;
        if (chunkTargets.size() == 1) {
            const int lineNumber = chunkTargets.constFirst();
            QString prompt;
            if (translationEnableCustomPrompt && !translationCustomContextPromptTemplate.trimmed().isEmpty()) {
                QStringList contextLines;
                contextLines.reserve(contextEnd - contextStart + 1);
                for (int lineNumberIter = contextStart; lineNumberIter <= contextEnd; ++lineNumberIter) {
                    contextLines.append(sourceLines.value(lineNumberIter));
                }
                prompt = translationCustomContextPromptTemplate.arg(sourceLines.value(lineNumber), contextLines.join(QChar('\n')));
            } else {
                prompt = buildSingleLineContextualTranslationPrompt(sourceLines, lineNumber, contextStart, contextEnd);
            }
            const QString translatedText = requestNetworkLargeModelTranslation(prompt, cancelFlag, &chunkError);
            if (isTranslationCanceled(cancelFlag)) {
                break;
            }
            if (!translatedText.isEmpty()) {
                translations.insert(lineNumber, translatedText);
                if (onLineProcessed) {
                    onLineProcessed(lineNumber, translatedText, true);
                }
            } else if (onLineProcessed) {
                onLineProcessed(lineNumber, QString(), false);
            }
            if (!chunkError.isEmpty()) {
                lastError = chunkError;
            }
            index = bestEndIndex + 1;
            continue;
        }

        const QString responseText = requestNetworkLargeModelTranslation(
            buildContextualTranslationPrompt(sourceLines, chunkTargets, contextStart, contextEnd, translationStrictOutputParsing),
            cancelFlag,
            &chunkError);
        if (isTranslationCanceled(cancelFlag)) {
            break;
        }
        if (responseText.isEmpty() && !chunkError.isEmpty()) {
            lastError = chunkError;
            break;
        }
        QHash<int, QString> parsedTranslations = parseTaggedTranslationResponse(responseText);
        if (parsedTranslations.isEmpty()) {
            parsedTranslations = parseSequentialTranslationResponse(responseText, chunkTargets);
        }

        for (int lineNumber : chunkTargets) {
            if (isTranslationCanceled(cancelFlag)) {
                break;
            }
            QString translatedText = normalizeTranslatedText(parsedTranslations.value(lineNumber));
            if (translatedText.isEmpty()) {
                translatedText = requestNetworkLargeModelTranslation(sourceLines.value(lineNumber), cancelFlag, &chunkError);
            }
            if (translatedText.isEmpty() && !chunkError.isEmpty()) {
                lastError = chunkError;
                break;
            }
            if (!translatedText.isEmpty()) {
                translations.insert(lineNumber, translatedText);
                if (onLineProcessed) {
                    onLineProcessed(lineNumber, translatedText, true);
                }
            } else if (onLineProcessed) {
                onLineProcessed(lineNumber, QString(), false);
            }
        }
        if (translations.size() < chunkTargets.size()) {
            lastError = chunkError;
        }
        if (isTranslationTimeoutMessage(lastError)) {
            break;
        }
        index = bestEndIndex + 1;
    }

    QList<QPair<int, QString>> results;
    results.reserve(translations.size());
    for (int lineNumber : sortedTargets) {
        if (translations.contains(lineNumber)) {
            results.append(qMakePair(lineNumber, translations.value(lineNumber)));
        }
    }

    if (errorMessage && !lastError.isEmpty()) {
        *errorMessage = lastError;
    }
    return results;
}

QList<QPair<int, QString>> MainWindow::requestOnlineTranslations(
    const QStringList &sourceLines,
    const QList<int> &targetLines,
    const std::shared_ptr<std::atomic_bool> &cancelFlag,
    const std::function<void(int, const QString &, bool)> &onLineProcessed,
    QString *errorMessage) const
{
    if (!translationLargeModelApiEndpoint.trimmed().isEmpty()) {
        return requestNetworkLargeModelContextualTranslations(sourceLines, targetLines, cancelFlag, onLineProcessed, errorMessage);
    }

    QList<QPair<int, QString>> results;
    QString lastError;
    for (int lineNumber : targetLines) {
        if (isTranslationCanceled(cancelFlag)) {
            break;
        }

        QString singleError;
        const QString translatedText = requestOnlineTranslation(sourceLines.value(lineNumber), cancelFlag, &singleError);
        if (!translatedText.isEmpty()) {
            results.append(qMakePair(lineNumber, translatedText));
            if (onLineProcessed) {
                onLineProcessed(lineNumber, translatedText, true);
            }
        } else if (!singleError.isEmpty()) {
            lastError = singleError;
            if (onLineProcessed) {
                onLineProcessed(lineNumber, QString(), false);
            }
        } else if (onLineProcessed) {
            onLineProcessed(lineNumber, QString(), false);
        }
    }
    if (errorMessage && !lastError.isEmpty()) {
        *errorMessage = lastError;
    }
    return results;
}

QString MainWindow::requestOllamaTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const
{
    const QString prompt = translationEnableCustomPrompt && !translationCustomPromptTemplate.trimmed().isEmpty()
        ? translationCustomPromptTemplate.arg(text)
        : QStringLiteral("你是专业翻译。请将下面的原文翻译成中文。最终输出格式必须严格为 <translation>译文</translation> ，标签外不要输出任何内容，不要解释，不要附加说明，不要保留原文。\n原文：\n%1").arg(text);
    const QString translatedText = requestOllamaCompletion(prompt, cancelFlag, errorMessage);
    const QString normalizedText = normalizeTranslatedText(translatedText);
    if (normalizedText.isEmpty()) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = QStringLiteral("本地 Ollama 未返回可用译文，请检查模型输出格式或改用云端翻译。");
        }
        return QString();
    }

    return normalizedText;
}

QString MainWindow::requestOnlineTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const
{
    if (!translationLargeModelApiEndpoint.trimmed().isEmpty()) {
        return requestNetworkLargeModelTranslation(text, cancelFlag, errorMessage);
    }

    QUrl url(QStringLiteral("https://api.mymemory.translated.net/get"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), text);
    query.addQueryItem(QStringLiteral("langpair"), QStringLiteral("auto|zh-CN"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VSCodeQt/1.0"));
    request.setRawHeader("Accept", "application/json");

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.get(request);
    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer cancelPollTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    bool canceledByUser = false;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    cancelPollTimer.setInterval(80);
    connect(&cancelPollTimer, &QTimer::timeout, &loop, [&]() {
        if (!isTranslationCanceled(cancelFlag)) {
            return;
        }
        canceledByUser = true;
        reply->abort();
        loop.quit();
    });
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    const int onlineTimeoutMs = translationTimeoutMs > 0 ? qMin(translationTimeoutMs, 30000) : 30000;
    timeoutTimer.start(onlineTimeoutMs);
    cancelPollTimer.start();
    loop.exec();
    timeoutTimer.stop();
    cancelPollTimer.stop();

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (canceledByUser || isTranslationCanceled(cancelFlag)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译已取消。");
        }
        return QString();
    }
    if (timedOut) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译请求超时。请稍后重试。");
        }
        return QString();
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = networkMessage;
        }
        return QString();
    }

    const QJsonDocument jsonDocument = QJsonDocument::fromJson(payload);
    if (!jsonDocument.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译服务返回了无效数据。");
        }
        return QString();
    }

    const QJsonObject root = jsonDocument.object();
    const QString translatedText = root.value(QStringLiteral("responseData")).toObject().value(QStringLiteral("translatedText")).toString().trimmed();
    const QString normalizedText = normalizeTranslatedText(translatedText);
    if (normalizedText.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译结果为空。");
        }
        return QString();
    }

    return normalizedText;
}

QString MainWindow::requestNetworkLargeModelTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const
{
    QString endpoint = translationLargeModelApiEndpoint.trimmed();
    if (endpoint.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("未配置网络大模型 API 地址。请在设置中填写。\n");
        }
        return QString();
    }

    if (!endpoint.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive)
        && !endpoint.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive)) {
        endpoint = QStringLiteral("https://") + endpoint;
    }

    QUrl url(endpoint);
    if (!url.isValid() || url.host().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("配置的网络大模型 API 地址无效。请填写类似 https://api.deepseek.com/chat/completions 的完整 URL。\n");
        }
        return QString();
    }

    if ((url.path().isEmpty() || url.path() == QStringLiteral("/"))
        && url.host().contains(QStringLiteral("deepseek"), Qt::CaseInsensitive)) {
        url.setPath(QStringLiteral("/chat/completions"));
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VSCodeQt/1.0"));
    if (!translationLargeModelApiKey.trimmed().isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(translationLargeModelApiKey).toUtf8());
    }

    QJsonObject payload;
    const bool useChatCompletion = endpoint.contains(QStringLiteral("/chat/completions"), Qt::CaseInsensitive)
        || endpoint.contains(QStringLiteral("/v1/chat"), Qt::CaseInsensitive);
    if (useChatCompletion) {
        payload.insert(QStringLiteral("model"), endpoint.contains(QStringLiteral("deepseek"), Qt::CaseInsensitive)
                                           ? QStringLiteral("deepseek-chat")
                                           : QStringLiteral("gpt-3.5-turbo"));
        QJsonArray messages;
        QJsonObject systemMessage;
        systemMessage.insert(QStringLiteral("role"), QStringLiteral("system"));
        systemMessage.insert(QStringLiteral("content"), QStringLiteral("你是专业翻译。请将下面的原文翻译成中文，只输出译文，不要解释，不要附加说明，不要保留原文。"));
        QJsonObject userMessage;
        userMessage.insert(QStringLiteral("role"), QStringLiteral("user"));
        userMessage.insert(QStringLiteral("content"), text);
        messages.append(systemMessage);
        messages.append(userMessage);
        payload.insert(QStringLiteral("messages"), messages);
    } else {
        payload.insert(QStringLiteral("prompt"), text);
    }
    payload.insert(QStringLiteral("temperature"), 0.2);
    payload.insert(QStringLiteral("stream"), false);

    QNetworkAccessManager networkAccessManager;
    QNetworkReply *reply = networkAccessManager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timeoutTimer;
    QTimer cancelPollTimer;
    timeoutTimer.setSingleShot(true);
    bool timedOut = false;
    bool canceledByUser = false;
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    cancelPollTimer.setInterval(80);
    connect(&cancelPollTimer, &QTimer::timeout, &loop, [&]() {
        if (!isTranslationCanceled(cancelFlag)) {
            return;
        }
        canceledByUser = true;
        reply->abort();
        loop.quit();
    });
    connect(&timeoutTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        reply->abort();
        loop.quit();
    });
    const int networkTimeoutMs = translationTimeoutMs > 0 ? translationTimeoutMs : 30000;
    timeoutTimer.start(networkTimeoutMs);
    cancelPollTimer.start();
    loop.exec();
    timeoutTimer.stop();
    cancelPollTimer.stop();

    const QByteArray payloadData = reply->readAll();
    const QNetworkReply::NetworkError networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();

    if (canceledByUser || isTranslationCanceled(cancelFlag)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("翻译已取消。" );
        }
        return QString();
    }
    if (timedOut) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("网络大模型翻译请求超时。请稍后重试。" );
        }
        return QString();
    }
    if (networkError != QNetworkReply::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法连接到网络大模型 API：%1").arg(networkMessage);
        }
        return QString();
    }

    const QString responseString = QString::fromUtf8(payloadData).trimmed();
    if (responseString.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("网络大模型 API 返回了空响应。请检查服务。" );
        }
        return QString();
    }

    const QJsonDocument document = QJsonDocument::fromJson(payloadData);
    QString translatedText;
    if (document.isObject()) {
        const QJsonObject root = document.object();
        if (root.contains(QStringLiteral("response"))) {
            translatedText = root.value(QStringLiteral("response")).toString().trimmed();
        } else if (root.contains(QStringLiteral("output"))) {
            translatedText = root.value(QStringLiteral("output")).toString().trimmed();
        } else if (root.contains(QStringLiteral("text"))) {
            translatedText = root.value(QStringLiteral("text")).toString().trimmed();
        } else if (root.contains(QStringLiteral("choices"))) {
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                const QJsonObject firstChoice = choices.first().toObject();
                if (firstChoice.contains(QStringLiteral("message"))) {
                    const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
                    translatedText = message.value(QStringLiteral("content")).toString().trimmed();
                }
                if (translatedText.isEmpty() && firstChoice.contains(QStringLiteral("text"))) {
                    translatedText = firstChoice.value(QStringLiteral("text")).toString().trimmed();
                }
            }
        }
    }
    if (translatedText.isEmpty()) {
        translatedText = responseString;
    }

    const QString normalizedText = normalizeTranslatedText(translatedText);
    if (normalizedText.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("网络大模型 API 未返回可用译文。" );
        }
        return QString();
    }

    return normalizedText;
}

QString MainWindow::normalizeTranslatedText(const QString &text) const
{
    QString normalized = QTextDocumentFragment::fromHtml(text).toPlainText().trimmed();
    if (normalized.isEmpty()) {
        return QString();
    }

    normalized.remove(QRegularExpression(QStringLiteral(R"(<think>[\s\S]*?</think>)")));
    const int thinkEndIndex = normalized.indexOf(QStringLiteral("</think>"));
    if (thinkEndIndex >= 0) {
        normalized = normalized.mid(thinkEndIndex + QStringLiteral("</think>").size()).trimmed();
    }
    const QString taggedTranslation = extractTaggedTranslationPayload(normalized);
    if (!taggedTranslation.isEmpty()) {
        normalized = taggedTranslation;
    }
    normalized.remove(QRegularExpression(QStringLiteral(R"(^(译文|翻译|中文|答案)\s*[:：]\s*)")));
    normalized.remove(QRegularExpression(QStringLiteral(R"(^(最终答案|最终输出|结果|输出)\s*[:：]\s*)")));

    if (normalized.contains(QChar('\n'))) {
        const QStringList rawLines = normalized.split(QChar('\n'), Qt::SkipEmptyParts);
        QStringList candidateLines;
        candidateLines.reserve(rawLines.size());
        for (QString line : rawLines) {
            line = line.trimmed();
            line.remove(QRegularExpression(QStringLiteral(R"(^(译文|翻译|中文|答案|最终答案|最终输出|结果|输出)\s*[:：]\s*)")));
            line = line.trimmed();
            if (line.isEmpty()) {
                continue;
            }
            candidateLines.append(line);
        }

        for (int index = candidateLines.size() - 1; index >= 0; --index) {
            const QString &candidate = candidateLines.at(index);
            if (candidate.size() > 120) {
                continue;
            }
            if (looksLikeTranslationMetaLine(candidate)) {
                continue;
            }
            normalized = candidate;
            break;
        }
    }

    normalized = normalized.trimmed();
    if ((normalized.startsWith(QLatin1Char('"')) && normalized.endsWith(QLatin1Char('"')))
        || (normalized.startsWith(QLatin1Char('\'')) && normalized.endsWith(QLatin1Char('\'')))) {
        normalized = normalized.mid(1, normalized.size() - 2).trimmed();
    }
    return normalized;
}

void MainWindow::translateCurrentOrSelectedLines()
{
    translateSourceLinesWithContext(selectedSourceLinesForTranslation(), QStringLiteral("正在结合上下文翻译原文并生成批注..."), QStringLiteral("已为 %1 行添加上下文翻译批注"));
}

void MainWindow::translateWholeDocument()
{
    bool accepted = false;
    const TranslationSelection selection = chooseWholeDocumentTranslationSelection(&accepted);
    if (!accepted) {
        return;
    }
    translateSourceLinesWithContext(selection.lines, selection.progressTitle, selection.successLabel);
}

void MainWindow::translateSourceLinesWithContext(const QList<int> &requestedLines, const QString &progressTitle, const QString &successLabel)
{
    QList<int> targetLines = requestedLines;
    std::sort(targetLines.begin(), targetLines.end());
    targetLines.erase(std::unique(targetLines.begin(), targetLines.end()), targetLines.end());

    int skippedExistingCount = 0;
    int skippedEmptyCount = 0;
    QList<int> filteredLines;
    filteredLines.reserve(targetLines.size());
    for (int lineNumber : targetLines) {
        if (editor->hasCommentAtLine(lineNumber)) {
            ++skippedExistingCount;
            continue;
        }
        if (editor->lineText(lineNumber).trimmed().isEmpty()) {
            ++skippedEmptyCount;
            continue;
        }
        filteredLines.append(lineNumber);
    }

    if (filteredLines.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("所选原文行都已有批注或内容为空"), 2500);
        return;
    }

    beginTranslationTask(editor->sourceLines(), filteredLines, progressTitle, successLabel, skippedExistingCount, skippedEmptyCount);
}

void MainWindow::insertCommentForManagedLine()
{
    const QList<int> lines = selectedManagedLines();
    if (lines.isEmpty()) {
        insertCommentForCurrentLine();
        return;
    }

    const int lineNumber = lines.constFirst();
    if (!editor->addCommentToLine(lineNumber)) {
        statusBar()->showMessage(QStringLiteral("所选行已经存在注释行"), 2500);
        return;
    }

    refreshCommentManager();
    syncCommentManagerSelection(lineNumber, false);
    statusBar()->showMessage(QStringLiteral("已在所选行下方插入注释行"), 2500);
}

void MainWindow::deleteCommentForCurrentLine()
{
    if (!editor->removeCommentFromLine(editor->currentLineNumber())) {
        statusBar()->showMessage(QStringLiteral("当前行没有可删除的注释"), 2500);
        return;
    }

    refreshCommentManager();
    syncCommentManagerSelection(editor->currentLineNumber());
    statusBar()->showMessage(QStringLiteral("已删除当前行注释"), 2500);
}

void MainWindow::deleteCommentForManagedLine()
{
    const QList<int> lines = selectedManagedLines();
    if (lines.isEmpty()) {
        deleteCommentForCurrentLine();
        return;
    }

    int deletedCount = 0;
    for (int index = lines.size() - 1; index >= 0; --index) {
        if (editor->removeCommentFromLine(lines[index])) {
            ++deletedCount;
        }
    }

    if (deletedCount == 0) {
        statusBar()->showMessage(QStringLiteral("所选行没有可删除的注释"), 2500);
        return;
    }

    refreshCommentManager();
    syncCommentManagerSelection(editor->currentLineNumber());
    statusBar()->showMessage(QStringLiteral("已删除 %1 条注释").arg(deletedCount), 2500);
}

void MainWindow::toggleCommentForCurrentLine()
{
    if (!editor->toggleCommentAtLine(editor->currentLineNumber())) {
        statusBar()->showMessage(QStringLiteral("当前行没有可折叠的注释"), 2500);
        return;
    }

    refreshCommentManager();
    syncCommentManagerSelection(editor->currentLineNumber(), false);
    statusBar()->showMessage(QStringLiteral("已切换当前行注释状态"), 2500);
}

void MainWindow::toggleCommentForManagedLine()
{
    const QList<int> lines = selectedManagedLines();
    if (lines.isEmpty()) {
        toggleCommentForCurrentLine();
        return;
    }

    int toggledCount = 0;
    for (int lineNumber : lines) {
        if (editor->toggleCommentAtLine(lineNumber)) {
            ++toggledCount;
        }
    }

    if (toggledCount == 0) {
        statusBar()->showMessage(QStringLiteral("所选行没有可折叠的注释"), 2500);
        return;
    }

    editor->goToLine(lines.constFirst());
    refreshCommentManager();
    syncCommentManagerSelection(lines.constFirst(), false);
    statusBar()->showMessage(QStringLiteral("已切换 %1 条注释状态").arg(toggledCount), 2500);
}

void MainWindow::deleteSelectedComments()
{
    const QList<int> lines = selectedManagedLines();
    if (lines.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先在注释管理中选择至少一条注释"), 2500);
        return;
    }

    deleteCommentForManagedLine();
}

void MainWindow::toggleSelectedComments()
{
    const QList<int> lines = selectedManagedLines();
    if (lines.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("请先在注释管理中选择至少一条注释"), 2500);
        return;
    }

    toggleCommentForManagedLine();
}

void MainWindow::chooseSourceAppearance()
{
    openSettingsDialog(2);
}

void MainWindow::chooseCommentAppearance()
{
    openSettingsDialog(3);
}

void MainWindow::showEditorContextMenu(const QPoint &position)
{
    QMenu *menu = editor->createStandardContextMenu();
    if (!menu) {
        return;
    }

    const int lineNumber = editor->currentLineNumber();
    const bool hasComment = editor->hasCommentAtLine(lineNumber);

    menu->addSeparator();
    QAction *insertAction = menu->addAction(QStringLiteral("插入注释行"), this, &MainWindow::insertCommentForCurrentLine);
    menu->addAction(QStringLiteral("翻译当前行或所选行并添加批注"), this, &MainWindow::translateCurrentOrSelectedLines);
    QAction *toggleAction = menu->addAction(QStringLiteral("展开或折叠注释"), this, &MainWindow::toggleCommentForCurrentLine);
    QAction *deleteAction = menu->addAction(QStringLiteral("删除注释"), this, &MainWindow::deleteCommentForCurrentLine);
    insertAction->setEnabled(!hasComment);
    toggleAction->setEnabled(hasComment);
    deleteAction->setEnabled(hasComment);

    menu->addSeparator();
    menu->addAction(QStringLiteral("打开设置"), this, &MainWindow::openSettings);
    menu->exec(editor->mapToGlobal(position));
    delete menu;
}

void MainWindow::updateFocusMode(bool enabled)
{
    focusModeEnabled = enabled;
    navigationDock->setVisible(!enabled);
    inspectorDock->setVisible(!enabled);
    commentDock->setVisible(!enabled);
    if (enabled) {
        findDock->hide();
    }
    mainToolBar->setVisible(!enabled);
    editor->setProperty("focusMode", enabled);
    editor->style()->unpolish(editor);
    editor->style()->polish(editor);
    statusBar()->showMessage(enabled ? QStringLiteral("已进入专注模式") : QStringLiteral("已退出专注模式"), 2000);
}

void MainWindow::resetTransientUiState()
{
    chapterRefreshTimer->stop();
    chapterRefreshPending = false;
    if (chapterIndexWatcher->isRunning()) {
        ++chapterRebuildRevision;
    }

    if (recentFilesList) {
        recentFilesList->clearSelection();
    }
    if (chapterList) {
        chapterList->clearSelection();
    }
    if (commentLineList) {
        commentLineList->clearSelection();
        commentLineList->clear();
    }
    if (findLineEdit) {
        findLineEdit->clear();
    }
    if (replaceLineEdit) {
        replaceLineEdit->clear();
    }

    hideFindPanel();
    editor->moveCursor(QTextCursor::Start);
    editor->centerCursor();
}

QString MainWindow::createdAtUtc() const
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

void MainWindow::setCreatedAtUtc(const QString &value)
{
    documentCreatedAtUtc = value;
}

QString MainWindow::buildTrxTitle() const
{
    const QStringList sourceLines = editor->sourceLines();
    for (const QString &line : sourceLines) {
        const QString text = line.trimmed();
        if (!text.isEmpty()) {
            return text.left(64);
        }
    }
    return currentDisplayName();
}

QString MainWindow::buildTrxThemeName() const
{
    return focusModeEnabled ? QStringLiteral("Fluent Sandstone Focus") : defaultThemeName();
}

void MainWindow::showError(const QString &title, const QString &message) const
{
    QMessageBox::critical(const_cast<MainWindow *>(this), title, message);
}

void MainWindow::showInfo(const QString &title, const QString &message) const
{
    QMessageBox::information(const_cast<MainWindow *>(this), title, message);
}