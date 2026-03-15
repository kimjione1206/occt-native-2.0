#include "leaderboard.h"

#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QStandardPaths>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace occt {

static std::string default_leaderboard_path()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + "/.occt";
    QDir().mkpath(dir);
    return (dir + "/leaderboard.json").toStdString();
}

Leaderboard::Leaderboard(const std::string& path)
    : path_(path.empty() ? default_leaderboard_path() : path)
{
    load();
}

double Leaderboard::compute_overall_score(double cpu, double gpu, double ram, double storage)
{
    return cpu * 0.4 + gpu * 0.3 + ram * 0.15 + storage * 0.15;
}

void Leaderboard::load()
{
    entries_.clear();

    QFile f(QString::fromStdString(path_));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (!doc.isObject())
        return;

    QJsonArray arr = doc.object()["entries"].toArray();
    for (int i = 0; i < arr.size(); ++i) {
        QJsonObject obj = arr[i].toObject();
        BenchmarkEntry e;
        e.system_name = obj["system_name"].toString().toStdString();
        e.cpu_score = obj["cpu_score"].toDouble();
        e.gpu_score = obj["gpu_score"].toDouble();
        e.ram_score = obj["ram_score"].toDouble();
        e.storage_score = obj["storage_score"].toDouble();
        e.overall_score = obj["overall_score"].toDouble();
        e.timestamp = obj["timestamp"].toString().toStdString();
        entries_.push_back(e);
    }
}

void Leaderboard::save() const
{
    QJsonArray arr;
    for (const auto& e : entries_) {
        QJsonObject obj;
        obj["system_name"] = QString::fromStdString(e.system_name);
        obj["cpu_score"] = e.cpu_score;
        obj["gpu_score"] = e.gpu_score;
        obj["ram_score"] = e.ram_score;
        obj["storage_score"] = e.storage_score;
        obj["overall_score"] = e.overall_score;
        obj["timestamp"] = QString::fromStdString(e.timestamp);
        arr.append(obj);
    }

    QJsonObject root;
    root["entries"] = arr;

    QString finalPath = QString::fromStdString(path_);
    QString tmpPath = finalPath + ".tmp";

    QFile f(tmpPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QJsonDocument doc(root);
    QByteArray data = doc.toJson(QJsonDocument::Indented);
    qint64 written = f.write(data);
    f.close();

    if (written != data.size()) {
        QFile::remove(tmpPath);
        return;
    }

    QFile::remove(finalPath);
    QFile::rename(tmpPath, finalPath);
}

void Leaderboard::submit(const BenchmarkEntry& entry)
{
    BenchmarkEntry e = entry;
    if (e.overall_score == 0.0) {
        e.overall_score = compute_overall_score(e.cpu_score, e.gpu_score,
                                                 e.ram_score, e.storage_score);
    }
    if (e.timestamp.empty()) {
        e.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    }
    entries_.push_back(e);
    save();
}

std::vector<BenchmarkEntry> Leaderboard::get_rankings(const std::string& category) const
{
    std::vector<BenchmarkEntry> sorted = entries_;

    auto get_score = [&category](const BenchmarkEntry& e) -> double {
        if (category == "cpu") return e.cpu_score;
        if (category == "gpu") return e.gpu_score;
        if (category == "ram") return e.ram_score;
        if (category == "storage") return e.storage_score;
        return e.overall_score; // "overall" or default
    };

    std::sort(sorted.begin(), sorted.end(),
              [&get_score](const BenchmarkEntry& a, const BenchmarkEntry& b) {
                  return get_score(a) > get_score(b); // descending
              });

    return sorted;
}

std::string Leaderboard::format_table() const
{
    auto ranked = get_rankings("overall");

    std::ostringstream out;
    out << "Benchmark Leaderboard\n";
    out << std::string(100, '=') << "\n";

    char header[256];
    std::snprintf(header, sizeof(header),
                  "%4s  %-30s %10s %10s %10s %10s %12s  %s\n",
                  "Rank", "System", "CPU", "GPU", "RAM", "Storage", "Overall", "Date");
    out << header;
    out << std::string(100, '-') << "\n";

    int rank = 1;
    for (const auto& e : ranked) {
        char line[256];
        std::snprintf(line, sizeof(line),
                      "%4d  %-30s %10.2f %10.2f %10.2f %10.2f %12.2f  %s\n",
                      rank++,
                      e.system_name.substr(0, 30).c_str(),
                      e.cpu_score, e.gpu_score,
                      e.ram_score, e.storage_score,
                      e.overall_score,
                      e.timestamp.c_str());
        out << line;
    }

    if (ranked.empty()) {
        out << "  (no entries yet)\n";
    }

    out << std::string(100, '=') << "\n";
    return out.str();
}

} // namespace occt
