#include "commentservice.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

CommentService::CommentService(QObject *parent)
    : QObject(parent)
{
}

void CommentService::setComment(int lineNumber, const QString &text)
{
    if (lineNumber < 0) {
        return;
    }
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        removeComment(lineNumber);
        return;
    }
    if (m_comments.value(lineNumber) == trimmed) {
        return;
    }
    m_comments.insert(lineNumber, trimmed);
    emit commentChanged(lineNumber);
}

void CommentService::removeComment(int lineNumber)
{
    if (m_comments.remove(lineNumber) > 0) {
        emit commentChanged(lineNumber);
    }
}

QString CommentService::commentAt(int lineNumber) const
{
    return m_comments.value(lineNumber);
}

bool CommentService::hasCommentAt(int lineNumber) const
{
    return m_comments.contains(lineNumber);
}

int CommentService::count() const
{
    return m_comments.size();
}

void CommentService::clear()
{
    if (m_comments.isEmpty()) {
        return;
    }
    m_comments.clear();
    emit commentsReset();
}

QVariantMap CommentService::allComments() const
{
    QVariantMap map;
    for (auto it = m_comments.constBegin(); it != m_comments.constEnd(); ++it) {
        map.insert(QString::number(it.key()), it.value());
    }
    return map;
}

void CommentService::shiftLines(int fromLineNumber, int delta)
{
    if (delta == 0) {
        return;
    }
    QHash<int, QString> shifted;
    for (auto it = m_comments.constBegin(); it != m_comments.constEnd(); ++it) {
        const int line = it.key();
        if (line >= fromLineNumber) {
            const int newLine = line + delta;
            if (newLine >= 0) {
                shifted.insert(newLine, it.value());
            }
            // 平移后为负的行丢弃
        } else {
            shifted.insert(line, it.value());
        }
    }
    if (shifted != m_comments) {
        m_comments = shifted;
        emit commentsReset();
    }
}

bool CommentService::exportToFile(const QString &path) const
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    QJsonObject comments;
    for (auto it = m_comments.constBegin(); it != m_comments.constEnd(); ++it) {
        comments.insert(QString::number(it.key()), it.value());
    }
    root.insert(QStringLiteral("comments"), comments);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.close();
    return true;
}

bool CommentService::importFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    const QJsonObject comments = root.value(QStringLiteral("comments")).toObject();
    QHash<int, QString> loaded;
    for (auto it = comments.constBegin(); it != comments.constEnd(); ++it) {
        bool ok = false;
        const int line = it.key().toInt(&ok);
        if (ok && line >= 0) {
            loaded.insert(line, it.value().toString());
        }
    }

    m_comments = loaded;
    emit commentsReset();
    return true;
}
