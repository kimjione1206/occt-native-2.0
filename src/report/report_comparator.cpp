#include "report_comparator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace occt {

// Keys that represent "higher is better" metrics
static bool is_higher_better(const std::string& name)
{
    // throughput, GFLOPS, bandwidth, IOPS are all "higher is better"
    // temperature, power, error_count are "lower is better"
    if (name.find("temperature") != std::string::npos) return false;
    if (name.find("power") != std::string::npos) return false;
    if (name.find("error") != std::string::npos) return false;
    return true;
}

static QJsonObject load_json(const std::string& path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    auto doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    return doc.isObject() ? doc.object() : QJsonObject{};
}

// Extract key numeric metrics from a report JSON.
// Returns a map of metric_name -> value.
static std::vector<std::pair<std::string, double>> extract_metrics(const QJsonObject& root)
{
    std::vector<std::pair<std::string, double>> metrics;

    // Walk "results" array - each entry may have score and numeric fields
    QJsonArray results = root["results"].toArray();
    for (int i = 0; i < results.size(); ++i) {
        QJsonObject r = results[i].toObject();
        std::string test_type = r["test_type"].toString().toStdString();
        std::string prefix = test_type.empty() ? "" : test_type + " ";

        // Common numeric fields
        auto try_add = [&](const char* key) {
            if (r.contains(QLatin1String(key)) && r[QLatin1String(key)].isDouble()) {
                metrics.push_back({prefix + key, r[QLatin1String(key)].toDouble()});
            }
        };

        try_add("gflops");
        try_add("throughput_mbs");
        try_add("read_mbs");
        try_add("write_mbs");
        try_add("iops");
        try_add("error_count");
        try_add("temperature");
        try_add("power_watts");
        try_add("bandwidth_gbs");
    }

    // Also check top-level fields
    auto try_top = [&](const char* key) {
        if (root.contains(QLatin1String(key)) && root[QLatin1String(key)].isDouble()) {
            metrics.push_back({key, root[QLatin1String(key)].toDouble()});
        }
    };

    try_top("total_duration_secs");

    // Check if there's a nested "summary" or "metrics" block
    if (root.contains("metrics") && root["metrics"].isObject()) {
        QJsonObject m = root["metrics"].toObject();
        for (auto it = m.begin(); it != m.end(); ++it) {
            if (it.value().isDouble()) {
                metrics.push_back({it.key().toStdString(), it.value().toDouble()});
            }
        }
    }

    return metrics;
}

ComparisonResult compare_reports(const std::string& path_a, const std::string& path_b)
{
    ComparisonResult result;
    result.report_a_path = path_a;
    result.report_b_path = path_b;

    QJsonObject json_a = load_json(path_a);
    QJsonObject json_b = load_json(path_b);

    auto metrics_a = extract_metrics(json_a);
    auto metrics_b = extract_metrics(json_b);

    // Build a map from metric name -> value for report B
    std::map<std::string, double> map_b;
    for (const auto& [name, val] : metrics_b) {
        map_b[name] = val;
    }

    int improved = 0, regressed = 0, unchanged = 0;

    for (const auto& [name, val_a] : metrics_a) {
        auto it = map_b.find(name);
        if (it == map_b.end()) continue;

        double val_b = it->second;
        ComparisonEntry entry;
        entry.metric_name = name;
        entry.value_a = val_a;
        entry.value_b = val_b;
        entry.diff_abs = val_b - val_a;

        if (std::abs(val_a) > 1e-9) {
            entry.diff_pct = ((val_b - val_a) / std::abs(val_a)) * 100.0;
        } else {
            entry.diff_pct = 0.0;
        }

        // Determine direction
        bool higher_better = is_higher_better(name);
        double threshold = 1.0; // 1% threshold

        if (std::abs(entry.diff_pct) <= threshold) {
            entry.direction = "unchanged";
            unchanged++;
        } else if ((higher_better && entry.diff_pct > 0) ||
                   (!higher_better && entry.diff_pct < 0)) {
            entry.direction = "improved";
            improved++;
        } else {
            entry.direction = "regressed";
            regressed++;
        }

        result.entries.push_back(entry);

        // Remove from map_b to track B-only metrics
        map_b.erase(it);
    }

    // Metrics only in B (new metrics)
    for (const auto& [name, val_b] : map_b) {
        ComparisonEntry entry;
        entry.metric_name = name;
        entry.value_a = 0.0;
        entry.value_b = val_b;
        entry.diff_abs = val_b;
        entry.diff_pct = 0.0;
        entry.direction = "unchanged";
        unchanged++;
        result.entries.push_back(entry);
    }

    // Build summary
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%d improved, %d regressed, %d unchanged",
                  improved, regressed, unchanged);
    result.summary = buf;

    return result;
}

std::string format_comparison_table(const ComparisonResult& result)
{
    std::ostringstream out;

    out << "Report Comparison\n";
    out << "  A: " << result.report_a_path << "\n";
    out << "  B: " << result.report_b_path << "\n\n";

    // Header
    char header[256];
    std::snprintf(header, sizeof(header), "%-24s %12s %12s %12s %8s  %s\n",
                  "Metric", "Report A", "Report B", "Diff", "Pct%", "Direction");
    out << header;
    out << std::string(80, '-') << "\n";

    for (const auto& e : result.entries) {
        char line[256];
        std::snprintf(line, sizeof(line), "%-24s %12.2f %12.2f %+12.2f %+7.1f%%  %s\n",
                      e.metric_name.c_str(), e.value_a, e.value_b,
                      e.diff_abs, e.diff_pct, e.direction.c_str());
        out << line;
    }

    out << std::string(80, '-') << "\n";
    out << "Summary: " << result.summary << "\n";

    return out.str();
}

} // namespace occt
