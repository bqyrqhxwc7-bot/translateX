#include "documentmanager.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include "documentmodel.h"
#include "commentservice.h"
#include "trxparser.h"

namespace {
bool isTrx(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("trx"), Qt::CaseInsensitive) == 0;
}
} // namespace

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
{
}

void DocumentManager::setDocument(DocumentModel *model)
{
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &QAbstractItemModel::dataChanged, this, &DocumentManager::markDirty);
        connect(m_model, &QAbstractItemModel::rowsInserted, this, &DocumentManager::markDirty);
        connect(m_model, &QAbstractItemModel::rowsRemoved, this, &DocumentManager::markDirty);
        connect(m_model, &QAbstractItemModel::modelReset, this, &DocumentManager::markDirty);
    }
}

void DocumentManager::setComments(CommentService *comments)
{
    if (m_comments == comments) {
        return;
    }
    if (m_comments) {
        disconnect(m_comments, nullptr, this, nullptr);
    }
    m_comments = comments;
    if (m_comments) {
        connect(m_comments, &CommentService::commentChanged, this, &DocumentManager::markDirty);
        connect(m_comments, &CommentService::commentsReset, this, &DocumentManager::markDirty);
    }
}

QString DocumentManager::currentPath() const
{
    return m_path;
}

bool DocumentManager::isDirty() const
{
    return m_dirty;
}

QString DocumentManager::documentName() const
{
    if (m_path.isEmpty()) {
        return QStringLiteral("未命名");
    }
    return QFileInfo(m_path).fileName();
}

bool DocumentManager::newDocument(const QStringList &initialLines)
{
    m_suppressDirty = true;
    if (m_model) {
        m_model->setLines(initialLines);
    }
    if (m_comments) {
        m_comments->clear();
    }
    m_suppressDirty = false;
    m_path.clear();
    m_meta.clear();
    setDirty(false);
    emit documentChanged(QString());
    return true;
}

QVariantMap DocumentManager::documentMeta() const
{
    return m_meta;
}

bool DocumentManager::openFile(const QString &path)
{
    // .trx：TrxParser 完整往返（显示层 + 批注内嵌 + meta 保真）
    if (isTrx(path)) {
        QString error;
        m_meta.clear();
        m_suppressDirty = true;
        const bool ok = TrxParser::read(path, m_model, m_comments, m_meta, &error);
        m_suppressDirty = false;
        if (!ok) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("打开 .trx 文件失败") : error);
            return false;
        }
        m_path = path;
        setDirty(false);
        emit documentChanged(m_path);
        addRecentFile(path);
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit operationFailed(QStringLiteral("无法打开文件：%1").arg(path));
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    // 编码：UTF-8（含 BOM 检测）
    QString text;
    if (raw.startsWith(QByteArrayLiteral("\xEF\xBB\xBF"))) {
        text = QString::fromUtf8(raw.mid(3));
    } else {
        text = QString::fromUtf8(raw);
    }

    // 行拆分（兼容 \r\n；去掉末尾换行产生的空行）
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }

    m_suppressDirty = true;
    if (m_model) {
        m_model->setLines(lines);
    }
    const QString commentsPath = path + QStringLiteral(".comments.json");
    if (m_comments && QFile::exists(commentsPath)) {
        m_comments->importFromFile(commentsPath);
    }
    m_suppressDirty = false;

    m_path = path;
    setDirty(false);
    emit documentChanged(m_path);
    addRecentFile(path);
    return true;
}

QStringList DocumentManager::recentFiles() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings settings(dir + QStringLiteral("/recent.ini"), QSettings::IniFormat);
    return settings.value(QStringLiteral("recentFiles")).toStringList();
}

void DocumentManager::addRecentFile(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    QStringList files = recentFiles();
    files.removeAll(path);
    files.prepend(path);
    while (files.size() > 10) {
        files.removeLast();
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings settings(dir + QStringLiteral("/recent.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("recentFiles"), files);
    settings.sync();
    emit recentFilesChanged();
}

void DocumentManager::clearRecentFiles()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QSettings settings(dir + QStringLiteral("/recent.ini"), QSettings::IniFormat);
    settings.remove(QStringLiteral("recentFiles"));
    settings.sync();
    emit recentFilesChanged();
}

bool DocumentManager::saveFile()
{
    if (m_path.isEmpty()) {
        emit operationFailed(QStringLiteral("请先使用“另存为”指定文件路径"));
        return false;
    }
    return writeDocument(m_path);
}

bool DocumentManager::saveFileAs(const QString &path)
{
    return writeDocument(path);
}

bool DocumentManager::writeDocument(const QString &path)
{
    if (!m_model) {
        emit operationFailed(QStringLiteral("未关联文档模型"));
        return false;
    }

    // .trx：TrxParser 完整往返
    if (isTrx(path)) {
        QString error;
        if (!TrxParser::write(path, m_model, m_comments, m_meta, &error)) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("保存 .trx 文件失败") : error);
            return false;
        }
        m_path = path;
        setDirty(false);
        emit documentChanged(m_path);
        return true;
    }

    // 文本：行拼接 + 结尾换行
    QStringList lines;
    lines.reserve(m_model->lineCount());
    for (int i = 0; i < m_model->lineCount(); ++i) {
        lines << m_model->lineText(i);
    }
    const QString text = lines.join(QLatin1Char('\n'))
                         + (lines.isEmpty() ? QString() : QStringLiteral("\n"));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit operationFailed(QStringLiteral("无法写入文件：%1").arg(path));
        return false;
    }
    file.write(text.toUtf8());
    file.close();

    // 批注随文档持久化
    if (m_comments) {
        m_comments->exportToFile(path + QStringLiteral(".comments.json"));
    }

    m_path = path;
    setDirty(false);
    emit documentChanged(m_path);
    return true;
}

void DocumentManager::markDirty()
{
    if (m_suppressDirty) {
        return;
    }
    setDirty(true);
}

void DocumentManager::setDirty(bool dirty)
{
    if (m_dirty == dirty) {
        return;
    }
    m_dirty = dirty;
    emit dirtyChanged(m_dirty);
}
