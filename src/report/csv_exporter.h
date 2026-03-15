#pragma once

#include "png_report.h" // TestResults, SensorDataPoint
#include <QString>

namespace occt {

class CsvExporter {
public:
    /// Export sensor time-series data: timestamp, sensor_name, value, unit
    static bool save_sensors(const QVector<SensorDataPoint>& data, const QString& path);

    /// Export test results: timestamp, test, mode, duration, errors, pass/fail
    static bool save_results(const QVector<TestResultData>& data, const QString& path);
};

} // namespace occt
