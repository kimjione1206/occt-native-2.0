#include "report_manager.h"
#include "png_report.h"
#include "html_report.h"
#include "csv_exporter.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>

namespace occt {

bool ReportManager::save_png(const TestResults& results, const QString& path) const
{
    return PngReport::save(results, path);
}

bool ReportManager::save_html(const TestResults& results, const QString& path) const
{
    return HtmlReport::save(results, path);
}

bool ReportManager::save_csv(const TestResults& results, const QString& path) const
{
    return CsvExporter::save_results(results.results, path);
}

bool ReportManager::save_json(const TestResults& results, const QString& path) const
{
    QJsonObject root;

    // System info
    QJsonObject sys;
    sys["cpu"]        = results.system_info.cpu_name;
    sys["gpu"]        = results.system_info.gpu_name;
    sys["os"]         = results.system_info.os_name;
    sys["ram"]        = results.system_info.ram_total;
    sys["cpu_cores"]  = results.system_info.cpu_cores;
    sys["cpu_threads"]= results.system_info.cpu_threads;
    root["system_info"] = sys;

    // Results
    QJsonArray arr;
    for (const auto& r : results.results) {
        QJsonObject obj;
        obj["timestamp"]  = r.timestamp;
        obj["test_type"]  = r.test_type;
        obj["mode"]       = r.mode;
        obj["duration"]   = r.duration;
        obj["score"]      = r.score;
        obj["passed"]     = r.passed;
        obj["errors"]     = r.error_count;
        obj["details"]    = r.details;
        arr.append(obj);
    }
    root["results"] = arr;

    // Sensor data
    QJsonArray sensors;
    for (const auto& dp : results.sensor_series) {
        QJsonObject s;
        s["timestamp_sec"] = dp.timestamp_sec;
        s["sensor_name"]   = dp.sensor_name;
        s["value"]         = dp.value;
        s["unit"]          = dp.unit;
        sensors.append(s);
    }
    root["sensor_data"] = sensors;

    root["verdict"] = results.overall_verdict;
    root["total_duration_secs"] = results.total_duration_secs;
    root["generated"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

} // namespace occt
