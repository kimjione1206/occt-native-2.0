#pragma once

#include "test_step.h"
#include <QVector>

namespace occt {

// Quick system check (~5 minutes)
QVector<TestStep> preset_quick_check();

// Standard stability test (~30 minutes)
QVector<TestStep> preset_standard();

// Extreme stress test (~1 hour)
QVector<TestStep> preset_extreme();

// Overclock validation (~2 hours)
QVector<TestStep> preset_oc_validation();

// Certification tier schedules
QVector<TestStep> preset_cert_bronze();    // ~1 hour
QVector<TestStep> preset_cert_silver();    // ~3 hours
QVector<TestStep> preset_cert_gold();      // ~6 hours
QVector<TestStep> preset_cert_platinum();  // ~12 hours

} // namespace occt
