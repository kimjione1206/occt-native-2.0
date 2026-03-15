#pragma once

#include "certificate.h"

#include <QImage>
#include <QJsonObject>
#include <QString>

namespace occt {

class CertGenerator {
public:
    CertGenerator() = default;

    // Generate HTML certificate (dark theme, professional layout)
    QString generate_html(const Certificate& cert) const;

    // Generate PNG summary image via QPainter
    QImage generate_image(const Certificate& cert, int width = 800, int height = 600) const;

    // Generate machine-readable JSON
    QJsonObject generate_json(const Certificate& cert) const;

    // Compute SHA256 hash for tamper detection
    static QString compute_hash(const QString& system_info, const QJsonObject& results_json);

    // Collect system information as JSON string
    static QString collect_system_info();
};

} // namespace occt
