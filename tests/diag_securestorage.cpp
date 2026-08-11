#include <QCoreApplication>
#include <QSettings>
#include <QDebug>
#include "services/securestorage.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString secret = QStringLiteral("sk-test-123456");
    const QByteArray cipher = SecureStorage::encrypt(secret);
    qInfo() << "cipher size:" << cipher.size() << "hex first16:" << cipher.left(16).toHex();

    const QString dec = SecureStorage::decrypt(cipher);
    qInfo() << "roundtrip match:" << (dec == secret);

    // 篡改中间字节
    QByteArray tampered = cipher;
    tampered[10] = tampered[10] ^ 0x01;
    const QString tamperedDec = SecureStorage::decrypt(tampered);
    qInfo() << "tampered decrypt empty:" << tamperedDec.isEmpty() << "==" << tamperedDec;

    // QSettings roundtrip
    SecureStorage::setEncryptedValue("diagGroup", "key", secret);
    QSettings settings;
    settings.beginGroup("diagGroup");
    const QVariant raw = settings.value("key");
    settings.endGroup();
    settings.remove("diagGroup");
    qInfo() << "settings raw type:" << raw.typeName() << "size:" << raw.toByteArray().size();
    qInfo() << "settings decrypt:" << SecureStorage::getDecryptedValue("diagGroup", "key");

    return 0;
}
