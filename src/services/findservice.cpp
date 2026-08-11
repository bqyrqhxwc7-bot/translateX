#include "findservice.h"

#include "documentmodel.h"

FindService::FindService(QObject *parent)
    : QObject(parent)
{
}

void FindService::setDocument(DocumentModel *model)
{
    m_model = model;
}

QRegularExpression FindService::makePattern(
    const QString &query, bool caseSensitive, bool wholeWord) const
{
    QString pattern = QRegularExpression::escape(query);
    if (wholeWord) {
        pattern = QStringLiteral("\\b") + pattern + QStringLiteral("\\b");
    }
    QRegularExpression re(pattern);
    re.setPatternOptions(caseSensitive ? QRegularExpression::NoPatternOption
                                       : QRegularExpression::CaseInsensitiveOption);
    return re;
}

QList<int> FindService::find(const QString &query, bool caseSensitive, bool wholeWord)
{
    QList<int> lines;
    if (!query.isEmpty() && m_model) {
        const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
        for (int i = 0; i < m_model->lineCount(); ++i) {
            if (re.match(m_model->lineText(i)).hasMatch()) {
                lines.append(i);
            }
        }
    }
    emit searchCompleted(lines.size());
    return lines;
}

int FindService::count(const QString &query, bool caseSensitive, bool wholeWord)
{
    int total = 0;
    if (!query.isEmpty() && m_model) {
        const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
        for (int i = 0; i < m_model->lineCount(); ++i) {
            auto it = re.globalMatch(m_model->lineText(i));
            while (it.hasNext()) {
                it.next();
                ++total;
            }
        }
    }
    emit searchCompleted(total);
    return total;
}

int FindService::findNext(const QString &query, int fromLine, bool caseSensitive,
                          bool wholeWord, bool wrap) const
{
    if (query.isEmpty() || !m_model) {
        return -1;
    }
    const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
    const int n = m_model->lineCount();
    for (int i = qMax(0, fromLine); i < n; ++i) {
        if (re.match(m_model->lineText(i)).hasMatch()) {
            return i;
        }
    }
    if (wrap) {
        for (int i = 0; i < qMax(0, fromLine) && i < n; ++i) {
            if (re.match(m_model->lineText(i)).hasMatch()) {
                return i;
            }
        }
    }
    return -1;
}

int FindService::findPrevious(const QString &query, int fromLine, bool caseSensitive,
                              bool wholeWord, bool wrap) const
{
    if (query.isEmpty() || !m_model) {
        return -1;
    }
    const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
    for (int i = qMin(fromLine, m_model->lineCount() - 1); i >= 0; --i) {
        if (re.match(m_model->lineText(i)).hasMatch()) {
            return i;
        }
    }
    if (wrap) {
        for (int i = m_model->lineCount() - 1; i > qMin(fromLine, m_model->lineCount() - 1); --i) {
            if (re.match(m_model->lineText(i)).hasMatch()) {
                return i;
            }
        }
    }
    return -1;
}

bool FindService::replaceLine(int lineNumber, const QString &query, const QString &replacement,
                              bool caseSensitive, bool wholeWord)
{
    if (!m_model || query.isEmpty() || lineNumber < 0 || lineNumber >= m_model->lineCount()) {
        return false;
    }
    const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
    const QString text = m_model->lineText(lineNumber);
    QString newText = text;
    newText.replace(re, replacement);
    if (newText == text) {
        return false;
    }
    m_model->updateLineText(lineNumber, newText);
    return true;
}

int FindService::replaceAll(const QString &query, const QString &replacement,
                            bool caseSensitive, bool wholeWord)
{
    if (query.isEmpty() || !m_model) {
        return 0;
    }
    const QRegularExpression re = makePattern(query, caseSensitive, wholeWord);
    int total = 0;
    for (int i = 0; i < m_model->lineCount(); ++i) {
        const QString text = m_model->lineText(i);
        int matches = 0;
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            it.next();
            ++matches;
        }
        if (matches > 0) {
            QString newText = text;
            newText.replace(re, replacement);
            m_model->updateLineText(i, newText);
            total += matches;
        }
    }
    emit searchCompleted(total);
    return total;
}
