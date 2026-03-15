#include "cert_store.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QStandardPaths>

namespace occt {

static std::string default_cert_path()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + "/.occt";
    QDir().mkpath(dir);
    return (dir + "/certs.json").toStdString();
}

CertStore::CertStore(const std::string& db_path)
    : db_path_(db_path.empty() ? default_cert_path() : db_path)
{
}

static QJsonObject load_db(const std::string& path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QJsonObject{};
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    return doc.isObject() ? doc.object() : QJsonObject{};
}

static bool save_db(const std::string& path, const QJsonObject& db)
{
    QString finalPath = QString::fromStdString(path);
    QString tmpPath = finalPath + ".tmp";

    QFile f(tmpPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(db);
    QByteArray data = doc.toJson(QJsonDocument::Indented);
    qint64 written = f.write(data);
    if (written != data.size()) {
        f.close();
        QFile::remove(tmpPath);
        return false;
    }
    f.close();

    // Atomic rename: remove old file, rename tmp to final
    QFile::remove(finalPath);
    if (!QFile::rename(tmpPath, finalPath)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool CertStore::submit(const std::string& cert_json)
{
    // Parse the certificate JSON
    QJsonDocument cert_doc = QJsonDocument::fromJson(QByteArray::fromStdString(cert_json));
    if (!cert_doc.isObject())
        return false;

    QJsonObject cert_obj = cert_doc.object();

    // Extract or compute the hash
    QString hash;
    if (cert_obj.contains("hash_sha256") && cert_obj["hash_sha256"].isString()) {
        hash = cert_obj["hash_sha256"].toString();
    } else {
        // Compute hash from the certificate content
        QByteArray data = QJsonDocument(cert_obj).toJson(QJsonDocument::Compact);
        hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    }

    if (hash.isEmpty())
        return false;

    // Load existing database
    QJsonObject db = load_db(db_path_);
    QJsonObject certs = db["certs"].toObject();

    // Store the certificate keyed by hash
    certs[hash] = cert_obj;
    db["certs"] = certs;

    return save_db(db_path_, db);
}

std::string CertStore::lookup(const std::string& hash) const
{
    QJsonObject db = load_db(db_path_);
    QJsonObject certs = db["certs"].toObject();

    QString key = QString::fromStdString(hash);
    if (!certs.contains(key))
        return {};

    QJsonObject cert = certs[key].toObject();
    return QJsonDocument(cert).toJson(QJsonDocument::Indented).toStdString();
}

bool CertStore::verify(const std::string& hash) const
{
    QJsonObject db = load_db(db_path_);
    QJsonObject certs = db["certs"].toObject();

    QString key = QString::fromStdString(hash);
    if (!certs.contains(key))
        return false;

    QJsonObject cert = certs[key].toObject();

    // Verify: if cert has hash_sha256 field, check it matches the key
    if (cert.contains("hash_sha256")) {
        return cert["hash_sha256"].toString() == key;
    }

    // If no hash field, verify by recomputing
    QByteArray data = QJsonDocument(cert).toJson(QJsonDocument::Compact);
    QString computed = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
    return computed == key;
}

std::vector<std::string> CertStore::list_hashes() const
{
    QJsonObject db = load_db(db_path_);
    QJsonObject certs = db["certs"].toObject();

    std::vector<std::string> result;
    result.reserve(certs.size());
    for (auto it = certs.begin(); it != certs.end(); ++it) {
        result.push_back(it.key().toStdString());
    }
    return result;
}

} // namespace occt
