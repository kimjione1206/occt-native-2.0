#pragma once

#include <QString>
#include <QVariantMap>

namespace occt {

struct TestStep {
    QString engine;           // "cpu", "gpu", "ram", "storage", "psu"
    QVariantMap settings;     // engine-specific settings (mode, threads, duration etc.)
    int duration_secs = 60;
    bool parallel_with_next = false; // if true, start simultaneously with next step
};

} // namespace occt
