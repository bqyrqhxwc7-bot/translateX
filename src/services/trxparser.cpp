#include "trxparser.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

#include "documentmodel.h"
#include "commentservice.h"

namespace {

const QString kTrxTag = QStringLiteral("translateX");
const int kTrxVersion = 1;

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

// QStringList ↔ QJsonArray
QJsonArray idsToJson(const QStringList &ids)
{
    QJsonArray arr;
    for (const QString &id : ids) {
        arr.append(id);
    }
    return arr;
}

QStringList idsFromJson(const QJsonValue &value)
{
    QStringList ids;
    if (!value.isArray()) {
        return ids;
    }
    const QJsonArray arr = value.toArray();
    for (const QJsonValue &v : arr) {
        if (v.isString()) {
            ids.append(v.toString());
        }
    }
    return ids;
}

} // namespace

bool TrxParser::read(const QString &path, DocumentModel *model,
                     CommentService *comments, QVariantMap &meta, QString *error)
{
    if (!model) {
        setError(error, QStringLiteral("未关联文档模型"));
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法打开文件：%1").arg(path));
        return false;
    }
    const QByteArray raw = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(error, QStringLiteral("不是有效的 .trx 文件（JSON 解析失败）"));
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("trx")).toString() != kTrxTag) {
        setError(error, QStringLiteral("不是 translateX 文档（缺少 trx 标记）"));
        return false;
    }

    // meta（原样保真，供写回）
    meta = root.value(QLatin1String("meta")).toObject().toVariantMap();

    // lines → 编辑层（text）
    QStringList texts;
    QVector<QPair<int, QString>> richLines;      // 行号 → rich
    QVector<QPair<int, QStringList>> imageLines; // 行号 → imageIds
    const QJsonArray lines = root.value(QLatin1String("lines")).toArray();
    for (const QJsonValue &lv : lines) {
        const QJsonObject lo = lv.toObject();
        texts.append(lo.value(QLatin1String("text")).toString());
    }
    model->setLines(texts);

    // 显示层：rich / image（setLines 后行号一致）
    const int n = qMin<int>(lines.size(), model->lineCount());
    for (int i = 0; i < n; ++i) {
        const QJsonObject lo = lines.at(i).toObject();
        const QString display = lo.value(QLatin1String("display")).toString();
        const QString rich = lo.value(QLatin1String("rich")).toString();
        const QStringList ids = idsFromJson(lo.value(QLatin1String("imageIds")));
        if (!rich.isEmpty()) {
            model->setLineRich(i, rich);
        }
        if (!ids.isEmpty()) {
            model->setLineImages(i, ids);
        }
        // 显式 display 优先；纯文本行保持默认 plain
        if (display == QLatin1String("rich") || display == QLatin1String("image")) {
            model->setLineDisplay(i, display);
        } else if (!rich.isEmpty()) {
            model->setLineDisplay(i, QStringLiteral("rich"));
        } else if (!ids.isEmpty()) {
            model->setLineDisplay(i, QStringLiteral("image"));
        }
    }

    // comments（逐行一条纯文本）
    if (comments) {
        comments->clear();
        const QJsonObject commentsObj = root.value(QLatin1String("comments")).toObject();
        for (auto it = commentsObj.constBegin(); it != commentsObj.constEnd(); ++it) {
            bool ok = false;
            const int lineNumber = it.key().toInt(&ok);
            if (ok && it.value().isString()) {
                comments->setComment(lineNumber, it.value().toString());
            }
        }
    }

    return true;
}

bool TrxParser::write(const QString &path, const DocumentModel *model,
                      const CommentService *comments, QVariantMap &meta,
                      QString *error)
{
    if (!model) {
        setError(error, QStringLiteral("未关联文档模型"));
        return false;
    }

    QJsonObject root;
    root.insert(QLatin1String("trx"), kTrxTag);
    root.insert(QLatin1String("version"), kTrxVersion);

    // meta：原样保留 + 更新时间戳/源文件名，并回填调用方
    QJsonObject metaObj = QJsonObject::fromVariantMap(meta);
    metaObj.insert(QLatin1String("importedAt"),
                   QDateTime::currentDateTime().toString(Qt::ISODate));
    metaObj.insert(QLatin1String("sourceFile"), QFileInfo(path).fileName());
    root.insert(QLatin1String("meta"), metaObj);
    meta = metaObj.toVariantMap();

    // lines：text 编辑层 + 显示层（display/rich/imageIds）
    QJsonArray lines;
    for (int i = 0; i < model->lineCount(); ++i) {
        QJsonObject lo;
        lo.insert(QLatin1String("text"), model->lineText(i));
        const QString display = model->displayAt(i);
        const QString rich = model->richAt(i);
        const QStringList ids = model->imageIdsAt(i);
        if (display == QLatin1String("rich") || display == QLatin1String("image")) {
            lo.insert(QLatin1String("display"), display);
        }
        if (!rich.isEmpty()) {
            lo.insert(QLatin1String("rich"), rich);
        }
        if (!ids.isEmpty()) {
            lo.insert(QLatin1String("imageIds"), idsToJson(ids));
        }
        lines.append(lo);
    }
    root.insert(QLatin1String("lines"), lines);

    // comments：行号 → 文本
    if (comments) {
        QJsonObject commentsObj;
        const QVariantMap all = comments->allComments();
        for (auto it = all.constBegin(); it != all.constEnd(); ++it) {
            commentsObj.insert(it.key(), it.value().toString());
        }
        root.insert(QLatin1String("comments"), commentsObj);
    }

    const QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入文件：%1").arg(path));
        return false;
    }
    file.write(data);
    file.close();
    return true;
}
