#include "termglossary.h"

#include <algorithm>

#include <QRegularExpression>
#include <QSet>

namespace {
// 英文停用词（提取候选时过滤；中文分词暂不支持）
const QSet<QString> &stopWords()
{
    static const QSet<QString> words = {
        QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("the"),
        QStringLiteral("and"), QStringLiteral("or"), QStringLiteral("but"),
        QStringLiteral("of"), QStringLiteral("to"), QStringLiteral("in"),
        QStringLiteral("on"), QStringLiteral("at"), QStringLiteral("for"),
        QStringLiteral("with"), QStringLiteral("from"), QStringLiteral("by"),
        QStringLiteral("is"), QStringLiteral("are"), QStringLiteral("was"),
        QStringLiteral("were"), QStringLiteral("be"), QStringLiteral("been"),
        QStringLiteral("being"), QStringLiteral("has"), QStringLiteral("have"),
        QStringLiteral("had"), QStringLiteral("do"), QStringLiteral("does"),
        QStringLiteral("did"), QStringLiteral("this"), QStringLiteral("that"),
        QStringLiteral("these"), QStringLiteral("those"), QStringLiteral("it"),
        QStringLiteral("its"), QStringLiteral("not"), QStringLiteral("so"),
        QStringLiteral("if"), QStringLiteral("then"), QStringLiteral("than"),
        QStringLiteral("as"), QStringLiteral("also"), QStringLiteral("only"),
        QStringLiteral("more"), QStringLiteral("most"), QStringLiteral("such"),
        QStringLiteral("will"), QStringLiteral("would"), QStringLiteral("can"),
        QStringLiteral("could"), QStringLiteral("should"), QStringLiteral("may"),
        QStringLiteral("might"), QStringLiteral("very"), QStringLiteral("just"),
        QStringLiteral("about"), QStringLiteral("into"), QStringLiteral("over"),
        QStringLiteral("under"), QStringLiteral("after"), QStringLiteral("before"),
        QStringLiteral("while"), QStringLiteral("when"), QStringLiteral("where"),
        QStringLiteral("what"), QStringLiteral("which"), QStringLiteral("who"),
        QStringLiteral("whom"), QStringLiteral("why"), QStringLiteral("how"),
        QStringLiteral("you"), QStringLiteral("your"), QStringLiteral("yours"),
        QStringLiteral("he"), QStringLiteral("his"), QStringLiteral("him"),
        QStringLiteral("she"), QStringLiteral("her"), QStringLiteral("they"),
        QStringLiteral("their"), QStringLiteral("them"), QStringLiteral("we"),
        QStringLiteral("our"), QStringLiteral("ours"), QStringLiteral("i"),
        QStringLiteral("my"), QStringLiteral("me"), QStringLiteral("all"),
        QStringLiteral("any"), QStringLiteral("each"), QStringLiteral("every"),
        QStringLiteral("some"), QStringLiteral("no"), QStringLiteral("nor"),
        QStringLiteral("too"), QStringLiteral("very"), QStringLiteral("up"),
        QStringLiteral("down"), QStringLiteral("out"), QStringLiteral("off"),
        QStringLiteral("again"), QStringLiteral("further"), QStringLiteral("once"),
        QStringLiteral("here"), QStringLiteral("there"), QStringLiteral("because"),
        QStringLiteral("until"), QStringLiteral("while"), QStringLiteral("both"),
        QStringLiteral("each"), QStringLiteral("few"), QStringLiteral("own"),
        QStringLiteral("same"), QStringLiteral("other"), QStringLiteral("another"),
        QStringLiteral("much"), QStringLiteral("many"), QStringLiteral("such"),
        QStringLiteral("than"), QStringLiteral("too"), QStringLiteral("very"),
        QStringLiteral("can"), QStringLiteral("will"), QStringLiteral("just"),
        QStringLiteral("don"), QStringLiteral("should"), QStringLiteral("now"),
    };
    return words;
}
} // namespace

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
        // 空译文 = 占位（自动提取未填标准译文），不注入提示词
        if (it.value().isEmpty()) {
            continue;
        }
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
        if (it.value().isEmpty()) {
            continue;   // 空译文占位不参与校验
        }
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
        if (it.value().isEmpty()) {
            continue;   // 空译文占位不参与校验
        }
        if (sourceText.contains(it.key()) && !translatedText.contains(it.value())) {
            missing.append(it.key());
        }
    }
    return missing;
}

QList<QPair<QString, int>> TermGlossary::extractCandidates(const QStringList &lines,
                                                           int minFreq, int maxCount) const
{
    QList<QPair<QString, int>> result;
    if (minFreq < 1) {
        minFreq = 1;
    }
    if (maxCount < 1) {
        maxCount = 1;
    }
    const QRegularExpression wordRe(QStringLiteral("[A-Za-z]{3,}"));
    // lower → {原始书写形式 → 次数}：统计按小写归组（大小写不敏感），
    // 但返回原文中最高频的实际书写形式（术语表校验是大小写敏感的）
    QHash<QString, QHash<QString, int>> formFreq;
    for (const QString &line : lines) {
        auto it = wordRe.globalMatch(line);
        while (it.hasNext()) {
            const QString raw = it.next().captured();
            const QString lower = raw.toLower();
            if (stopWords().contains(lower) || containsCaseInsensitive(lower)) {
                continue;
            }
            ++formFreq[lower][raw];
        }
    }
    // 频率 ≥ minFreq，按频率降序（同频按字母序稳定）
    QList<QPair<QString, int>> candidates;
    for (auto it = formFreq.constBegin(); it != formFreq.constEnd(); ++it) {
        int total = 0;
        QString best;
        int bestCount = 0;
        for (auto f = it.value().constBegin(); f != it.value().constEnd(); ++f) {
            total += f.value();
            if (f.value() > bestCount) {
                bestCount = f.value();
                best = f.key();
            }
        }
        if (total >= minFreq) {
            candidates.append(qMakePair(best, total));
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const QPair<QString, int> &a, const QPair<QString, int> &b) {
                  if (a.second != b.second) {
                      return a.second > b.second;
                  }
                  return a.first.compare(b.first, Qt::CaseInsensitive) < 0;
              });
    for (int i = 0; i < candidates.size() && i < maxCount; ++i) {
        result.append(candidates.at(i));
    }
    return result;
}

bool TermGlossary::containsCaseInsensitive(const QString &word) const
{
    for (auto it = m_terms.constBegin(); it != m_terms.constEnd(); ++it) {
        if (it.key().compare(word, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}
