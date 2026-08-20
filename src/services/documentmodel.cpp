#include "documentmodel.h"

#include <QRegularExpression>
#include <QVariantMap>

#include "commentservice.h"

DocumentModel::DocumentModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

QString DocumentModel::serviceId() const
{
    return QStringLiteral("documentModel");
}

QString DocumentModel::displayName() const
{
    return QStringLiteral("文档模型");
}

QString DocumentModel::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap DocumentModel::healthCheck() const
{
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("模型可用") } };
}

int DocumentModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_lines.size();
}

QHash<int, QByteArray> DocumentModel::roleNames() const
{
    return {
        { LineNumberRole, "lineNumber" },
        { TextRole, "text" },
        { IsCommentRole, "isComment" },
        { HasCommentRole, "hasComment" },
        { CommentTextRole, "commentText" },
        { DisplayRole, "display" },
        { RichTextRole, "rich" },
        { ImageIdsRole, "imageIds" },
    };
}

QString DocumentModel::textForLine(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return QString();
    }
    return m_lines.at(lineNumber).text;
}

QVariant DocumentModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_lines.size()) {
        return QVariant();
    }

    const LineEntry &entry = m_lines.at(index.row());
    switch (role) {
    case LineNumberRole:
        return index.row() + 1;
    case TextRole:
        return entry.text;
    case IsCommentRole:
        return false;
    case HasCommentRole:
        return m_commentProvider ? m_commentProvider->hasCommentAt(index.row())
                                 : entry.hasComment;
    case CommentTextRole:
        return m_commentProvider ? m_commentProvider->commentAt(index.row())
                                 : entry.comment;
    case DisplayRole:
        // 受限模式：对外一律纯文本（渲染走编辑层），显示层数据保留供 .trx 往返
        return m_limitedMode ? QLatin1String("plain") : entry.display;
    case RichTextRole:
        return m_limitedMode ? QString() : entry.rich;
    case ImageIdsRole:
        return m_limitedMode ? QStringList() : entry.imageIds;
    default:
        return QVariant();
    }
}

QString DocumentModel::lineText(int lineNumber) const
{
    return textForLine(lineNumber);
}

int DocumentModel::lineCount() const
{
    return m_lines.size();
}

bool DocumentModel::limitedMode() const
{
    return m_limitedMode;
}

void DocumentModel::setLimitedMode(bool limited)
{
    if (m_limitedMode == limited) {
        return;
    }
    m_limitedMode = limited;
    // 显示层角色掩蔽变化 → 全表刷新渲染（受限：rich/image 回退纯文本）
    if (!m_lines.isEmpty()) {
        const QModelIndex first = index(0);
        const QModelIndex last = index(m_lines.size() - 1);
        emit dataChanged(first, last, { DisplayRole, RichTextRole, ImageIdsRole });
    }
    emit limitedModeChanged();
}

void DocumentModel::setLines(const QStringList &lines)
{
    beginResetModel();
    m_lines.clear();
    m_lines.reserve(lines.size());
    for (const QString &line : lines) {
        LineEntry entry;
        entry.text = line;
        m_lines.append(entry);
    }
    endResetModel();
    // 新文档内容 → 批注清空（数据源在 CommentService）+ 清空编辑历史
    if (m_commentProvider) {
        m_commentProvider->clear();
    }
    clearUndoHistory();
    emit lineCountChanged();
}

void DocumentModel::setCommentProvider(CommentService *provider)
{
    if (m_commentProvider == provider) {
        return;
    }
    m_commentProvider = provider;
    if (!m_commentProvider) {
        return;
    }
    // 单行批注变化 → 该行数据刷新
    connect(m_commentProvider, &CommentService::commentChanged, this,
            [this](int lineNumber) {
        if (lineNumber < 0 || lineNumber >= m_lines.size()) {
            return;
        }
        const QModelIndex idx = index(lineNumber);
        emit dataChanged(idx, idx, { HasCommentRole, CommentTextRole });
    });
    // 全量变化 → 整表刷新
    connect(m_commentProvider, &CommentService::commentsReset, this,
            [this]() {
        if (m_lines.isEmpty()) {
            return;
        }
        const QModelIndex first = index(0);
        const QModelIndex last = index(m_lines.size() - 1);
        emit dataChanged(first, last, { HasCommentRole, CommentTextRole });
    });
}

