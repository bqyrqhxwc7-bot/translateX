#pragma once

#include <QString>
#include <QStringList>
#include <QPair>

#include "termglossary.h"

// 质量自检：对高风险翻译结果做规则校验（质量增强）。
// 不通过则标记需人工复核（不阻塞，仅降级/提示）。
struct QualityReport {
    bool passed = true;
    QStringList issues;       // 问题描述
    double score = 1.0;       // 0.0 ~ 1.0
};

class QualityGate
{
public:
    // 校验一条翻译结果
    // source: 原文, translated: 译文, glossary: 术语表（可空）
    static QualityReport evaluate(
        const QString &source,
        const QString &translated,
        const TermGlossary *glossary);

    // 规则：译文长度异常（相对原文过长/过短，中文通常更短）
    static bool lengthReasonable(const QString &source, const QString &translated);
    // 规则：数字/代码/占位符应保留
    static bool preservesTokens(const QString &source, const QString &translated);
    // 规则：纯原文回显（模型未翻译直接复制原文）
    // sameLanguage=true 时启用近似相似度检测（同语言回显嫌疑）；
    // 跨语言（中→日等共享汉字场景）只做完全一致拦截，避免误杀真实译文
    static bool notJustEcho(const QString &source, const QString &translated,
                            bool sameLanguage = false);

private:
    // 提取数字、代码片段等需要保留的 token
    static QStringList extractTokens(const QString &text);
};
