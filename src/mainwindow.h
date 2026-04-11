#pragma once

#include <atomic>
#include <functional>
#include <memory>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QKeySequence>
#include <QList>
#include <QMutex>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

#include "annotatedtexteditor.h"

class QAction;
class QCheckBox;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPoint;
class QProgressBar;
class QProgressDialog;
class QPushButton;
class QTimer;
class QToolBar;
template <typename T>
class QFutureWatcher;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class DocumentFormat {
        Txt,
        Docx,
        Trx,
        Unknown,
    };

    enum class ExportContentMode {
        SourceOnly,
        SourceWithComments,
        CommentsOnly,
    };

    struct ChapterEntry {
        QString title;
        int blockNumber;
    };

    struct ShortcutBinding {
        QString id;
        QString label;
        QString section;
        QAction *action;
        QKeySequence defaultSequence;
    };

    enum class TranslationDocumentScope {
        WholeDocument,
        CurrentChapter,
        UnannotatedChapters,
    };

    enum class LocalTranslationStrategy {
        Responsive,
        Consistent,
    };

    struct TranslationSelection {
        QList<int> lines;
        QString progressTitle;
        QString successLabel;
    };

    struct TranslationTaskResult {
        QString errorMessage;
        int requestedCount = 0;
        bool canceled = false;
        bool timedOut = false;
    };

    struct TranslationUiUpdate {
        int lineNumber = -1;
        QString translatedText;
        bool success = false;
    };

    void setupCentralEditor();
    void setupDockPanels();
    void setupMenus();
    void setupToolBar();
    void setupStatusBarWidgets();
    void applyFluentTheme();
    void connectSignals();
    void restorePersistentState();
    void savePersistentState();
    void refreshWindowTitle();
    void refreshDocumentStats();
    void refreshInspectorPanel();
    void refreshRecentFilesUi();
    void refreshRecentFileActions();
    void refreshCommentManager();
    void rebuildChapterIndex();
    void startChapterRebuildAsync();
    void applyChapterEntries(const QVector<ChapterEntry> &entries);
    static QVector<ChapterEntry> buildChapterEntriesFromSourceLines(const QStringList &sourceLines);
    void scheduleDocumentStatsRefresh();
    void scheduleChapterRefresh();
    void resetTransientUiState();
    void applyEditorUiPreferences();
    void applyConfiguredEditorDefaults();
    void openSettingsDialog(int initialPage = 0);
    void openSettings();
    void showEditorContextMenu(const QPoint &position);
    void syncCommentManagerSelection(int lineNumber, bool clearWhenMissing = true);
    int adjacentCommentLine(bool forward) const;
    QVector<ShortcutBinding> shortcutBindings() const;
    void restoreShortcutSettings();
    void setCurrentFile(const QString &filePath, DocumentFormat format);
    QString currentDisplayName() const;
    QList<int> selectedManagedLines() const;
    QPair<int, int> chapterLineRangeForIndex(int chapterIndex) const;
    int chapterIndexForLine(int lineNumber) const;
    QList<int> sourceLinesForRange(int startLine, int endLine) const;
    ExportContentMode chooseExportContentMode(DocumentFormat format, bool *accepted) const;
    QString buildExportText(ExportContentMode mode) const;

    void newDocument();
    bool maybeSave();
    bool openDocument();
    bool openRecentFile(const QString &filePath);
    bool saveDocument();
    bool saveDocumentAs();
    bool exportDocument(DocumentFormat forcedFormat);
    bool saveToPath(
        const QString &filePath,
        DocumentFormat format,
        bool isAutosave = false,
        ExportContentMode exportMode = ExportContentMode::SourceOnly);
    bool loadFromPath(const QString &filePath);

    bool loadTxtFile(const QString &filePath);
    bool saveTxtFile(const QString &filePath, ExportContentMode exportMode);
    bool loadTrxFile(const QString &filePath);
    bool saveTrxFile(const QString &filePath, bool isAutosave);
    bool loadDocxFile(const QString &filePath);
    bool saveDocxFile(const QString &filePath, ExportContentMode exportMode);

    bool writePlainTextToEditor(const QString &text);
    QString readPlainTextFromEditor() const;
    DocumentFormat formatFromPath(const QString &filePath) const;
    DocumentFormat formatFromSelectedFilter(const QString &selectedFilter) const;
    QString defaultSuffixForFormat(DocumentFormat format) const;
    QString saveFilterForFormat(DocumentFormat format) const;
    QString normalizeSavePath(const QString &filePath, DocumentFormat format, const QString &selectedFilter) const;
    QString formatLabel(DocumentFormat format) const;
    QString stripDocxXml(const QString &xml) const;
    QString buildDocxDocumentXml(const QString &text) const;
    bool createDocxArchive(const QString &filePath, const QString &documentXml);
    QString extractDocxDocumentXml(const QString &filePath, QString *errorMessage) const;
    bool runPowerShellScript(const QString &script, QString *stdOut, QString *stdErr) const;

    void addRecentFile(const QString &filePath);
    QStringList recentFiles() const;
    void storeRecentFiles(const QStringList &files);

    void setAutosaveEnabled(bool enabled);
    void scheduleAutosave();
    void performAutosave();
    QString autosaveFilePath(const QString &filePath = QString()) const;
    void clearAutosaveSnapshot(const QString &filePath = QString());

    void showFindPanel();
    void hideFindPanel();
    bool findText(bool forward);
    void replaceCurrentSelection();
    void replaceAllMatches();
    void triggerUndo();
    void triggerRedo();
    void jumpToChapterRow(int row);
    void jumpToManagedLineRow(int row);
    void updateFocusMode(bool enabled);

    void insertCommentForCurrentLine();
    void batchInsertCommentsByRange();
    void translateCurrentOrSelectedLines();
    void translateWholeDocument();
    void insertCommentForManagedLine();
    void deleteCommentForCurrentLine();
    void deleteCommentForManagedLine();
    void toggleCommentForCurrentLine();
    void toggleCommentForManagedLine();
    void deleteSelectedComments();
    void toggleSelectedComments();
    void goToPreviousComment();
    void goToNextComment();
    void chooseSourceAppearance();
    void chooseCommentAppearance();

    QString createdAtUtc() const;
    void setCreatedAtUtc(const QString &value);
    QString buildTrxTitle() const;
    QString buildTrxThemeName() const;

    void showError(const QString &title, const QString &message) const;
    void showInfo(const QString &title, const QString &message) const;
    QList<int> selectedSourceLinesForTranslation() const;
    TranslationSelection chooseWholeDocumentTranslationSelection(bool *accepted) const;
    void translateSourceLinesWithContext(const QList<int> &requestedLines, const QString &progressTitle, const QString &successLabel);
    void beginTranslationTask(
        const QStringList &sourceLines,
        const QList<int> &requestedLines,
        const QString &progressTitle,
        const QString &successLabel,
        int skippedExistingCount,
        int skippedEmptyCount);
    QString buildTranslationProgressMessage() const;
    void refreshTranslationProgressUi(bool active);
    void flushPendingTranslationUpdates();
    QString buildTranslationElapsedLabel() const;
    int markPendingTranslationsAborted();
    TranslationTaskResult runTranslationTask(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const std::shared_ptr<std::atomic_bool> &cancelFlag);
    QString requestTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const;
    QString requestOllamaCompletion(const QString &prompt, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const;
    QList<QPair<int, QString>> requestContextualTranslations(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const std::shared_ptr<std::atomic_bool> &cancelFlag,
        const std::function<void(int, const QString &, bool)> &onLineProcessed,
        QString *errorMessage) const;
    QList<QPair<int, QString>> requestOllamaContextualTranslations(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const std::shared_ptr<std::atomic_bool> &cancelFlag,
        const std::function<void(int, const QString &, bool)> &onLineProcessed,
        QString *errorMessage) const;
    QList<QPair<int, QString>> requestNetworkLargeModelContextualTranslations(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const std::shared_ptr<std::atomic_bool> &cancelFlag,
        const std::function<void(int, const QString &, bool)> &onLineProcessed,
        QString *errorMessage) const;
    QList<QPair<int, QString>> requestOnlineTranslations(
        const QStringList &sourceLines,
        const QList<int> &targetLines,
        const std::shared_ptr<std::atomic_bool> &cancelFlag,
        const std::function<void(int, const QString &, bool)> &onLineProcessed,
        QString *errorMessage) const;
    QString requestOllamaTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const;
    QString requestOllamaTranslationWithContext(
        const QStringList &sourceLines,
        int targetLine,
        int contextRadius,
        const std::shared_ptr<std::atomic_bool> &cancelFlag,
        QString *errorMessage) const;
    QString requestNetworkLargeModelTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const;
    QString requestOnlineTranslation(const QString &text, const std::shared_ptr<std::atomic_bool> &cancelFlag, QString *errorMessage) const;
    QString normalizeTranslatedText(const QString &text) const;
    void saveTranslationDefaults(const QString &preset,
                                 bool enableCustomPrompt,
                                 const QString &customPrompt,
                                 const QString &customContextPrompt,
                                 const QString &largeModelApiEndpoint,
                                 const QString &largeModelApiKey,
                                 int maxChunkLines,
                                 int maxChunkChars,
                                 bool strictOutput) const;
    bool loadTranslationDefaults(QString *preset,
                                 bool *enableCustomPrompt,
                                 QString *customPrompt,
                                 QString *customContextPrompt,
                                 QString *largeModelApiEndpoint,
                                 QString *largeModelApiKey,
                                 int *maxChunkLines,
                                 int *maxChunkChars,
                                 bool *strictOutput) const;

    Ui::MainWindow *ui;

    AnnotatedTextEdit *editor;

    QDockWidget *navigationDock;
    QDockWidget *inspectorDock;
    QDockWidget *findDock;
    QDockWidget *commentDock;
    QDockWidget *translationDock;

    QListWidget *recentFilesList;
    QListWidget *chapterList;
    QListWidget *commentLineList;
    QProgressBar *translationProgressBar;
    QLabel *translationProgressTimeLabel;
    QPushButton *translationCancelButton;

    QLabel *fileNameValueLabel;
    QLabel *filePathValueLabel;
    QLabel *formatValueLabel;
    QLabel *charValueLabel;
    QLabel *lineValueLabel;
    QLabel *savedAtValueLabel;
    QLabel *autosaveValueLabel;
    QLabel *statusSummaryLabel;
    QLabel *statusDetailLabel;

    QCheckBox *autosaveCheckBox;

    QLineEdit *findLineEdit;
    QLineEdit *replaceLineEdit;

    QToolBar *mainToolBar;
    QMenu *recentFilesMenu;

    QAction *newAction;
    QAction *openAction;
    QAction *saveAction;
    QAction *saveAsAction;
    QAction *exportTxtAction;
    QAction *exportDocxAction;
    QAction *exportTrxAction;
    QAction *autosaveNowAction;
    QAction *exitAction;
    QAction *undoAction;
    QAction *redoAction;
    QAction *cutAction;
    QAction *copyAction;
    QAction *pasteAction;
    QAction *selectAllAction;
    QAction *showFindAction;
    QAction *findNextAction;
    QAction *findPreviousAction;
    QAction *replaceAction;
    QAction *replaceAllAction;
    QAction *focusModeAction;
    QAction *insertCommentAction;
    QAction *batchInsertCommentsAction;
    QAction *translateLinesAction;
    QAction *translateDocumentAction;
    QAction *deleteCommentAction;
    QAction *toggleCommentAction;
    QAction *deleteSelectedCommentsAction;
    QAction *toggleSelectedCommentsAction;
    QAction *previousCommentAction;
    QAction *nextCommentAction;
    QAction *sourceAppearanceAction;
    QAction *commentAppearanceAction;
    QAction *settingsAction;

    QVector<QAction *> recentFileActions;
    QVector<ChapterEntry> chapterEntries;

    QTimer *autosaveTimer;
    QTimer *statsRefreshTimer;
    QTimer *chapterRefreshTimer;
    QTimer *translationUiFlushTimer;
    QTimer *translationElapsedUiTimer;
    QFutureWatcher<QVector<ChapterEntry>> *chapterIndexWatcher;
    QFutureWatcher<TranslationTaskResult> *translationWatcher;
    QProgressDialog *translationProgressDialog;

    QString currentFilePath;
    QString documentCreatedAtUtc;
    QString lastSavedAtUtc;
    QString sessionId;
    std::shared_ptr<std::atomic_bool> translationCancelFlag;
    mutable QMutex translationUiUpdateMutex;
    QList<TranslationUiUpdate> translationPendingUiUpdates;
    bool translationUiFlushScheduled;
    QHash<int, QString> translationPendingSourceTexts;
    QString translationOllamaEndpoint;
    QString translationOllamaModel;
    QString translationProgressTitle;
    QString translationSuccessLabelTemplate;
    QElapsedTimer translationElapsedTimer;
    AnnotatedTextEdit::TextAppearance defaultSourceAppearance;
    AnnotatedTextEdit::TextAppearance defaultCommentAppearance;
    DocumentFormat currentFormat;
    bool autosaveEnabled;
    bool focusModeEnabled;
    bool translationUseOllama;
    bool translationFallbackToOnline;
    bool translationDisableThinking;
    bool translationEnableCustomPrompt;
    QString translationCustomPromptTemplate;
    QString translationCustomContextPromptTemplate;
    QString translationLargeModelApiEndpoint;
    QString translationLargeModelApiKey;
    QString translationConfigPreset;
    int translationMaxChunkTargetLines;
    int translationMaxChunkChars;
    bool translationStrictOutputParsing;
    LocalTranslationStrategy translationLocalStrategy;
    bool suppressDocumentRefresh;
    bool rememberWindowLayout;
    bool commentManagerDirty;
    bool inspectorPanelDirty;
    bool chapterIndexDirty;
    int autosaveIntervalMs;
    int translationContextRadius;
    int translationTimeoutMs;
    int translationRequestedCount;
    int translationCompletedCount;
    int translationInsertedCount;
    int translationFailedCount;
    int translationSkippedConflictCount;
    int translationAbortedCount;
    int editorDocumentMargin;
    int editorTabStopDistance;
    bool editorWrapEnabled;
    int translationSkippedExistingCount;
    int translationSkippedEmptyCount;
    int translationPendingSourceLineCount;
    quint64 chapterRebuildRevision;
    quint64 activeChapterRevision;
    bool chapterRefreshPending;
};
