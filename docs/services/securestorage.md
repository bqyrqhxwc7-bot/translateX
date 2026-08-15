# SecureStorage 服务文档

> 状态：已实现
> 类型：L1 内置服务（C++ 静态工具类）

## 1. 职责

敏感设置（API Key、令牌等）的加密存储，避免明文落盘。

## 2. 安全模型

```
明文 ──toUtf8──> 加随机盐 ──XOR 密钥──> 校验哈希 ──base64──> 密文
```

- **密钥**：机器指纹（主机名+内核+架构）经 SHA-256 派生，跨机器不可解
- **随机盐**：每次加密结果不同，防重放/模式分析
- **校验哈希**：防篡改（数据损坏/密钥不符 → 返回空串，不崩溃）

## 3. API（C++ 静态方法）

| 方法 | 说明 |
| --- | --- |
| `SecureStorage::setEncryptedValue(group, key, plainText)` | 加密写盘 |
| `SecureStorage::getDecryptedValue(group, key)` | 读取解密 |
| `SecureStorage::encrypt(plainText)` | 纯加解密（测试用） |
| `SecureStorage::decrypt(cipher)` | 解密（损坏返回空串） |

## 4. 存储位置

固定 INI 文件：`%APPDATA%/Translex/secure.ini`

> 不依赖调用方 QSettings 的 org/app 配置（历史缺陷已修复：旧版用默认 QSettings 导致读写不一致）。

## 5. 使用示例

```cpp
// 保存 API Key
SecureStorage::setEncryptedValue("network", "apiKey", userInputKey);

// 读取
const QString key = SecureStorage::getDecryptedValue("network", "apiKey");
```

## 6. 边界与限制

- **混淆而非强加密**：密钥派生自本机公开信息，能防"普通查看"，非防"定向破解"。如需更高强度，可升级到系统凭据库（Windows Credential Manager）。
- **换机迁移**：换机器后无法解密（安全特性，非缺陷）。
- **损坏容错**：任何异常输入返回空串，绝不崩溃。

## 7. 测试

见 `tests/tst_securestorage.cpp`：
- 加解密往返
- 篡改检测
- 明文不落盘（直接读 INI 文件验证）
- 相同明文不同密文（盐随机性）
