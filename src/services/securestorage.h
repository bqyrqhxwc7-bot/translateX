#pragma once

#include <QByteArray>
#include <QString>

class QSettings;

// 安全存储服务：对敏感设置（如 API Key）做混淆加密后再写入 QSettings。
// 设计目标：避免明文落盘，降低泄露风险（并非不可破解，但显著提高门槛）。
class SecureStorage
{
public:
    // 加密后写回 QSettings（键值）
    static void setEncryptedValue(const QString &group, const QString &key, const QString &plainText);
    // 从 QSettings 读取并解密；不存在或无法解密时返回空串
    static QString getDecryptedValue(const QString &group, const QString &key);

    // 供测试使用：直接加解密字符串
    static QByteArray encrypt(const QString &plainText);
    static QString decrypt(const QByteArray &cipher);

private:
    // 从机器标识派生密钥（同一台机器稳定，跨机器不可解密）
    static QByteArray deriveKey();
    static QByteArray machineFingerprint();
    // 稳定、与调用方 QSettings 配置无关的存储句柄（固定 INI 文件，读写一致）
    static QSettings settingsHandle();
};
