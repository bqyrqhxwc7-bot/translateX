#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>

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
    void limitedModeTrxRoundTripPreservesDisplay();
    // 迭代4：自动保存
    void autosaveWritesFileOnDirty();
    void autosaveClearedOnSave();
    void autosaveDisabledSkips();
    void autosaveLimitedModeSkips();
    void autosaveRestoreRoundTrip();
    void autosaveDiscardRemoves();
    void autosavePromptOnce();

private:
    QTemporaryDir m_temp;
    DocumentModel m_model;
    CommentService m_comments;
    DocumentManager m_mgr;
    // 触发自动保存 tick（QTimer 60s 无法在测试中等待，直接调用私有槽）
    void fireAutosaveTick() { QVERIFY(QMetaObject::invokeMethod(&m_mgr, "onAutosaveTick")); }
};

void TestDocumentManager::initTestCase()
{
    // 自动保存/recent 落盘在 AppConfigLocation：测试模式重定向到 .qttest 目录，
    // 避免污染真实用户数据目录
    QStandardPaths::setTestModeEnabled(true);
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

// 受限模式下 .trx 保存：显示层数据（rich）不丢（data() 掩蔽仅影响渲染）
void TestDocumentManager::limitedModeTrxRoundTripPreservesDisplay()
{
    m_mgr.newDocument({ QStringLiteral("行一"), QStringLiteral("行二") });
    m_model.setLineRich(0, QStringLiteral("<b>富文本</b>"));
    m_model.setLineDisplay(0, QStringLiteral("rich"));
    m_model.setLimitedMode(true);

    // 受限模式下渲染层对外纯文本，但数据保留
    QCOMPARE(m_model.data(m_model.index(0), DocumentModel::DisplayRole).toString(),
             QStringLiteral("plain"));
    QCOMPARE(m_model.richAt(0), QStringLiteral("<b>富文本</b>"));

    const QString path = m_temp.filePath(QStringLiteral("limited.trx"));
    QVERIFY(m_mgr.saveFileAs(path));

    // 新实例读回：显示层完整往返
    DocumentModel model2;
    CommentService comments2;
    DocumentManager mgr2;
    mgr2.setDocument(&model2);
    mgr2.setComments(&comments2);
    QVERIFY(mgr2.openFile(path));
    QCOMPARE(model2.lineCount(), 2);
    QCOMPARE(model2.richAt(0), QStringLiteral("<b>富文本</b>"));
    QCOMPARE(model2.displayAt(0), QStringLiteral("rich"));
}

// ---- 迭代4：自动保存 ----

// dirty 时 tick 生成 .autosave.trx（含批注），未修改时不再写
void TestDocumentManager::autosaveWritesFileOnDirty()
{
    m_mgr.discardAutosave();
    m_mgr.newDocument({ QStringLiteral("alpha"), QStringLiteral("beta") });
    m_comments.setComment(0, QStringLiteral("译文甲"));

    m_model.updateLineText(1, QStringLiteral("beta changed"));
    QVERIFY(m_mgr.isDirty());

    fireAutosaveTick();
    const QString autosave = m_mgr.autosavePath();
    QVERIFY(QFile::exists(autosave));
    QVERIFY(m_mgr.hasAutosave());

    // 未修改时再次 tick 不报错（文件仍在）
    m_mgr.saveFileAs(m_temp.filePath(QStringLiteral("docA.txt")));
    fireAutosaveTick();
    QVERIFY(!m_mgr.hasAutosave());
}

// 正常保存后清理对应自动保存文件
void TestDocumentManager::autosaveClearedOnSave()
{
    m_mgr.discardAutosave();
    m_mgr.newDocument({ QStringLiteral("x"), QStringLiteral("y") });
    m_model.updateLineText(0, QStringLiteral("edited"));
    fireAutosaveTick();
    QVERIFY(m_mgr.hasAutosave());

    QVERIFY(m_mgr.saveFileAs(m_temp.filePath(QStringLiteral("docB.txt"))));
    QVERIFY(!m_mgr.hasAutosave());
}

// 配置关闭后不自动保存
void TestDocumentManager::autosaveDisabledSkips()
{
    m_mgr.discardAutosave();
    m_mgr.setAutosaveEnabled(false);
    m_mgr.newDocument({ QStringLiteral("x") });
    m_model.updateLineText(0, QStringLiteral("edited"));
    fireAutosaveTick();
    QVERIFY(!m_mgr.hasAutosave());
    m_mgr.setAutosaveEnabled(true);
}

// 受限模式（大文件）跳过自动保存
void TestDocumentManager::autosaveLimitedModeSkips()
{
    m_mgr.discardAutosave();
    DocumentManager::setLargeFileLimits(1, 0);   // 1 行即受限
    const QString path = m_temp.filePath(QStringLiteral("big.txt"));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("a\nb\n");
    f.close();
    QVERIFY(m_mgr.openFile(path));
    QVERIFY(m_model.limitedMode());
    m_model.updateLineText(0, QStringLiteral("edited"));
    fireAutosaveTick();
    QVERIFY(!m_mgr.hasAutosave());
    DocumentManager::setLargeFileLimits(50000, 200LL * 1024 * 1024);   // 还原默认
    m_model.setLimitedMode(false);   // 还原受限模式，避免泄漏到后续用例
}

// 恢复：restoreAutosave 后行/批注一致、m_path 还原为原始文档路径（dirty）、自动保存文件被清理
void TestDocumentManager::autosaveRestoreRoundTrip()
{
    m_mgr.discardAutosave();
    const QString original = m_temp.filePath(QStringLiteral("restore.txt"));
    QVERIFY(m_mgr.saveFileAs(original));
    m_comments.setComment(1, QStringLiteral("译文乙"));
    m_model.updateLineText(0, QStringLiteral("恢复一（改）"));
    fireAutosaveTick();
    QVERIFY(m_mgr.hasAutosave());

    QVERIFY(m_mgr.restoreAutosave());
    QVERIFY(!m_mgr.hasAutosave());   // 恢复成功即清理
    // 回归：m_path 必须还原为原始文档路径（而非已删除的 autosave 文件），
    // 否则 Ctrl+S 会写进应用数据目录、自动保存失效（review fd8d1f3 🔴2）
    QCOMPARE(m_mgr.currentPath(), original);
    QVERIFY(m_mgr.isDirty());        // 恢复的内容尚未保存到原文件
    QCOMPARE(m_model.lineCount(), 2);
    QCOMPARE(m_model.lineText(0), QStringLiteral("恢复一（改）"));
    QCOMPARE(m_comments.commentAt(1), QStringLiteral("译文乙"));
}

// 启动提示只弹一次（NoStack 页面重建防护）
void TestDocumentManager::autosavePromptOnce()
{
    m_mgr.discardAutosave();
    m_mgr.newDocument({ QStringLiteral("x") });
    m_model.updateLineText(0, QStringLiteral("edited"));
    fireAutosaveTick();
    QVERIFY(m_mgr.takeAutosavePrompt());
    QVERIFY(!m_mgr.takeAutosavePrompt());   // 第二次恒 false
    QVERIFY(!m_mgr.takeAutosavePrompt());
    m_mgr.discardAutosave();
}

// 丢弃：删除全部自动保存文件
void TestDocumentManager::autosaveDiscardRemoves()
{
    m_mgr.discardAutosave();
    m_mgr.newDocument({ QStringLiteral("x") });
    m_model.updateLineText(0, QStringLiteral("edited"));
    fireAutosaveTick();
    QVERIFY(m_mgr.hasAutosave());

    m_mgr.discardAutosave();
    QVERIFY(!m_mgr.hasAutosave());
}

QTEST_GUILESS_MAIN(TestDocumentManager)
#include "tst_documentmanager.moc"
