#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QPair>
#include <QVariantMap>

// 术语表服务：保证翻译术语一致性（质量增强）。
// - 用户可维护 "原文术语 → 标准译文" 映射
// - 翻译前注入术语约束提示
// - 翻译后校验术语命中率
class TermGlossary
{
public:
    // 添加/更新术语
    void setTerm(const QString &source, const QString &translation);
    void removeTerm(const QString &source);
    void clear();
    int size() const;

    // 查询
    QString translationFor(const QString &source) const;
    bool contains(const QString &source) const;
    QList<QPair<QString, QString>> terms() const;

    // 批量加载（从配置/文件）
    void loadFromMap(const QVariantMap &map);
    QVariantMap toMap() const;

    // 生成提示词片段：要求模型遵守术语表
    QString buildConstraintPrompt() const;

    // 校验：译文中命中的术语（源术语标准译文是否出现）
    // 返回命中率（0.0 ~ 1.0）
    double verify(const QString &sourceText, const QString &translatedText) const;
    // 返回未命中的术语列表（用于报告）
    QStringList missingTerms(const QString &sourceText, const QString &translatedText) const;

    // 术语自动提取（迭代4，2026-08-19 增强）：从行文本提取高频词候选。
    // - 英文/标识符：ASCII 字母数字连字符（≥2 字符且含字母，如 API/C++/P2899R1/x86-64），
    //   按小写归组统计（大小写不敏感）
    // - 中文：连续 CJK 段内 2-3 字 n-gram（过滤虚词字符）
    // - 过滤内置停用词与已收录术语（大小写不敏感）
    // - 频率 ≥ minFreq，按频率降序，最多 maxCount 个
    // - 返回原文中最高频的实际书写形式（术语表校验是大小写敏感的）
    // 返回 {word, count} 列表。
    QList<QPair<QString, int>> extractCandidates(const QStringList &lines,
                                                 int minFreq = 3, int maxCount = 20) const;

private:
    bool containsCaseInsensitive(const QString &word) const;
    QHash<QString, QString> m_terms; // source → translation
};
