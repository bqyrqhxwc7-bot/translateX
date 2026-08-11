#include "termglossary.h"

void TermGlossary::setTerm(const QString &source, const QString &translation)
{
    if (source.trimmed().isEmpty()) {
        return;
    }
    m_terms.insert(source.trimmed(), translation.trimmed());
}

void TermGlossary::removeTerm(const QString &source)
{
    m_terms.remove(source.trimmed());
}

void TermGlossary::clear()
{
    m_terms.clear();
}

int TermGlossary::size() const
{
    return m_terms.size();
}

QString TermGlossary::translationFor(const QString &source) const
{
    return m_terms.value(source.trimmed());
}

bool TermGlossary::contains(const QString &source) const
{
    return m_terms.contains(source.trimmed());
}

QList<QPair<QString, QString>> TermGlossary::terms() const
{
    QList<QPair<QString, QString>> result;
    result.reserve(m_terms.size());
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        result.append(qMakePair(it.key(), it.value()));
    }
    return result;
}

void TermGlossary::loadFromMap(const QVariantMap &map)
{
    for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
        m_terms.insert(it.key().trimmed(), it.value().toString().trimmed());
    }
}

QVariantMap TermGlossary::toMap() const
{
    QVariantMap map;
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        map.insert(it.key(), it.value());
    }
    return map;
}

QString TermGlossary::buildConstraintPrompt() const
{
    if (m_terms.isEmpty()) {
        return QString();
    }

    QStringList lines;
    lines.append(QStringLiteral("翻译时必须使用以下术语对照（严格遵守，不得意译或混用）："));
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        lines.append(QStringLiteral("  “%1” 一律译为 “%2”").arg(it.key(), it.value()));
    }
    return lines.join(QLatin1Char('\n'));
}

double TermGlossary::verify(const QString &sourceText, const QString &translatedText) const
{
    const QStringList missing = missingTerms(sourceText, translatedText);
    if (missing.isEmpty()) {
        return 1.0;
    }
    // 统计源文中出现的术语总数，计算命中率
    int total = 0;
    int hit = 0;
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        if (sourceText.contains(it.key())) {
            ++total;
            if (translatedText.contains(it.value())) {
                ++hit;
            }
        }
    }
    return total == 0 ? 1.0 : static_cast<double>(hit) / total;
}

QStringList TermGlossary::missingTerms(const QString &sourceText, const QString &translatedText) const
{
    QStringList missing;
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        if (sourceText.contains(it.key()) && !translatedText.contains(it.value())) {
            missing.append(it.key());
        }
    }
    return missing;
}
