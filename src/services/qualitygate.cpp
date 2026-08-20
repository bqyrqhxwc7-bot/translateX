#include "qualitygate.h"

#include <QRegularExpression>
#include <QSet>
#include <QVector>

QualityReport QualityGate::evaluate(
    const QString &source,
    const QString &translated,
    const TermGlossary *glossary)
{
    QualityReport report;

    if (translated.trimmed().isEmpty()) {
        report.passed = false;
        report.score = 0.0;
        report.issues.append(QStringLiteral("译文为空"));
        return report;
    }

    // 规则 1：纯回显（模型没翻译）
    if (!notJustEcho(source, translated)) {
        report.passed = false;
        report.score = qMin(report.score, 0.1);
        report.issues.append(QStringLiteral("译文疑似直接复制原文，未翻译"));
        return report;
    }

    // 规则 2：长度异常
    if (!lengthReasonable(source, translated)) {
        report.score = qMin(report.score, 0.5);
        report.issues.append(QStringLiteral("译文长度异常（过长或过短）"));
    }

    // 规则 3：保留数字/代码 token
    if (!preservesTokens(source, translated)) {
        report.score = qMin(report.score, 0.6);
        report.issues.append(QStringLiteral("译文丢失了数字/代码标记"));
    }

    // 规则 4：术语一致性
    if (glossary && glossary->size() > 0) {
        const double termScore = glossary->verify(source, translated);
        report.score = qMin(report.score, termScore);
        const QStringList missing = glossary->missingTerms(source, translated);
        if (!missing.isEmpty()) {
            report.issues.append(QStringLiteral("术语未按标准翻译：%1").arg(missing.join(QStringLiteral("、"))));
        }
    }

    report.passed = report.score >= 0.6;
    return report;
}

QStringList QualityGate::extractTokens(const QString &text)
{
    QStringList tokens;
    // 需保留的 token 仅限：数字/版本号、占位符、代码标识符（驼峰/下划线/含数字/全大写）。
    // 普通英文单词不视为 token（英→中会被正常翻译掉，不应触发告警）。
    static const QRegularExpression tokenRe(
        QStringLiteral(
            "\\b\\d+(?:\\.\\d+)?\\b"                    // 数字 / 小数 / 版本号
            "|%[A-Za-z0-9_.-]+|\\{[^}]+\\}|<[^>]+>|\\$[A-Za-z_][A-Za-z0-9_]*" // 占位符
            "|\\b[A-Za-z_][A-Za-z0-9_]*[A-Z][A-Za-z0-9_]*\\b" // 驼峰标识符
            "|\\b[A-Za-z0-9_]*_[A-Za-z0-9_]+\\b"         // 下划线标识符
            "|\\b[A-Za-z_][A-Za-z0-9_]*\\d[A-Za-z0-9_]*\\b" // 含数字标识符
            "|\\b[A-Z]{2,}\\b"));                        // 全大写缩写（API、HTTP…）
    auto it = tokenRe.globalMatch(text);
    while (it.hasNext()) {
        tokens.append(it.next().captured(0));
    }
    return tokens;
}

// 原文是否含实质可翻译内容：去除数字/型号/代码 token 后仍有 ≥2 连续字母（普通词）
// 或 CJK 字符 → 可翻译；仅剩数字/型号/标点（"P2899R1"、"2025-03-14"）→ 不可翻译。
bool QualityGate::hasTranslatableContent(const QString &text)
{
    QString stripped = text;
    const QStringList tokens = extractTokens(text);
    for (const QString &t : tokens) {
        stripped.remove(t);
    }
    static const QRegularExpression wordRe(
        QStringLiteral("[A-Za-z]{2,}|[\\x{4e00}-\\x{9fff}]"));
    return wordRe.match(stripped).hasMatch();
}

bool QualityGate::lengthReasonable(const QString &source, const QString &translated)
{
    const int sourceLen = source.size();
    const int targetLen = translated.size();
    if (sourceLen <= 0) {
        return true;
    }
    // 短行（≤8 字符：型号/编号/单词）长度比天然波动大（"OK"→"好" 0.5x、
    // "AI"→"人工智能" 3x），且纯数字/型号行译文本就等长——放宽阈值（0.1x~8x）
    // 而非完全跳过：极端异常（"hi"→30 字 15x）仍能拦截
    if (sourceLen <= 8 || !hasTranslatableContent(source)) {
        const double loose = static_cast<double>(targetLen) / sourceLen;
        return loose >= 0.1 && loose <= 8.0;
    }
    // 中文通常更紧凑（英→中常见 0.25~0.5 倍）；允许 0.2x ~ 4x 范围
    const double ratio = static_cast<double>(targetLen) / sourceLen;
    return ratio >= 0.2 && ratio <= 4.0;
}

bool QualityGate::preservesTokens(const QString &source, const QString &translated)
{
    const QStringList tokens = extractTokens(source);
    if (tokens.isEmpty()) {
        return true;
    }
    QSet<QString> preserved;
    for (const QString &token : tokens) {
        if (translated.contains(token)) {
            preserved.insert(token);
        }
    }
    // 至少保留 80% 的 token
    return static_cast<double>(preserved.size()) / tokens.size() >= 0.8;
}

// 编辑距离（Levenshtein，O(n*m) DP，行文本较短可接受）
static int editDistance(const QString &a, const QString &b)
{
    const int n = a.size();
    const int m = b.size();
    if (n == 0) {
        return m;
    }
    if (m == 0) {
        return n;
    }
    QVector<int> prev(m + 1), cur(m + 1);
    for (int j = 0; j <= m; ++j) {
        prev[j] = j;
    }
    for (int i = 1; i <= n; ++i) {
        cur[0] = i;
        for (int j = 1; j <= m; ++j) {
            const int cost = (a.at(i - 1) == b.at(j - 1)) ? 0 : 1;
            cur[j] = qMin(qMin(cur[j - 1] + 1, prev[j] + 1), prev[j - 1] + cost);
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

bool QualityGate::notJustEcho(const QString &source, const QString &translated,
                              bool sameLanguage)
{
    const QString src = source.trimmed();
    const QString tgt = translated.trimmed();
    if (src.isEmpty()) {
        return true;
    }
    // 原文无实质可翻译内容（仅数字/型号/代码标识符/标点，如 "P2899R1"、"2025-03-14"）：
    // 译文与原文一致是正确翻译（专名/数字本就不译），不算未翻译
    if (!hasTranslatableContent(src)) {
        return true;
    }
    // 完全一致视为未翻译（任何场景）
    if (src == tgt) {
        return false;
    }
    // 跨语言场景（中→日等共享汉字/字符时相似度天然高）：不做近似检测，
    // 否则真实译文被误判为“疑似未翻译”（qualitygate 的经典误杀，见 translation-service.md）
    if (!sameLanguage) {
        return true;
    }
    // 近似回显：编辑距离归一化相似度 > 0.85 视为未翻译
    // （覆盖回显时仅空格/标点等细微差异，如后端对非源语言文本原样返回）
    const int maxLen = qMax(src.size(), tgt.size());
    if (maxLen <= 0) {
        return true;
    }
    const double similarity = 1.0 - static_cast<double>(editDistance(src, tgt)) / maxLen;
    return similarity < 0.85;
}
