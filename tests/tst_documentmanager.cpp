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

QTEST_GUILESS_MAIN(TestDocumentManager)
#include "tst_documentmanager.moc"
