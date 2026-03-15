#include "csv_exporter.h"

#include <QFile>
#include <QTextStream>

namespace occt {

static bool write_utf8_bom(QFile& file)
{
    // UTF-8 BOM: EF BB BF
    return file.write("\xEF\xBB\xBF", 3) == 3;
}

static QString escape_csv(const QString& s)
{
    if (s.contains(',') || s.contains('"') || s.contains('\n')) {
        QString escaped = s;
        escaped.replace('"', "\"\"");
        return '"' + escaped + '"';
    }
    return s;
}

bool CsvExporter::save_sensors(const QVector<SensorDataPoint>& data, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    if (!write_utf8_bom(file))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "timestamp_sec,sensor_name,value,unit\n";

    for (const auto& dp : data) {
        out << dp.timestamp_sec << ","
            << escape_csv(dp.sensor_name) << ","
            << dp.value << ","
            << escape_csv(dp.unit) << "\n";
    }

    file.close();
    return true;
}

bool CsvExporter::save_results(const QVector<TestResultData>& data, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    if (!write_utf8_bom(file))
        return false;

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);

    out << "timestamp,test_type,mode,duration,errors,result\n";

    for (const auto& r : data) {
        out << escape_csv(r.timestamp) << ","
            << escape_csv(r.test_type) << ","
            << escape_csv(r.mode) << ","
            << escape_csv(r.duration) << ","
            << r.error_count << ","
            << (r.passed ? "PASS" : "FAIL") << "\n";
    }

    file.close();
    return true;
}

} // namespace occt
