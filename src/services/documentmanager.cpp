#include "documentmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include "documentmodel.h"
#include "commentservice.h"
#include "configservice.h"
#include "trxparser.h"
#include "docxparser.h"
#include "pdfparser.h"

namespace {
bool isTrx(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("trx"), Qt::CaseInsensitive) == 0;
}
bool isDocx(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("docx"), Qt::CaseInsensitive) == 0;
}
bool isPdf(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0;
}
bool isMarkdown(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) == 0;
}
// 自动保存文件标记（避免对 autosave 文件本身再自动保存）
bool isAutosavePath(const QString &path)
{
    return QFileInfo(path).fileName().endsWith(QLatin1String(".autosave.trx"));
}
} // namespace

int DocumentManager::s_maxLines = 50000;
qint64 DocumentManager::s_maxBytes = 200LL * 1024 * 1024;

void DocumentManager::setLargeFileLimits(int maxLines, qint64 maxBytes)
{
    s_maxLines = maxLines;
    s_maxBytes = maxBytes;
}

void DocumentManager::applyLargeFileLimit(const QString &path)
{
    if (!m_model) {
        return;
    }
    const qint64 size = QFileInfo(path).size();
    const bool limited = (s_maxLines > 0 && m_model->lineCount() > s_maxLines)
                         || (s_maxBytes > 0 && size > s_maxBytes);
    m_model->setLimitedMode(limited);
}

DocumentManager::DocumentManager(QObject *parent)
    : QObject(parent)
{
    m_autosaveTimer = new QTimer(this);
    m_autosaveTimer->setInterval(60 * 1000);
    connect(m_autosaveTimer, &QTimer::timeout, this, &DocumentManager::onAutosaveTick);
    m_autosaveTimer->start();
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
    clearAutosaveFor(m_path);
    m_path.clear();
    m_meta.clear();
    if (m_model) {
        m_model->setLimitedMode(false);
    }
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
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        applyLargeFileLimit(path);
        addRecentFile(path);
        return true;
    }

    // .docx：DocxParser 残缺导入（仅读，段落→行 + 图片 + 基础格式）
    if (isDocx(path)) {
        QString error;
        m_meta.clear();
        m_suppressDirty = true;
        const bool ok = DocxParser::read(path, m_model, m_comments, m_meta, &error);
        m_suppressDirty = false;
        if (!ok) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("导入 .docx 失败") : error);
            return false;
        }
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        applyLargeFileLimit(path);
        addRecentFile(path);
        return true;
    }

    // .pdf：PdfParser 残缺导入（只读，每页一行）
    if (isPdf(path)) {
        QString error;
        m_meta.clear();
        m_suppressDirty = true;
        const bool ok = PdfParser::read(path, m_model, m_comments, m_meta, &error);
        m_suppressDirty = false;
        if (!ok) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("导入 .pdf 失败") : error);
            return false;
        }
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        applyLargeFileLimit(path);
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
    applyLargeFileLimit(path);
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

// ---- 自动保存（迭代4）----

void DocumentManager::setAutosaveEnabled(bool enabled)
{
    m_autosaveEnabled = enabled;
    if (enabled) {
        m_autosaveTimer->start();
    } else {
        m_autosaveTimer->stop();
    }
}

QString DocumentManager::sanitizeFileName(QString name)
{
    name.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")), QStringLiteral("_"));
    if (name.isEmpty()) {
        name = QStringLiteral("未命名");
    }
    return name;
}

QString DocumentManager::autosaveDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                        + QStringLiteral("/autosave");
    QDir().mkpath(dir);
    return dir;
}

QString DocumentManager::autosavePathFor(const QString &path)
{
    const QString base = path.isEmpty() ? QStringLiteral("未命名")
                                        : QFileInfo(path).completeBaseName();
    // 路径哈希后缀：跨目录/跨格式同名文档（A/report.docx vs B/report.txt）不互相覆盖
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1)
                                .toHex().left(8);
    return autosaveDir() + QLatin1Char('/') + sanitizeFileName(base)
           + QStringLiteral("-") + QString::fromLatin1(hash)
           + QStringLiteral(".autosave.trx");
}

QString DocumentManager::autosavePath() const
{
    return autosavePathFor(m_path);
}

bool DocumentManager::hasAutosave() const
{
    const QDir dir(autosaveDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.autosave.trx"),
                                            QDir::Files);
    return !files.isEmpty();
}

bool DocumentManager::takeAutosavePrompt()
{
    if (m_autosavePromptShown) {
        return false;
    }
    m_autosavePromptShown = true;
    return hasAutosave();
}