bool DocumentModel::hasCommentAt(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return false;
    }
    if (m_commentProvider) {
        return m_commentProvider->hasCommentAt(lineNumber);
    }
    return m_lines.at(lineNumber).hasComment;
}

QString DocumentModel::commentAt(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return QString();
    }
    if (m_commentProvider) {
        return m_commentProvider->commentAt(lineNumber);
    }
    return m_lines.at(lineNumber).comment;
}

void DocumentModel::setComment(int lineNumber, const QString &text)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return;
    }
    if (m_commentProvider) {
        // 委托：CommentService 会发 commentChanged → 本模型刷新该行
        m_commentProvider->setComment(lineNumber, text);
        return;
    }
    LineEntry &entry = m_lines[lineNumber];
    entry.comment = text;
    entry.hasComment = !text.trimmed().isEmpty();
    const QModelIndex idx = index(lineNumber);
    emit dataChanged(idx, idx, { HasCommentRole, CommentTextRole });
}

void DocumentModel::updateLineText(int lineNumber, const QString &text)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return;
    }
    const QString oldText = m_lines.at(lineNumber).text;
    if (oldText == text) {
        return;
    }
    m_lines[lineNumber].text = text;
    if (m_undoEnabled) {
        pushCommand({ EditCommand::TextChange, lineNumber, oldText, text });
    }
    const QModelIndex idx = index(lineNumber);
    emit dataChanged(idx, idx, { TextRole });

    // 编辑 rich/image 行 → 显示层同步降级并清空（富文本/图片与编辑后内容
    // 不同步，残留会导致 .trx 保存后再打开显示旧样式错位；降级不可逆是
    // 既有设计权衡，见 HANDOVER.md §4「显示层退化」）
    LineEntry &entry = m_lines[lineNumber];
    if (entry.display != QLatin1String("plain")) {
        entry.display = QStringLiteral("plain");
        entry.rich.clear();
        entry.imageIds.clear();
        emit dataChanged(idx, idx, { DisplayRole, RichTextRole, ImageIdsRole });
    }
}

void DocumentModel::setLineDisplay(int lineNumber, const QString &mode)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return;
    }
    LineEntry &entry = m_lines[lineNumber];
    if (entry.display == mode) {
        return;
    }
    entry.display = mode;
    const QModelIndex idx = index(lineNumber);
    emit dataChanged(idx, idx, { DisplayRole });
}

void DocumentModel::setLineRich(int lineNumber, const QString &html)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return;
    }
    LineEntry &entry = m_lines[lineNumber];
    if (entry.rich == html) {
        return;
    }
    entry.rich = html;
    const QModelIndex idx = index(lineNumber);
    emit dataChanged(idx, idx, { RichTextRole });
}

void DocumentModel::setLineImages(int lineNumber, const QStringList &ids)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return;
    }
    LineEntry &entry = m_lines[lineNumber];
    if (entry.imageIds == ids) {
        return;
    }
    entry.imageIds = ids;
    const QModelIndex idx = index(lineNumber);
    emit dataChanged(idx, idx, { ImageIdsRole });
}

QString DocumentModel::displayAt(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return QStringLiteral("plain");
    }
    return m_lines.at(lineNumber).display;
}

QString DocumentModel::richAt(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return QString();
    }
    return m_lines.at(lineNumber).rich;
}

QStringList DocumentModel::imageIdsAt(int lineNumber) const
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return {};
    }
    return m_lines.at(lineNumber).imageIds;
}

int DocumentModel::insertLine(int atLineNumber, const QString &text)
{
    // 负数视为无效索引，拒绝插入（避免意外修改文档）
    if (atLineNumber < 0) {
        return -1;
    }
    const int pos = qMin(atLineNumber, m_lines.size());
    beginInsertRows(QModelIndex(), pos, pos);
    LineEntry entry;
    entry.text = text;
    m_lines.insert(pos, entry);
    endInsertRows();
    // 批注随行号平移（新行之后的批注 +1）
    if (m_commentProvider) {
        m_commentProvider->shiftLines(pos, +1);
    }
    if (m_undoEnabled) {
        pushCommand({ EditCommand::Insert, pos, QString(), text });
    }
    emit lineCountChanged();
    return pos;
}

