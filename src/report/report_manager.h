#pragma once

#include "png_report.h" // TestResults
#include <QString>

namespace occt {

class ReportManager {
public:
    /// Save an 800x600 PNG summary image.
    bool save_png(const TestResults& results, const QString& path) const;

    /// Save a self-contained HTML report.
    bool save_html(const TestResults& results, const QString& path) const;

    /// Save sensor time-series data as CSV.
    bool save_csv(const TestResults& results, const QString& path) const;

    /// Save full test results as JSON.
    bool save_json(const TestResults& results, const QString& path) const;
};

} // namespace occt
