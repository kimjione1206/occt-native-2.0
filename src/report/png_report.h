#pragma once

#include <QString>
#include <QVector>

namespace occt {

struct TestResultData {
    QString timestamp;
    QString test_type;
    QString mode;
    QString duration;
    QString score;
    bool passed = false;
    QString details;
    int error_count = 0;
};

struct SystemInfoData {
    QString cpu_name;
    QString gpu_name;
    QString os_name;
    QString ram_total;
    int cpu_cores = 0;
    int cpu_threads = 0;
};

struct SensorDataPoint {
    double timestamp_sec = 0.0;
    QString sensor_name;
    double value = 0.0;
    QString unit;
};

struct TestResults {
    QVector<TestResultData> results;
    SystemInfoData system_info;
    QVector<SensorDataPoint> sensor_series;
    QString overall_verdict;  // "PASS" or "FAIL"
    double total_duration_secs = 0.0;
};

class PngReport {
public:
    /// Render an 800x600 summary image to the given path.
    static bool save(const TestResults& results, const QString& path);
};

} // namespace occt
