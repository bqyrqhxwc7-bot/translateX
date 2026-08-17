#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class DocumentModel;
class CommentService;
class QTimer;

// 文档管理服务：打开/保存/新建文档，跟踪修改状态（dirty）。
// 与 DocumentModel（行文本）+ CommentService（批注）协作，批注随文档持久化到 <path>.comments.json。
// 自动保存（迭代4）：见 docs/services/iteration4-stats-autosave-glossary.md §2——
//   dirty 且未受限时每 60s 写 <AppConfigLocation>/autosave/<名>.autosave.trx（完整往返含批注）；
//   正常保存/打开/新建/恢复后清理；启动时 hasAutosave() 提示恢复。
class DocumentManager : public QObject
{
    Q_OBJECT

public:
    explicit DocumentManager(QObject *parent = nullptr);

    // 关联（模型在 QML 页面内创建，用 setter 注入）
    Q_INVOKABLE void setDocument(DocumentModel *model);
    Q_INVOKABLE void setComments(CommentService *comments);

    Q_INVOKABLE QString currentPath() const;
    Q_INVOKABLE bool isDirty() const;
    Q_INVOKABLE QString documentName() const;
    // .trx 元数据（sourceFile/sourceFormat/font/images 等，原样保真；QML 可读）
    Q_INVOKABLE QVariantMap documentMeta() const;

    Q_INVOKABLE bool newDocument(const QStringList &initialLines = {});
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE bool saveFile();
    Q_INVOKABLE bool saveFileAs(const QString &path);

    // ---- 自动保存（迭代4）----
    Q_INVOKABLE void setAutosaveEnabled(bool enabled);   // ui.autosaveEnabled 变化时调用
    Q_INVOKABLE bool hasAutosave() const;                // autosave 目录存在未恢复的自动保存文件
    Q_INVOKABLE QString autosavePath() const;            // 当前文档对应的自动保存文件
    Q_INVOKABLE bool restoreAutosave();                  // 恢复最新的自动保存文件（成功后清理）
    Q_INVOKABLE void discardAutosave();                  // 丢弃（删除全部自动保存文件）

    // 最近文件（持久化到 AppConfigLocation/recent.ini，上限 10）
    Q_INVOKABLE QStringList recentFiles() const;
    Q_INVOKABLE void addRecentFile(const QString &path);
    Q_INVOKABLE void clearRecentFiles();

    // 大文件受限模式阈值（默认 5 万行 / 200MB；测试可调，用完须还原）
    static void setLargeFileLimits(int maxLines, qint64 maxBytes);

signals:
    void documentChanged(const QString &path);
    void dirtyChanged(bool dirty);
    void operationFailed(const QString &message);
    void recentFilesChanged();

private slots:
    void onAutosaveTick();

private:
    bool writeDocument(const QString &path);
    void markDirty();
    void setDirty(bool dirty);
    // 打开成功后按 行数/体积 阈值设置模型受限模式（大文件降级）
    void applyLargeFileLimit(const QString &path);
    static QString sanitizeFileName(QString name);
    static QString autosaveDir();
    static QString autosavePathFor(const QString &path);
    void clearAutosaveFor(const QString &path);

    // QPointer：页面（QML）销毁后自动置空，避免悬挂指针崩溃（NoStack 导航每次重建页面）
    QPointer<DocumentModel> m_model;
    CommentService *m_comments = nullptr;
    QString m_path;
    QVariantMap m_meta;   // .trx 元数据（read 保留、write 写回）
    bool m_dirty = false;
    bool m_suppressDirty = false;
    QTimer *m_autosaveTimer = nullptr;
    bool m_autosaveEnabled = true;
    static int s_maxLines;         // 大文件受限阈值：行数（默认 50000）
    static qint64 s_maxBytes;      // 大文件受限阈值：字节（默认 200MB）
};
