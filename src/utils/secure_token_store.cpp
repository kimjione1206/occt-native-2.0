#include "secure_token_store.h"
#include "app_config.h"

#include <QByteArray>
#include <QDebug>

namespace occt { namespace utils {

// ─── Singleton ──────────────────────────────────────────────────────────────
SecureTokenStore& SecureTokenStore::instance()
{
    static SecureTokenStore store;
    return store;
}

// ─── Common public methods ──────────────────────────────────────────────────
bool SecureTokenStore::storeToken(const QString& token)
{
    if (token.isEmpty())
        return false;

#ifdef Q_OS_WIN
    return storeTokenDPAPI(token);
#elif defined(Q_OS_MACOS)
    return storeTokenKeychain(token);
#else
    return storeTokenObfuscated(token);
#endif
}

QString SecureTokenStore::retrieveToken() const
{
#ifdef Q_OS_WIN
    QString token = retrieveTokenDPAPI();
#elif defined(Q_OS_MACOS)
    QString token = retrieveTokenKeychain();
#else
    QString token = retrieveTokenObfuscated();
#endif

    // 마이그레이션: 기존 평문 토큰이 있으면 보안 저장소로 이동
    if (token.isEmpty()) {
        const QVariant plainToken = AppConfig::instance().value(
            QStringLiteral("Update/gistToken"));
        if (plainToken.isValid() && !plainToken.toString().isEmpty()) {
            token = plainToken.toString();
            // const_cast for migration — one-time operation
            const_cast<SecureTokenStore*>(this)->storeToken(token);
            qInfo() << "[SecureTokenStore] Migrated plaintext token to secure storage.";
        }
    }

    return token;
}

bool SecureTokenStore::deleteToken()
{
    // 평문 토큰도 함께 삭제
    AppConfig::instance().setValue(QStringLiteral("Update/gistToken"), QVariant());
    AppConfig::instance().sync();

#ifdef Q_OS_WIN
    return deleteTokenDPAPI();
#elif defined(Q_OS_MACOS)
    return deleteTokenKeychain();
#else
    return deleteTokenObfuscated();
#endif
}

bool SecureTokenStore::hasToken() const
{
    return !retrieveToken().isEmpty();
}

QString SecureTokenStore::maskedToken() const
{
    const QString token = retrieveToken();
    if (token.isEmpty())
        return {};

    if (token.length() < 8)
        return QStringLiteral("********");

    return token.left(4) + QStringLiteral("****") + token.right(4);
}

// ═══════════════════════════════════════════════════════════════════════════
// Platform-specific implementations
// ═══════════════════════════════════════════════════════════════════════════

#ifdef Q_OS_WIN
// ─── Windows DPAPI ──────────────────────────────────────────────────────────
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")

bool SecureTokenStore::storeTokenDPAPI(const QString& token)
{
    QByteArray utf8 = token.toUtf8();
    DATA_BLOB input, output;
    input.pbData = reinterpret_cast<BYTE*>(utf8.data());
    input.cbData = static_cast<DWORD>(utf8.size());

    if (!CryptProtectData(&input, L"OCCT GitHub Token",
                          nullptr, nullptr, nullptr, 0, &output)) {
        qWarning() << "[SecureTokenStore] DPAPI CryptProtectData failed.";
        return false;
    }

    QByteArray encrypted(reinterpret_cast<const char*>(output.pbData),
                         static_cast<int>(output.cbData));
    LocalFree(output.pbData);

    // base64로 settings.ini에 저장
    AppConfig::instance().setValue(QStringLiteral("Update/gistTokenEncrypted"),
                                  encrypted.toBase64());
    AppConfig::instance().sync();

    // 평문 키 삭제 (마이그레이션)
    AppConfig::instance().setValue(QStringLiteral("Update/gistToken"), QVariant());
    return true;
}

QString SecureTokenStore::retrieveTokenDPAPI() const
{
    const QString b64 = AppConfig::instance().value(
        QStringLiteral("Update/gistTokenEncrypted")).toString();
    if (b64.isEmpty())
        return {};

    QByteArray encrypted = QByteArray::fromBase64(b64.toUtf8());
    DATA_BLOB input, output;
    input.pbData = reinterpret_cast<BYTE*>(encrypted.data());
    input.cbData = static_cast<DWORD>(encrypted.size());

    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr,
                            nullptr, 0, &output)) {
        qWarning() << "[SecureTokenStore] DPAPI CryptUnprotectData failed.";
        return {};
    }