int DocumentModel::removeLine(int lineNumber)
{
    if (lineNumber < 0 || lineNumber >= m_lines.size()) {
        return -1;
    }
    const QString removedText = m_lines.at(lineNumber).text;
    beginRemoveRows(QModelIndex(), lineNumber, lineNumber);
    m_lines.removeAt(lineNumber);
    endRemoveRows();
    // 被删行的批注移除，其后行号 -1
    if (m_commentProvider) {
        m_commentProvider->removeComment(lineNumber);
        m_commentProvider->shiftLines(lineNumber + 1, -1);
    }
    if (m_undoEnabled) {
        pushCommand({ EditCommand::Remove, lineNumber, removedText, QString() });
    }
    emit lineCountChanged();
    return lineNumber;
}

int DocumentModel::appendLine(const QString &text)
{
    return insertLine(m_lines.size(), text);
}

void DocumentModel::clear()
{
    beginResetModel();
    m_lines.clear();
    endResetModel();
    if (m_commentProvider) {
        m_commentProvider->clear();
    }
    clearUndoHistory();
    emit lineCountChanged();
}

void DocumentModel::pushCommand(const EditCommand &cmd)
{
    m_undoStack.append(cmd);
    m_redoStack.clear();
    emit undoStackChanged();
}

void DocumentModel::applyCommand(const EditCommand &cmd, bool undo)
{
    // 回放期间不记录（m_undoEnabled=false），批注随行平移照常进行
    switch (cmd.type) {
    case EditCommand::TextChange:
        updateLineText(cmd.line, undo ? cmd.before : cmd.after);
        break;
    case EditCommand::Insert:
        if (undo) {
            removeLine(cmd.line);
        } else {
            insertLine(cmd.line, cmd.after);
        }
        break;
    case EditCommand::Remove:
        if (undo) {
            insertLine(cmd.line, cmd.before);
        } else {
            removeLine(cmd.line);
        }
        break;
    }
}

bool DocumentModel::undo()
{
    if (m_undoStack.isEmpty()) {
        return false;
    }
    const EditCommand cmd = m_undoStack.takeLast();
    m_undoEnabled = false;
    applyCommand(cmd, true);
    m_undoEnabled = true;
    m_redoStack.append(cmd);
    emit undoStackChanged();
    return true;
}

bool DocumentModel::redo()
{
    if (m_redoStack.isEmpty()) {
        return false;
    }
    const EditCommand cmd = m_redoStack.takeLast();
    m_undoEnabled = false;
    applyCommand(cmd, false);
    m_undoEnabled = true;
    m_undoStack.append(cmd);
    emit undoStackChanged();
    return true;
}

bool DocumentModel::canUndo() const
{
    return !m_undoStack.isEmpty();
}

bool DocumentModel::canRedo() const
{
    return !m_redoStack.isEmpty();
}

void DocumentModel::clearUndoHistory()
{
    if (m_undoStack.isEmpty() && m_redoStack.isEmpty()) {
        return;
    }
    m_undoStack.clear();
    m_redoStack.clear();
    emit undoStackChanged();
}

QVariantMap DocumentModel::stats() const
{
    // 正则只编译一次（50 万行场景避免每行重编译）
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QVariantMap out;
    int nonEmpty = 0;
    qint64 chars = 0;
    qint64 words = 0;
    int richLines = 0;
    int imageLines = 0;
    int comments = 0;
    for (int i = 0; i < m_lines.size(); ++i) {
        const LineEntry &entry = m_lines.at(i);
        if (!entry.text.trimmed().isEmpty()) {
            ++nonEmpty;
        }
        // 非空白字符数
        for (const QChar &c : entry.text) {
            if (!c.isSpace()) {
                ++chars;
            }
        }
        // 空白分词数（不含首尾空白的空串）
        words += entry.text.split(ws, Qt::SkipEmptyParts).size();
        if (entry.display == QStringLiteral("rich")) {
            ++richLines;
        } else if (entry.display == QStringLiteral("image")) {
            ++imageLines;
        }
        if (hasCommentAt(i)) {
            ++comments;
        }
    }
    out.insert(QStringLiteral("lines"), m_lines.size());
    out.insert(QStringLiteral("nonEmptyLines"), nonEmpty);
    out.insert(QStringLiteral("chars"), chars);
    out.insert(QStringLiteral("words"), words);
    out.insert(QStringLiteral("comments"), comments);
    out.insert(QStringLiteral("richLines"), richLines);
    out.insert(QStringLiteral("imageLines"), imageLines);
    return out;
}
