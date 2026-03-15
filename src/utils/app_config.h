#pragma once

#include <QByteArray>
#include <QSettings>
#include <QVariantMap>

#include <memory>

namespace occt { namespace utils {

class AppConfig {
public:
    static AppConfig& instance();

    // Window state
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geo);
    QByteArray windowState() const;
    void setWindowState(const QByteArray& state);

    // Last test settings
    QVariantMap lastCpuSettings() const;
    void setLastCpuSettings(const QVariantMap& s);
    QVariantMap lastGpuSettings() const;
    void setLastGpuSettings(const QVariantMap& s);
    QVariantMap lastRamSettings() const;
    void setLastRamSettings(const QVariantMap& s);
    QVariantMap lastStorageSettings() const;
    void setLastStorageSettings(const QVariantMap& s);

    // Update settings
    QString gistToken() const;
    void setGistToken(const QString& token);
    QString lastUpdateCheck() const;
    void setLastUpdateCheck(const QString& timestamp);
    QString updateSkippedVersion() const;
    void setUpdateSkippedVersion(const QString& version);

    // Generic access
    QVariant value(const QString& key, const QVariant& defaultValue = QVariant()) const;
    void setValue(const QString& key, const QVariant& val);

    void sync();

private:
    AppConfig();
    ~AppConfig();
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    QVariantMap readGroup(const QString& group) const;
    void writeGroup(const QString& group, const QVariantMap& map);

    std::unique_ptr<QSettings> settings_;
};

}} // namespace occt::utils
