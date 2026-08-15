#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>

#include "services/documentmanager.h"
#include "services/documentmodel.h"
#include "services/commentservice.h"

class TestDocumentManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void newDocumentDirtyFlow();
    void openSaveRoundTrip();
    void commentsPersistWithDocument();
    void saveFileAsUpdatesPath();
    void openMissingFileFails();
    void recentFilesPersist();
    void largeFileLimitedModeByLines();
    void largeFileLimitedModeBySize();
    void largeFileLimitedModeResetsOnNew();

private:
    QTemporaryDir m_temp;
    DocumentModel m_model;
    CommentService m_comments;
    DocumentManager m_mgr;
};

void TestDocumentManager::initTestCase()
{
    QVERIFY(m_temp.isValid());
    m_mgr.setDocument(&m_model);
    m_mgr.setComments(&m_comments);
}

void TestDocumentManager::newDocumentDirtyFlow()
{
    m_mgr.newDocument({ QStringLiteral("a"), QStringLiteral("b") });
    QVERIFY(!m_mgr.isDirty());
    QCOMPARE(m_model.lineCount(), 2);
    // 编辑 → dirty
    m_model.updateLineText(0, QStringLiteral("changed"));
    QVERIFY(m_mgr.isDirty());
}

void TestDocumentManager::openSaveRoundTrip()
{
    const QString path = m_temp.filePath(QStringLiteral("doc.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("line1\nline2\nline3\n");
    f.close();

    QVERIFY(m_mgr.openFile(path));
    QCOMPARE(m_mgr.currentPath(), path);
    QCOMPARE(m_model.lineCount(), 3);
    QCOMPARE(m_model.lineText(1), QStringLiteral("line2"));
    QVERIFY(!m_mgr.isDirty());
}

void TestDocumentManager::commentsPersistWithDocument()
{
    const QString path = m_temp.filePath(QStringLiteral("doc2.txt"));
    m_mgr.newDocument({ QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z") });
    m_comments.setComment(1, QStringLiteral("批注Y"));
    QVERIFY(m_mgr.saveFileAs(path));

    // 新实例打开 → 批注还原
    DocumentModel model2;
    CommentService comments2;
    DocumentManager mgr2;
    mgr2.setDocument(&model2);
    mgr2.setComments(&comments2);
    QVERIFY(mgr2.openFile(path));
    QCOMPARE(model2.lineCount(), 3);
    QVERIFY(comments2.hasCommentAt(1));
    QCOMPARE(comments2.commentAt(1), QStringLiteral("批注Y"));
}

void TestDocumentManager::saveFileAsUpdatesPath()
{
    const QString path = m_temp.filePath(QStringLiteral("doc3.txt"));
    m_mgr.newDocument({ QStringLiteral("only") });
    QVERIFY(m_mgr.saveFileAs(path));
    QCOMPARE(m_mgr.currentPath(), path);
    QCOMPARE(m_mgr.documentName(), QStringLiteral("doc3.txt"));
    QVERIFY(!m_mgr.isDirty());
}

void TestDocumentManager::openMissingFileFails()
{
    QSignalSpy spy(&m_mgr, &DocumentManager::operationFailed);
    QVERIFY(!m_mgr.openFile(m_temp.filePath(QStringLiteral("no_such.txt"))));
    QCOMPARE(spy.count(), 1);
}

void TestDocumentManager::recentFilesPersist()
{
    m_mgr.clearRecentFiles();

    const QString path = m_temp.filePath(QStringLiteral("recent_doc.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("content\n");
    f.close();

    // 打开后自动记录到最近文件
    QVERIFY(m_mgr.openFile(path));
    QVERIFY(m_mgr.recentFiles().contains(path));

    // 新实例持久化读取（跨对象）
    DocumentManager mgr2;
    QVERIFY(mgr2.recentFiles().contains(path));

    // 去重：重复打开不产生重复条目
    QVERIFY(m_mgr.openFile(path));
    int count = 0;
    for (const QString &p : m_mgr.recentFiles()) {
        if (p == path) {
            ++count;
        }
    }
    QCOMPARE(count, 1);

    m_mgr.clearRecentFiles();
    QVERIFY(m_mgr.recentFiles().isEmpty());
}

// 行数超限 → 受限模式（默认阈值 5 万行）
void TestDocumentManager::largeFileLimitedModeByLines()
{
    DocumentManager::setLargeFileLimits(50000, 200LL * 1024 * 1024);

    const QString path = m_temp.filePath(QStringLiteral("big.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QByteArray content;
    content.reserve(50001 * 16);
    for (int i = 0; i < 50001; ++i) {
        content += QStringLiteral("line %1 content\n").arg(i).toUtf8();
    }
    f.write(content);
    f.close();

    QVERIFY(m_mgr.openFile(path));
    QCOMPARE(m_model.lineCount(), 50001);
    QVERIFY(m_model.limitedMode());

    // 受限模式下显示层角色对外回退纯文本（数据保留）
    m_model.setLineRich(0, QStringLiteral("<b>x</b>"));
    m_model.setLineDisplay(0, QStringLiteral("rich"));
    QCOMPARE(m_model.data(m_model.index(0), DocumentModel::DisplayRole).toString(),
             QStringLiteral("plain"));
    QCOMPARE(m_model.richAt(0), QStringLiteral("<b>x</b>"));
}

// 体积超限 → 受限模式（注入小体积阈值）
void TestDocumentManager::largeFileLimitedModeBySize()
{
    DocumentManager::setLargeFileLimits(100000, 512);

    const QString path = m_temp.filePath(QStringLiteral("wide.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QString(600, QLatin1Char('x')).toUtf8());
    f.close();

    QVERIFY(m_mgr.openFile(path));
    QVERIFY(m_model.limitedMode());

    // 还原默认阈值
    DocumentManager::setLargeFileLimits(50000, 200LL * 1024 * 1024);
}

// 新建文档复位受限模式
void TestDocumentManager::largeFileLimitedModeResetsOnNew()
{
    DocumentManager::setLargeFileLimits(50000, 200LL * 1024 * 1024);

    const QString path = m_temp.filePath(QStringLiteral("big2.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    for (int i = 0; i < 50001; ++i) {
        f.write(QStringLiteral("l%1\n").arg(i).toUtf8());
    }
    f.close();

    QVERIFY(m_mgr.openFile(path));
    QVERIFY(m_model.limitedMode());

    m_mgr.newDocument({ QStringLiteral("a"), QStringLiteral("b") });
    QVERIFY(!m_model.limitedMode());
}

QTEST_GUILESS_MAIN(TestDocumentManager)
#include "tst_documentmanager.moc"
