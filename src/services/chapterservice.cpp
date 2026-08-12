#include "chapterservice.h"

#include "documentmodel.h"

#include <QTimer>

ChapterService::ChapterService(QObject *parent)
    : QObject(parent)
    , m_pattern(QStringLiteral("(^#{1,6}\\s.*)|(第[一二三四五六七八九十百千万0-9]+[章节篇回])"))
    , m_debounceTimer(new QTimer(this))
{
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(250);
    connect(m_debounceTimer, &QTimer::timeout, this, &ChapterService::rebuild);
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
        for (int i = 0; i < m_model->lineCount(); ++i) {
            const QString text = m_model->lineText(i);
            if (m_pattern.match(text).hasMatch()) {
                m_chapters.append({ i, text.trimmed() });
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
