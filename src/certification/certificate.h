#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>

namespace occt {

enum class CertTier {
    BRONZE,
    SILVER,
    GOLD,
    PLATINUM
};

inline QString cert_tier_name(CertTier tier) {
    switch (tier) {
        case CertTier::BRONZE:   return "Bronze";
        case CertTier::SILVER:   return "Silver";
        case CertTier::GOLD:     return "Gold";
        case CertTier::PLATINUM: return "Platinum";
    }
    return "Unknown";
}

inline QString cert_tier_color(CertTier tier) {
    switch (tier) {
        case CertTier::BRONZE:   return "#CD7F32";
        case CertTier::SILVER:   return "#C0C0C0";
        case CertTier::GOLD:     return "#FFD700";
        case CertTier::PLATINUM: return "#E5E4E2";
    }
    return "#888888";
}

struct TestResult {
    QString engine;
    QString mode;
    bool passed = false;
    int errors = 0;
    double duration_secs = 0.0;
};

struct Certificate {
    CertTier tier = CertTier::BRONZE;
    QString system_info_json;
    QVector<TestResult> results;
    bool passed = false;          // all errors == 0
    QString hash_sha256;          // tamper detection
    QDateTime issued_at;
    QDateTime expires_at;         // typically issued_at + 90 days
};

} // namespace occt
