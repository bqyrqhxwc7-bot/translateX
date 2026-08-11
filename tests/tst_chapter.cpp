#include <QtTest>

#include "services/chapterservice.h"
#include "services/documentmodel.h"

class TestChapterService : public QObject
{
    Q_OBJECT

private slots:
    void defaultPatternChinese();
    void defaultPatternMarkdown();
    void chapterAtLine();
    void customPattern();
    void chapterTitlesList();
};

void TestChapterService::defaultPatternChinese()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("第一章 简介"), QStringLiteral("正文1"),
                     QStringLiteral("第二章 深入"), QStringLiteral("正文2"),
                     QStringLiteral("第三章 结束") });
    ChapterService cs;
    cs.setDocument(&model);
    cs.rebuild();
    QCOMPARE(cs.chapterCount(), 3);
    QCOMPARE(cs.chapterTitle(0), QStringLiteral("第一章 简介"));
    QCOMPARE(cs.chapterStartLine(0), 0);
    QCOMPARE(cs.chapterStartLine(1), 2);
    QCOMPARE(cs.chapterStartLine(2), 4);
}

void TestChapterService::defaultPatternMarkdown()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("# 标题"), QStringLiteral("内容"),
                     QStringLiteral("## 子标题"), QStringLiteral("更多") });
    ChapterService cs;
    cs.setDocument(&model);
    cs.rebuild();
    QCOMPARE(cs.chapterCount(), 2);
    QCOMPARE(cs.chapterTitle(0), QStringLiteral("# 标题"));
    QCOMPARE(cs.chapterTitle(1), QStringLiteral("## 子标题"));
}

void TestChapterService::chapterAtLine()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("第1章"), QStringLiteral("a"), QStringLiteral("b"),
                     QStringLiteral("第2章"), QStringLiteral("c") });
    ChapterService cs;
    cs.setDocument(&model);
    cs.rebuild();
    QCOMPARE(cs.chapterAtLine(0), 0);
    QCOMPARE(cs.chapterAtLine(1), 0);
    QCOMPARE(cs.chapterAtLine(2), 0);
    QCOMPARE(cs.chapterAtLine(3), 1);
    QCOMPARE(cs.chapterAtLine(4), 1);

    // 无章节文档
    DocumentModel m2;
    m2.setLines({ QStringLiteral("a"), QStringLiteral("b") });
    ChapterService cs2;
    cs2.setDocument(&m2);
    cs2.rebuild();
    QCOMPARE(cs2.chapterCount(), 0);
    QCOMPARE(cs2.chapterAtLine(0), -1);
}

void TestChapterService::customPattern()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("SECTION 1"), QStringLiteral("x"),
                     QStringLiteral("SECTION 2"), QStringLiteral("y") });
    ChapterService cs;
    cs.setDocument(&model);
    cs.setChapterPattern(QStringLiteral("^SECTION \\d+"));
    cs.rebuild();
    QCOMPARE(cs.chapterCount(), 2);
    QCOMPARE(cs.chapterTitle(0), QStringLiteral("SECTION 1"));
    QCOMPARE(cs.chapterStartLine(1), 2);
}

void TestChapterService::chapterTitlesList()
{
    DocumentModel model;
    model.setLines({ QStringLiteral("第一章"), QStringLiteral("a"), QStringLiteral("第二章") });
    ChapterService cs;
    cs.setDocument(&model);
    cs.rebuild();
    QCOMPARE(cs.chapterTitles(), QStringList({ QStringLiteral("第一章"), QStringLiteral("第二章") }));
}

QTEST_GUILESS_MAIN(TestChapterService)
#include "tst_chapter.moc"
