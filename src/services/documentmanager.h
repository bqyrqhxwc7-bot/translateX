#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class DocumentModel;
class CommentService;

// 文档管理服务：打开/保存/新建文档，跟踪修改状态（dirty）。
// 与 DocumentModel（行文本）+ CommentService（批注）协作，批注随文档持久化到 <path>.comments.json。
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

    Q_INVOKABLE bool newDocument(const QStringList &initialLines = {});
    Q_INVOKABLE bool openFile(const QString &path);
    Q_INVOKABLE bool saveFile();
    Q_INVOKABLE bool saveFileAs(const QString &path);

    // 最近文件（持久化到 AppConfigLocation/recent.ini，上限 10）
    Q_INVOKABLE QStringList recentFiles() const;
    Q_INVOKABLE void addRecentFile(const QString &path);
    Q_INVOKABLE void clearRecentFiles();

signals:
    void documentChanged(const QString &path);
    void dirtyChanged(bool dirty);
    void operationFailed(const QString &message);
    void recentFilesChanged();

private:
    bool writeDocument(const QString &path);
    void markDirty();
    void setDirty(bool dirty);

    // QPointer：页面（QML）销毁后自动置空，避免悬挂指针崩溃（NoStack 导航每次重建页面）
    QPointer<DocumentModel> m_model;
    CommentService *m_comments = nullptr;
    QString m_path;
    bool m_dirty = false;
    bool m_suppressDirty = false;
};
