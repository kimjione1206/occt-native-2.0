#pragma once

#include "cli_args.h"
#include "report/png_report.h" // TestResults

#include <QCoreApplication>
#include <memory>

namespace occt {

class WheaMonitor;

class CliRunner {
public:
    /// Run the CLI with the given options.
    /// Returns exit code: 0=PASS, 1=FAIL, 2=ERROR.
    int run(const CliOptions& opts);

private:
    /// Output JSON progress line to stdout.
    void emit_json(const QString& type, const QString& key, const QVariant& value);

    /// Run a single stress test.
    int run_test(const CliOptions& opts);

    /// Run a scheduled test sequence.
    int run_schedule(const CliOptions& opts);

    /// Run a preset schedule (quick, standard, extreme, oc_validation).
    int run_preset_schedule(const CliOptions& opts);

    /// Run a certification tier.
    int run_certificate(const CliOptions& opts);

    /// Run monitor-only mode.
    int run_monitor(const CliOptions& opts);

    /// Run combined test (multiple engines simultaneously).
    int run_combined(const CliOptions& opts);

    /// Compare two report JSON files (P4-3).
    int run_compare(const CliOptions& opts);

    /// Upload a certificate to the local store (P4-4).
    int run_cert_upload(const CliOptions& opts);

    /// Verify a certificate hash (P4-4).
    int run_cert_verify(const CliOptions& opts);

    /// List all stored certificates (P4-4).
    int run_cert_list(const CliOptions& opts);

    /// Show or submit to leaderboard (P4-5).
    int run_leaderboard(const CliOptions& opts);

    /// Generate report after test completes.
    bool generate_report(const TestResults& results, const CliOptions& opts);

    /// Collect current system info.
    SystemInfoData collect_system_info();

    /// Start WHEA monitoring if --whea is set. Returns the monitor (may be null).
    std::unique_ptr<WheaMonitor> start_whea_if_enabled(const CliOptions& opts);

    /// Stop WHEA monitor and append error count to results JSON.
    void stop_whea(std::unique_ptr<WheaMonitor>& whea);

    TestResults results_;
    int whea_error_count_ = 0;
};

} // namespace occt
