#pragma once

#include "png_report.h" // TestResults, etc.
#include <QString>

namespace occt {

class HtmlReport {
public:
    /// Generate a self-contained HTML report (inline CSS/JS, offline viewable).
    static bool save(const TestResults& results, const QString& path);
};

} // namespace occt
