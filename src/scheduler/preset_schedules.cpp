#include "preset_schedules.h"

namespace occt {

// ─── Test duration constants (in seconds) ────────────────────────────────────

static constexpr int DURATION_2MIN   = 120;
static constexpr int DURATION_3MIN   = 180;
static constexpr int DURATION_10MIN  = 600;
static constexpr int DURATION_20MIN  = 1200;
static constexpr int DURATION_30MIN  = 1800;
static constexpr int DURATION_1HR    = 3600;
static constexpr int DURATION_2HR    = 7200;
static constexpr int DURATION_5HR    = 18000;

QVector<TestStep> preset_quick_check()
{
    return {
        { "cpu", {{"mode", "avx2"}}, DURATION_3MIN, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.50}}, DURATION_2MIN, false }
    };
}

QVector<TestStep> preset_standard()
{
    return {
        { "cpu", {{"mode", "avx2"}}, DURATION_10MIN, false },
        { "gpu", {{"mode", "matrix"}}, DURATION_10MIN, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, DURATION_10MIN, false }
    };
}

QVector<TestStep> preset_extreme()
{
    return {
        { "cpu", {{"mode", "avx2"}}, DURATION_20MIN, true },  // parallel with RAM
        { "ram", {{"mode", "random"}, {"memory_pct", 0.80}}, DURATION_20MIN, false },
        { "gpu", {{"mode", "mixed"}}, DURATION_20MIN, false },
        { "storage", {{"mode", "mixed"}}, DURATION_20MIN, false }
    };
}

QVector<TestStep> preset_oc_validation()
{
    return {
        { "cpu", {{"mode", "linpack"}}, DURATION_1HR, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.80}}, DURATION_1HR, false }
    };
}

// --- Certification tier schedules ---

QVector<TestStep> preset_cert_bronze()
{
    return {
        { "cpu", {{"mode", "sse"}}, DURATION_30MIN, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, DURATION_30MIN, false }
    };
}

QVector<TestStep> preset_cert_silver()
{
    return {
        { "cpu", {{"mode", "avx2"}}, DURATION_1HR, false },
        { "gpu", {{"mode", "matrix"}}, DURATION_1HR, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.70}}, DURATION_1HR, false }
    };
}

QVector<TestStep> preset_cert_gold()
{
    return {
        { "cpu", {{"mode", "linpack"}}, DURATION_2HR, false },
        { "gpu", {{"mode", "vram"}}, DURATION_1HR, false },
        { "ram", {{"mode", "random"}, {"memory_pct", 0.80}}, DURATION_2HR, false },
        { "storage", {{"mode", "fill_verify"}}, DURATION_1HR, false }
    };
}

QVector<TestStep> preset_cert_platinum()
{
    return {
        { "cpu", {{"mode", "avx2"}}, DURATION_5HR, false },
        { "ram", {{"mode", "march"}, {"memory_pct", 0.85}}, DURATION_5HR, false },
        { "gpu", {{"mode", "mixed"}}, DURATION_2HR, false },
        { "storage", {{"mode", "verify_seq"}}, DURATION_2HR, false }
    };
}

} // namespace occt
