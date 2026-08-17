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

    // 术语自动提取（迭代4）：从行文本提取高频英文词候选。
    // - 提取 ASCII 字母序列（≥3 字母），转小写
    // - 过滤内置停用词与已收录术语
    // - 频率 ≥ minFreq，按频率降序，最多 maxCount 个
    // 返回 {word, count} 列表；中文分词暂不支持（返回空）。
    QList<QPair<QString, int>> extractCandidates(const QStringList &lines,
                                                 int minFreq = 3, int maxCount = 20) const;

private:
    QHash<QString, QString> m_terms; // source → translation
};