    QString token = QString::fromUtf8(
        reinterpret_cast<const char*>(output.pbData),
        static_cast<int>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return token;
}

bool SecureTokenStore::deleteTokenDPAPI()
{
    AppConfig::instance().setValue(QStringLiteral("Update/gistTokenEncrypted"),
                                  QVariant());
    AppConfig::instance().sync();
    return true;
}

#elif defined(Q_OS_MACOS)
// ─── macOS Keychain ─────────────────────────────────────────────────────────
#include <Security/Security.h>

static const char* kKeychainService = "com.occt-native.github-token";
static const char* kKeychainAccount = "gist-pat";

bool SecureTokenStore::storeTokenKeychain(const QString& token)
{
    const QByteArray utf8 = token.toUtf8();

    // 기존 항목 삭제 후 새로 추가
    SecKeychainItemRef itemRef = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(strlen(kKeychainService)), kKeychainService,
        static_cast<UInt32>(strlen(kKeychainAccount)), kKeychainAccount,
        nullptr, nullptr, &itemRef);

    if (status == errSecSuccess && itemRef) {
        // 기존 항목 업데이트
        status = SecKeychainItemModifyAttributesAndData(
            itemRef, nullptr,
            static_cast<UInt32>(utf8.size()),
            utf8.constData());
        CFRelease(itemRef);
    } else {
        // 새로 추가
        status = SecKeychainAddGenericPassword(
            nullptr,
            static_cast<UInt32>(strlen(kKeychainService)), kKeychainService,
            static_cast<UInt32>(strlen(kKeychainAccount)), kKeychainAccount,
            static_cast<UInt32>(utf8.size()), utf8.constData(),
            nullptr);
    }

    if (status != errSecSuccess) {
        qWarning() << "[SecureTokenStore] Keychain store failed, OSStatus:" << status;
        return false;
    }

    // 평문 키 삭제 (마이그레이션)
    AppConfig::instance().setValue(QStringLiteral("Update/gistToken"), QVariant());
    AppConfig::instance().sync();
    return true;
}

QString SecureTokenStore::retrieveTokenKeychain() const
{
    UInt32 passwordLength = 0;
    void* passwordData = nullptr;

    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(strlen(kKeychainService)), kKeychainService,
        static_cast<UInt32>(strlen(kKeychainAccount)), kKeychainAccount,
        &passwordLength, &passwordData, nullptr);

    if (status != errSecSuccess || !passwordData)
        return {};

    QString token = QString::fromUtf8(
        static_cast<const char*>(passwordData),
        static_cast<int>(passwordLength));
    SecKeychainItemFreeContent(nullptr, passwordData);
    return token;
}

bool SecureTokenStore::deleteTokenKeychain()
{
    SecKeychainItemRef itemRef = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(strlen(kKeychainService)), kKeychainService,
        static_cast<UInt32>(strlen(kKeychainAccount)), kKeychainAccount,
        nullptr, nullptr, &itemRef);

    if (status == errSecSuccess && itemRef) {
        status = SecKeychainItemDelete(itemRef);
        CFRelease(itemRef);
    }

    return (status == errSecSuccess || status == errSecItemNotFound);
}

#else
// ─── Linux XOR + base64 (obfuscation fallback) ─────────────────────────────
static const QByteArray XOR_KEY = "OcCtNaTiVe2024!@#$";

bool SecureTokenStore::storeTokenObfuscated(const QString& token)
{
    QByteArray data = token.toUtf8();
    for (int i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ XOR_KEY[i % XOR_KEY.size()];

    AppConfig::instance().setValue(QStringLiteral("Update/gistTokenObfuscated"),
                                  data.toBase64());
    // 평문 키 삭제 (마이그레이션)
    AppConfig::instance().setValue(QStringLiteral("Update/gistToken"), QVariant());
    AppConfig::instance().sync();
    return true;
}

QString SecureTokenStore::retrieveTokenObfuscated() const
{
    const QString b64 = AppConfig::instance().value(
        QStringLiteral("Update/gistTokenObfuscated")).toString();
    if (b64.isEmpty())
        return {};

    QByteArray data = QByteArray::fromBase64(b64.toUtf8());
    for (int i = 0; i < data.size(); ++i)
        data[i] = data[i] ^ XOR_KEY[i % XOR_KEY.size()];

    return QString::fromUtf8(data);
}

bool SecureTokenStore::deleteTokenObfuscated()
{
    AppConfig::instance().setValue(QStringLiteral("Update/gistTokenObfuscated"),
                                  QVariant());
    AppConfig::instance().sync();
    return true;
}
#endif

}} // namespace occt::utils
