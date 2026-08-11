#include <QtTest>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

#include "services/securestorage.h"

class TestSecureStorage : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void emptyInput();
    void tamperDetection();
    void noPlaintextOnDisk();
    void uniqueCipherPerCall();
};

void TestSecureStorage::roundTrip()
{
    const QString secret = QStringLiteral("sk-abcdef1234567890-DeepSeek-API-Key");
    const QByteArray cipher = SecureStorage::encrypt(secret);
    QVERIFY(!cipher.isEmpty());
    QCOMPARE(SecureStorage::decrypt(cipher), secret);
}

void TestSecureStorage::emptyInput()
{
    const QByteArray cipher = SecureStorage::encrypt(QString());
    QVERIFY(SecureStorage::decrypt(cipher).isEmpty());
    // 空/损坏密文返回空串而非崩溃
    QVERIFY(SecureStorage::decrypt(QByteArray()).isEmpty());
    QVERIFY(SecureStorage::decrypt(QByteArrayLiteral("short")).isEmpty());
    QVERIFY(SecureStorage::decrypt(QByteArrayLiteral("!@#$%^&*()_+invalid")).isEmpty());
}

void TestSecureStorage::tamperDetection()
{
    const QString secret = QStringLiteral("my-secret-value");
    const QByteArray cipher = SecureStorage::encrypt(secret);

    // 篡改数据区中间字节（不是 base64 填充区），应导致校验失败
    QVERIFY(cipher.size() > 12);
    QByteArray tampered = cipher;
    tampered[10] = static_cast<char>(tampered.constData()[10] ^ 0x01);
    const QString result = SecureStorage::decrypt(tampered);
    // 篡改后要么失败为空，要么与原文不同（绝不能返回原文）
    QVERIFY(result.isEmpty() || result != secret);
}

void TestSecureStorage::noPlaintextOnDisk()
{
    // 通过 SecureStorage 写入固定 INI，然后直接读原始文件内容
    // 确认磁盘上不是明文
    const QString group = QStringLiteral("testSecureGroup");
    const QString key = QStringLiteral("apiKey");
    const QString secret = QStringLiteral("sk-plaintext-should-not-appear");

    SecureStorage::setEncryptedValue(group, key, secret);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString path = dir + QStringLiteral("/secure.ini");
    QVERIFY2(QFile::exists(path), "secure.ini 应已创建");

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    file.close();

    // 磁盘上绝不能出现明文
    QVERIFY(!content.contains("sk-plaintext-should-not-appear"));

    // 解密回读应一致
    QCOMPARE(SecureStorage::getDecryptedValue(group, key), secret);

    // 清理
    QSettings settings(path, QSettings::IniFormat);
    settings.remove(group);
    settings.sync();
}

void TestSecureStorage::uniqueCipherPerCall()
{
    // 相同明文每次加密结果应不同（含随机盐）
    const QString secret = QStringLiteral("same-value");
    const QByteArray c1 = SecureStorage::encrypt(secret);
    const QByteArray c2 = SecureStorage::encrypt(secret);
    QVERIFY(c1 != c2);
    QCOMPARE(SecureStorage::decrypt(c1), secret);
    QCOMPARE(SecureStorage::decrypt(c2), secret);
}

QTEST_GUILESS_MAIN(TestSecureStorage)
#include "tst_securestorage.moc"
