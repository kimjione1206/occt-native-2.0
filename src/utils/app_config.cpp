#include "app_config.h"
#include "portable_paths.h"

namespace occt { namespace utils {

AppConfig& AppConfig::instance()
{
    static AppConfig inst;
    return inst;
}

AppConfig::AppConfig()
{
    QString iniPath = PortablePaths::configDir() + "/settings.ini";
    settings_ = std::make_unique<QSettings>(iniPath, QSettings::IniFormat);
}

AppConfig::~AppConfig()
{
    settings_->sync();
    // settings_ is automatically destroyed by unique_ptr
}

// Window state
QByteArray AppConfig::windowGeometry() const
{
    return settings_->value("Window/geometry").toByteArray();
}

void AppConfig::setWindowGeometry(const QByteArray& geo)
{
    settings_->setValue("Window/geometry", geo);
}

QByteArray AppConfig::windowState() const
{
    return settings_->value("Window/state").toByteArray();
}

void AppConfig::setWindowState(const QByteArray& state)
{
    settings_->setValue("Window/state", state);
}

// Test settings
QVariantMap AppConfig::lastCpuSettings() const   { return readGroup("LastCpuSettings"); }
void AppConfig::setLastCpuSettings(const QVariantMap& s)   { writeGroup("LastCpuSettings", s); }
QVariantMap AppConfig::lastGpuSettings() const   { return readGroup("LastGpuSettings"); }
void AppConfig::setLastGpuSettings(const QVariantMap& s)   { writeGroup("LastGpuSettings", s); }
QVariantMap AppConfig::lastRamSettings() const   { return readGroup("LastRamSettings"); }
void AppConfig::setLastRamSettings(const QVariantMap& s)   { writeGroup("LastRamSettings", s); }
QVariantMap AppConfig::lastStorageSettings() const { return readGroup("LastStorageSettings"); }
void AppConfig::setLastStorageSettings(const QVariantMap& s) { writeGroup("LastStorageSettings", s); }

// Update settings
QString AppConfig::gistToken() const
{
    return settings_->value("Update/gistToken").toString();
}

void AppConfig::setGistToken(const QString& token)
{
    settings_->setValue("Update/gistToken", token);
}

QString AppConfig::lastUpdateCheck() const
{
    return settings_->value("Update/lastCheck").toString();
}

void AppConfig::setLastUpdateCheck(const QString& timestamp)
{
    settings_->setValue("Update/lastCheck", timestamp);
}

QString AppConfig::updateSkippedVersion() const
{
    return settings_->value("Update/skippedVersion").toString();
}

void AppConfig::setUpdateSkippedVersion(const QString& version)
{
    settings_->setValue("Update/skippedVersion", version);
}

// Generic
QVariant AppConfig::value(const QString& key, const QVariant& defaultValue) const
{
    return settings_->value(key, defaultValue);
}

void AppConfig::setValue(const QString& key, const QVariant& val)
{
    settings_->setValue(key, val);
}

void AppConfig::sync()
{
    settings_->sync();
}

// Private helpers
QVariantMap AppConfig::readGroup(const QString& group) const
{
    QVariantMap map;
    settings_->beginGroup(group);
    const QStringList keys = settings_->childKeys();
    for (const QString& key : keys) {
        map[key] = settings_->value(key);
    }
    settings_->endGroup();
    return map;
}

void AppConfig::writeGroup(const QString& group, const QVariantMap& map)
{
    settings_->beginGroup(group);
    settings_->remove(""); // clear existing keys in group
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        settings_->setValue(it.key(), it.value());
    }
    settings_->endGroup();
}

}} // namespace occt::utils