QString DocumentManager::autosaveDescription() const
{
    const QDir dir(autosaveDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.autosave.trx"),
                                            QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        return QString();
    }
    const QFileInfo info(dir.absoluteFilePath(files.first()));
    QString base = info.completeBaseName();
    base.remove(QStringLiteral(".autosave"));
    return QStringLiteral("%1（%2）").arg(base,
        info.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
}

bool DocumentManager::restoreAutosave()
{
    const QDir dir(autosaveDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.autosave.trx"),
                                            QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        return false;
    }
    const QString autosave = dir.absoluteFilePath(files.first());
    if (!openFile(autosave)) {
        return false;
    }
    // 还原原始文档路径（tick 写入时记录在 meta.originalPath）：
    // 否则 m_path 指向已删除的 autosave 文件 → Ctrl+S 写进数据目录、自动保存失效
    const QString original = m_meta.value(QStringLiteral("originalPath")).toString();
    m_meta.remove(QStringLiteral("originalPath"));
    if (!original.isEmpty()) {
        m_path = original;
        setDirty(true);   // 恢复的内容尚未保存到原文件
        emit documentChanged(m_path);
        // openFile 已把 autosave 路径加入最近文件，移除死条目
        QStringList recents = recentFiles();
        recents.removeAll(autosave);
        const QString dir2 = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QSettings settings(dir2 + QStringLiteral("/recent.ini"), QSettings::IniFormat);
        settings.setValue(QStringLiteral("recentFiles"), recents);
        settings.sync();
        emit recentFilesChanged();
    }
    return true;
}

void DocumentManager::discardAutosave()
{
    const QDir dir(autosaveDir());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.autosave.trx"),
                                            QDir::Files);
    for (const QString &f : files) {
        QFile::remove(dir.absoluteFilePath(f));
    }
}

void DocumentManager::clearAutosaveFor(const QString &path)
{
    // 空路径 = 未命名文档（autosavePathFor 内部映射为“未命名”）
    QFile::remove(autosavePathFor(path));
}

void DocumentManager::onAutosaveTick()
{
    if (!m_autosaveEnabled || !m_dirty || !m_model) {
        return;
    }
    // 受限模式（大文件）禁用自动保存，避免反复写盘
    if (m_model->limitedMode()) {
        return;
    }
    // 自动保存文件本身不再二次自动保存
    if (isAutosavePath(m_path)) {
        return;
    }
    QString error;
    // meta 副本注入 originalPath：恢复时还原原始文档路径（见 restoreAutosave）
    QVariantMap meta = m_meta;
    meta.insert(QStringLiteral("originalPath"), m_path);
    if (!TrxParser::write(autosavePath(), m_model, m_comments, meta, &error)) {
        emit operationFailed(error.isEmpty() ? QStringLiteral("自动保存失败") : error);
    }
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
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        return true;
    }

    // .docx：DocxParser 导出（原文 + 译文批注，样式 docxCommentStyle: inline/native）
    if (isDocx(path)) {
        QString error;
        const QString style = ConfigService::instance()->get(
            QStringLiteral("translation"), QStringLiteral("docxCommentStyle")).toString();
        if (!DocxParser::write(path, m_model, m_comments, style, &m_meta, &error)) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("保存 .docx 文件失败") : error);
            return false;
        }
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        return true;
    }

    // .pdf：PdfParser 导出（编辑层文本 → 文本页，自动分页）
    if (isPdf(path)) {
        QString error;
        if (!PdfParser::write(path, m_model, m_comments, m_meta, &error)) {
            emit operationFailed(error.isEmpty() ? QStringLiteral("导出 .pdf 失败") : error);
            return false;
        }
        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
        setDirty(false);
        if (isAutosavePath(path)) {
            QFile::remove(path);
        }
        emit documentChanged(m_path);
        return true;
    }

    // .md：Markdown 对照导出（迭代4b：原文 + 译文批注引用块，单向导出）
    if (isMarkdown(path)) {
        QStringList md;
        const QString title = QFileInfo(path).completeBaseName();
        md << QStringLiteral("# %1").arg(title) << QString();
        for (int i = 0; i < m_model->lineCount(); ++i) {
            const QString src = m_model->lineText(i);
            if (src.trimmed().isEmpty()) {
                continue;
            }
            md << QStringLiteral("## 第 %1 行").arg(i + 1) << src;
            const QString trans = m_comments ? m_comments->commentAt(i) : QString();
            if (!trans.trimmed().isEmpty()) {
                md << QStringLiteral("> %1").arg(trans);
            }
            md << QString();
        }
        const QString text = md.join(QLatin1Char('\n')) + QLatin1Char('\n');

        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            emit operationFailed(QStringLiteral("无法写入文件：%1").arg(path));
            return false;
        }
        file.write(text.toUtf8());
        file.close();

        clearAutosaveFor(m_path);
        m_path = path;
        clearAutosaveFor(path);
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

    clearAutosaveFor(m_path);
    m_path = path;
    clearAutosaveFor(path);
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
