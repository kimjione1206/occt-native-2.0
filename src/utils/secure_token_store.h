#pragma once
#include <QString>

namespace occt { namespace utils {

class SecureTokenStore {
public:
    static SecureTokenStore& instance();

    bool storeToken(const QString& token);
    QString retrieveToken() const;
    bool deleteToken();
    bool hasToken() const;

    /// 마스킹 표시: "ghp_****...Ab3f" (앞4+뒤4만 표시)
    QString maskedToken() const;

private:
    SecureTokenStore() = default;
    ~SecureTokenStore() = default;
    SecureTokenStore(const SecureTokenStore&) = delete;
    SecureTokenStore& operator=(const SecureTokenStore&) = delete;

#ifdef Q_OS_WIN
    bool storeTokenDPAPI(const QString& token);
    QString retrieveTokenDPAPI() const;
    bool deleteTokenDPAPI();
#elif defined(Q_OS_MACOS)
    bool storeTokenKeychain(const QString& token);
    QString retrieveTokenKeychain() const;
    bool deleteTokenKeychain();
#else
    bool storeTokenObfuscated(const QString& token);
    QString retrieveTokenObfuscated() const;
    bool deleteTokenObfuscated();
#endif
};

}} // namespace occt::utils
