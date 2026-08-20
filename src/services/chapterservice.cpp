#include "chapterservice.h"

#include "documentmodel.h"

#include <QRegularExpression>
#include <QTimer>

namespace {

// 行 rich HTML 中最大的 font-size（pt）；无 rich 返回 0
double maxRichFontSize(const QString &rich)
{
    if (rich.isEmpty()) {
        return 0;
    }
    static const QRegularExpression szRe(QStringLiteral("font-size:([0-9.]+)pt"));
    double max = 0;
    QRegularExpressionMatchIterator it = szRe.globalMatch(rich);
    while (it.hasNext()) {
        max = qMax(max, it.next().captured(1).toDouble());
    }
    return max;
}

} // namespace

ChapterService::ChapterService(QObject *parent)
    : QObject(parent)
    , m_pattern(QStringLiteral("(^#{1,6}\\s.*)|(第[一二三四五六七八九十百千万0-9]+[章节篇回])"))
    , m_debounceTimer(new QTimer(this))
{
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(250);
    connect(m_debounceTimer, &QTimer::timeout, this, &ChapterService::rebuild);
}

QString ChapterService::serviceId() const
{
    return QStringLiteral("chapter");
}

QString ChapterService::displayName() const
{
    return QStringLiteral("章节服务");
}

QString ChapterService::serviceVersion() const
{
    return QStringLiteral("1.0");
}

QVariantMap ChapterService::healthCheck() const
{
    return { { QStringLiteral("status"), QStringLiteral("ok") },
             { QStringLiteral("message"), QStringLiteral("章节 %1 个").arg(chapterCount()) } };
}

void ChapterService::setDocument(DocumentModel *model)
{
    if (m_model) {
        disconnect(m_model, &DocumentModel::lineCountChanged, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        // 行增删/清空/重建后防抖自动重建章节索引（QPointer：模型销毁自动断开）
        connect(m_model, &DocumentModel::lineCountChanged, this, [this] {
            m_debounceTimer->start();
        });
    }
}

void ChapterService::setChapterPattern(const QString &regex)
{
    QRegularExpression re(regex);
    if (re.isValid()) {
        m_pattern = re;
    }
}

QString ChapterService::chapterPattern() const
{
    return m_pattern.pattern();
}

void ChapterService::rebuild()
{
    m_chapters.clear();
    if (m_model) {
        // 富文本标题特征（PDF/docx 导入的行无 markdown/中文编号标记）：
        // 先统计字号众数（正文基准），标题 = 字号 ≥ 基准×1.15 或 短行加粗。
        QHash<int, int> sizeFreq;
        for (int i = 0; i < m_model->lineCount(); ++i) {
            const double s = maxRichFontSize(m_model->richAt(i));
            if (s > 0) {
                ++sizeFreq[qRound(s)];
            }
        }
        int modeSize = 0;
        int best = 0;
        for (auto it = sizeFreq.constBegin(); it != sizeFreq.constEnd(); ++it) {
            if (it.value() > best) {
                best = it.value();
                modeSize = it.key();
            }
        }

        for (int i = 0; i < m_model->lineCount(); ++i) {
            const QString text = m_model->lineText(i);
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty()) {
                continue;
            }
            bool heading = m_pattern.match(text).hasMatch();
            if (!heading) {
                const QString rich = m_model->richAt(i);
                if (rich.isEmpty() || trimmed.size() > 60) {
                    continue;
                }
                const double s = maxRichFontSize(rich);
                if (modeSize > 0 && s >= modeSize * 1.15) {
                    heading = true;   // 大字标题（章节/文档主标题）
                } else if (rich.contains(QStringLiteral("<b>")) && trimmed.size() <= 60) {
                    heading = true;   // 短行加粗（"Abstract"/"Contents" 等）
                }
                // 目录条目过滤：样式命中的行若「数字开头 + 行尾紧贴页码」是目录
                //（如 "1 Introduction4"）→ 排除（正文章节标题无尾随页码）
                if (heading && trimmed.at(0).isDigit()
                    && trimmed.at(trimmed.size() - 1).isDigit()) {
                    heading = false;
                }
            }
            if (heading) {
                m_chapters.append({ i, trimmed });
            }
        }
    }
    emit chaptersChanged();
}

int ChapterService::chapterCount() const
{
    return m_chapters.size();
}

QString ChapterService::chapterTitle(int index) const
{
    if (index < 0 || index >= m_chapters.size()) {
        return QString();
    }
    return m_chapters.at(index).title;
}

int ChapterService::chapterStartLine(int index) const
{
    if (index < 0 || index >= m_chapters.size()) {
        return -1;
    }
    return m_chapters.at(index).startLine;
}

int ChapterService::chapterAtLine(int lineNumber) const
{
    int lo = 0;
    int hi = m_chapters.size() - 1;
    int answer = -1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (m_chapters.at(mid).startLine <= lineNumber) {
            answer = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return answer;
}

QStringList ChapterService::chapterTitles() const
{
    QStringList titles;
    titles.reserve(m_chapters.size());
    for (const Chapter &ch : m_chapters) {
        titles.append(ch.title);
    }
    return titles;
}
