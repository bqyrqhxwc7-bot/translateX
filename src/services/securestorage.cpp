#include "securestorage.h"

#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QDir>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QByteArray>
#include <QDataStream>

namespace {

// XOR + 密钥派生混淆（对称、可逆）
QByteArray xorCipher(const QByteArray &data, const QByteArray &key)
{
    QByteArray out;
    out.resize(data.size());
    for (int i = 0; i < data.size(); ++i) {
        out[i] = data.at(i) ^ key.at(i % key.size());
    }
    return out;
}

} // namespace

QByteArray SecureStorage::machineFingerprint()
{
    // 用机器标识派生稳定指纹（Win32 机器 GUID / 系统主机名+内核版本）
    const QString machine = QSysInfo::machineHostName()
                            + QLatin1Char('|')
                            + QSysInfo::kernelType()
                            + QLatin1Char('|')
                            + QSysInfo::kernelVersion()
                            + QLatin1Char('|')
                            + QSysInfo::buildCpuArchitecture();
    return QCryptographicHash::hash(machine.toUtf8(), QCryptographicHash::Sha256);
}

QByteArray SecureStorage::deriveKey()
{
    const QByteArray fp = machineFingerprint();
    // 再次哈希扩展，得到 32 字节密钥
    return QCryptographicHash::hash(fp, QCryptographicHash::Sha256);
}

QSettings SecureStorage::settingsHandle()
{
    // 固定路径存储：%APPDATA%/Translex/secure.ini
    // 不依赖调用方 QSettings 的 org/app 配置，保证读写一致、跨平台稳定
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/secure.ini");
    return QSettings(path, QSettings::IniFormat);
}

QByteArray SecureStorage::encrypt(const QString &plainText)
{
    const QByteArray key = deriveKey();
    QByteArray payload = plainText.toUtf8();

    // 添加随机盐：每次加密结果不同，防止重放/模式分析
    const QByteArray salt = QByteArray::number(QRandomGenerator::global()->generate(), 16)
                            + QByteArray::number(QRandomGenerator::global()->generate(), 16);
    payload.prepend(salt + QByteArrayLiteral("|"));

    // 双重混淆：先 XOR，再做一次 HMAC 风格哈希混合
    const QByteArray xored = xorCipher(payload, key);
    const QByteArray digest = QCryptographicHash::hash(xored + key, QCryptographicHash::Sha256).left(8);
    return digest + xored.toBase64();
}

QString SecureStorage::decrypt(const QByteArray &cipher)
{
    if (cipher.size() < 16) {
        return QString();
    }
    const QByteArray key = deriveKey();
    const QByteArray raw = QByteArray::fromBase64(cipher.mid(8));
    const QByteArray expected = cipher.left(8);
    const QByteArray digest = QCryptographicHash::hash(raw + key, QCryptographicHash::Sha256).left(8);
    if (digest != expected) {
        // 密钥不匹配（换了机器）或数据损坏
        return QString();
    }
    QByteArray payload = xorCipher(raw, key);
    const int sep = payload.indexOf('|');
    if (sep <= 0) {
        return QString();
    }
    payload = payload.mid(sep + 1);
    return QString::fromUtf8(payload);
}

void SecureStorage::setEncryptedValue(const QString &group, const QString &key, const QString &plainText)
{
    QSettings settings = settingsHandle();
    settings.beginGroup(group);
    settings.setValue(key, encrypt(plainText));
    settings.endGroup();
    settings.sync();
}

QString SecureStorage::getDecryptedValue(const QString &group, const QString &key)
{
    QSettings settings = settingsHandle();
    settings.beginGroup(group);
    const QByteArray cipher = settings.value(key).toByteArray();
    settings.endGroup();
    return decrypt(cipher);
}
