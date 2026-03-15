#include "lhm_bridge.h"

#ifdef _WIN32
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#endif

namespace occt {

// ─── Windows implementation ─────────────────────────────────────────────────

#ifdef _WIN32

struct LhmBridge::Impl {
    QString helper_path;
    QString log_file_;

    void log(const std::string& msg) {
        if (log_file_.isEmpty()) {
            QString logDir = QCoreApplication::applicationDirPath() + "/logs";
            QDir().mkpath(logDir);
            log_file_ = logDir + "/lhm_bridge.log";
        }
        QFile f(log_file_);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " " << QString::fromStdString(msg) << "\n";
        }
    }

    bool find_helper() {
        // Look for lhm-sensor-reader.exe next to the application
        QStringList search_paths = {
            QCoreApplication::applicationDirPath(),
            QCoreApplication::applicationDirPath() + "/tools",
            QCoreApplication::applicationDirPath() + "/../tools",
            QDir::currentPath()
        };

        for (const auto& dir : search_paths) {
            QString candidate = dir + "/lhm-sensor-reader.exe";
            if (QFileInfo::exists(candidate)) {
                helper_path = candidate;
                return true;
            }
        }
        return false;
    }

    static constexpr int kFirstPollTimeoutMs = 15000;
    static constexpr int kNormalTimeoutMs    = 10000;

    bool poll_helper(std::vector<SensorReading>& out, bool first_poll) {
        if (helper_path.isEmpty()) return false;

        const int timeout_ms = first_poll ? kFirstPollTimeoutMs : kNormalTimeoutMs;

        QProcess proc;
        proc.setProgram(helper_path);
        proc.setArguments({"--json", "--once"});
        proc.start();

        if (!proc.waitForFinished(timeout_ms)) {
            log("[LHM] Helper timed out after " + std::to_string(timeout_ms / 1000) + "s");
            proc.kill();
            return false;
        }

        QByteArray data = proc.readAllStandardOutput();
        QByteArray errData = proc.readAllStandardError();
        int exitCode = proc.exitCode();

        if (exitCode != 0) {
            log("[LHM] Helper exit code: " + std::to_string(exitCode));
            if (!errData.isEmpty()) {
                log("[LHM] Helper stderr: " + errData.toStdString());
            }
            log("[LHM] Helper stdout (first 500 chars): " + data.left(500).toStdString());
            return false;
        }

        if (data.isEmpty()) {
            log("[LHM] Helper returned empty output");
            return false;
        }

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(data, &err);
        if (err.error != QJsonParseError::NoError) {
            log("[LHM] JSON parse error: " + err.errorString().toStdString()
                + " at offset " + std::to_string(err.offset));
            return false;
        }

        if (!doc.isObject()) return false;
        QJsonObject root = doc.object();

        // Expected JSON format:
        // { "hardware": [ { "name": "...", "type": "CPU",
        //     "sensors": [ { "name": "...", "value": 45.0, "unit": "C" }, ... ] }, ... ] }
        QJsonArray hardware = root["hardware"].toArray();
        for (const auto& hw_val : hardware) {
            QJsonObject hw = hw_val.toObject();
            QString type = hw["type"].toString();
            QJsonArray sensors = hw["sensors"].toArray();

            for (const auto& s_val : sensors) {
                QJsonObject s = s_val.toObject();
                SensorReading reading;
                reading.name     = s["name"].toString().toStdString();
                reading.category = type.toStdString();
                reading.value    = s["value"].toDouble();
                reading.unit     = s["unit"].toString().toStdString();

                if (s.contains("min")) reading.min_value = s["min"].toDouble();
                else                   reading.min_value = reading.value;
                if (s.contains("max")) reading.max_value = s["max"].toDouble();
                else                   reading.max_value = reading.value;

                out.push_back(std::move(reading));
            }
        }
        return !hardware.isEmpty();
    }
};

LhmBridge::LhmBridge(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {}

LhmBridge::~LhmBridge() = default;

bool LhmBridge::initialize() {
    available_ = impl_->find_helper();
    if (available_) {
        impl_->log("[LHM] Found helper at: " + impl_->helper_path.toStdString());
    } else {
        impl_->log("[LHM] Helper not found, using WMI fallback");
    }
    return available_;
}

bool LhmBridge::is_available() const { return available_; }

void LhmBridge::poll(std::vector<SensorReading>& out) {
    // If in backoff mode, check whether enough time has elapsed before retrying.
    if (fail_count_ >= 5) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_fail_time_).count();
        if (elapsed < retry_interval_secs_) {
            return; // Still waiting for backoff period
        }
        impl_->log("[LHM] Recovery: retrying after " + std::to_string(retry_interval_secs_)
                    + "s backoff (fail_count: " + std::to_string(fail_count_) + ")");
        available_ = true; // Temporarily re-enable for this attempt
    }

    if (!available_) return;

    if (!impl_->poll_helper(out, first_poll_)) {
        fail_count_++;
        last_fail_time_ = std::chrono::steady_clock::now();
        retry_interval_secs_ = compute_retry_interval();

        if (fail_count_ >= 5) {
            available_ = false;
            impl_->log("[LHM] Poll failed (fail_count: " + std::to_string(fail_count_)
                        + "), next retry in " + std::to_string(retry_interval_secs_) + "s");
        } else {
            impl_->log("[LHM] Poll failed (attempt " + std::to_string(fail_count_) + "/5)");
        }
    } else {
        if (fail_count_ >= 5) {
            impl_->log("[LHM] Recovery successful after " + std::to_string(fail_count_) + " failures");
        }
        // Full reset on success
        fail_count_ = 0;
        retry_interval_secs_ = 0;
        available_ = true;
    }
    first_poll_ = false;
}

int LhmBridge::compute_retry_interval() const {
    if (fail_count_ >= 20) return 120;
    if (fail_count_ >= 10) return 60;
    if (fail_count_ >= 5)  return 30;
    return 0;
}

#else
// ─── Non-Windows stub ───────────────────────────────────────────────────────

LhmBridge::LhmBridge(QObject* parent) : QObject(parent) {}
LhmBridge::~LhmBridge() = default;
bool LhmBridge::initialize() { return false; }
bool LhmBridge::is_available() const { return false; }
void LhmBridge::poll(std::vector<SensorReading>& /*out*/) {}
int LhmBridge::compute_retry_interval() const { return 0; }

#endif

} // namespace occt
