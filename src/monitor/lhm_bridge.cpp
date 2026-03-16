#include "lhm_bridge.h"

#ifdef _WIN32
#include <windows.h>
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

    // Win32 프로세스 핸들
    HANDLE hProcess = INVALID_HANDLE_VALUE;
    HANDLE hStdoutRead = INVALID_HANDLE_VALUE;
    HANDLE hStdoutWrite = INVALID_HANDLE_VALUE;
    bool process_running = false;

    // 라인 버퍼 (파이프에서 부분 읽기 처리)
    std::string line_buffer;

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
        // 폴더 배포 경로 (tools/lhm/) 우선, 단일 파일 경로도 호환성 유지
        QStringList search_paths = {
            QCoreApplication::applicationDirPath() + "/tools/lhm",
            QCoreApplication::applicationDirPath() + "/../tools/lhm",
            QCoreApplication::applicationDirPath(),
            QCoreApplication::applicationDirPath() + "/tools",
            QCoreApplication::applicationDirPath() + "/../tools",
            QDir::currentPath() + "/tools/lhm",
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

    bool start_helper() {
        stop_helper();  // 기존 프로세스 정리

        // stdout 파이프 생성
        SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
            log("[LHM] CreatePipe failed: " + std::to_string(GetLastError()));
            return false;
        }
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);  // 읽기 핸들은 상속 안 함

        // 프로세스 생성
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStdoutWrite;
        si.hStdInput = INVALID_HANDLE_VALUE;

        PROCESS_INFORMATION pi = {};
        std::wstring cmdLine = helper_path.toStdWString() + L" --loop 500";

        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            log("[LHM] CreateProcess failed: " + std::to_string(GetLastError()));
            CloseHandle(hStdoutRead);  hStdoutRead = INVALID_HANDLE_VALUE;
            CloseHandle(hStdoutWrite); hStdoutWrite = INVALID_HANDLE_VALUE;
            return false;
        }

        hProcess = pi.hProcess;
        CloseHandle(pi.hThread);  // 스레드 핸들 불필요

        // 쓰기 핸들 닫기 (부모에서는 읽기만)
        CloseHandle(hStdoutWrite);
        hStdoutWrite = INVALID_HANDLE_VALUE;

        process_running = true;
        log("[LHM] Started persistent helper (PID: " + std::to_string(pi.dwProcessId) + ")");

        // 첫 데이터 대기 (최대 15초, 1초 간격 폴링)
        for (int i = 0; i < 15; ++i) {
            DWORD avail = 0;
            if (PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
                log("[LHM] First data available after " + std::to_string(i) + "s");
                return true;
            }
            // 프로세스가 죽었는지 확인
            if (WaitForSingleObject(hProcess, 0) != WAIT_TIMEOUT) {
                log("[LHM] Helper exited during initialization");
                stop_helper();
                return false;
            }
            Sleep(1000);
        }

        log("[LHM] No data within 15s, continuing anyway");
        return true;
    }

    void stop_helper() {
        if (hProcess != INVALID_HANDLE_VALUE) {
            TerminateProcess(hProcess, 0);
            WaitForSingleObject(hProcess, 3000);
            CloseHandle(hProcess);
            hProcess = INVALID_HANDLE_VALUE;
        }
        if (hStdoutRead != INVALID_HANDLE_VALUE) {
            CloseHandle(hStdoutRead);
            hStdoutRead = INVALID_HANDLE_VALUE;
        }
        if (hStdoutWrite != INVALID_HANDLE_VALUE) {
            CloseHandle(hStdoutWrite);
            hStdoutWrite = INVALID_HANDLE_VALUE;
        }
        process_running = false;
        line_buffer.clear();
    }

    // Returns: 1 = success, 0 = no data yet (not an error), -1 = process dead
    int read_latest(std::vector<SensorReading>& out) {
        // 프로세스 상태 확인
        if (hProcess == INVALID_HANDLE_VALUE) return -1;
        if (WaitForSingleObject(hProcess, 0) != WAIT_TIMEOUT) {
            DWORD exitCode = 0;
            GetExitCodeProcess(hProcess, &exitCode);
            log("[LHM] Helper process exited (exit code: " + std::to_string(exitCode) + ")");
            process_running = false;
            return -1;  // 프로세스 종료됨
        }

        // 파이프에서 가용 데이터 확인
        DWORD avail = 0;
        if (!PeekNamedPipe(hStdoutRead, nullptr, 0, nullptr, &avail, nullptr)) {
            return -1;
        }

        if (avail == 0) return 0;  // 아직 데이터 없음

        // 읽기
        char buf[4096];
        DWORD bytesRead = 0;
        DWORD toRead = (avail < sizeof(buf)) ? avail : static_cast<DWORD>(sizeof(buf));
        if (!ReadFile(hStdoutRead, buf, toRead, &bytesRead, nullptr) || bytesRead == 0) {
            return 0;
        }

        line_buffer.append(buf, bytesRead);

        // 마지막 완전한 줄 찾기
        std::string lastLine;
        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos) {
            lastLine = line_buffer.substr(0, pos);
            line_buffer.erase(0, pos + 1);
        }

        if (lastLine.empty()) return 0;  // 완전한 줄 아직 없음

        // '\r' 제거 (Windows 줄바꿈)
        if (!lastLine.empty() && lastLine.back() == '\r') {
            lastLine.pop_back();
        }

        // JSON 파싱
        QByteArray jsonData = QByteArray::fromRawData(lastLine.data(),
                                                       static_cast<int>(lastLine.size()));
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(jsonData, &err);
        if (err.error != QJsonParseError::NoError) {
            log("[LHM] JSON parse error: " + err.errorString().toStdString()
                + " at offset " + std::to_string(err.offset));
            return 0;
        }

        if (!doc.isObject()) return 0;
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
        return hardware.isEmpty() ? 0 : 1;
    }
};

LhmBridge::LhmBridge(QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>()) {}

LhmBridge::~LhmBridge() {
    if (impl_) {
        impl_->stop_helper();
    }
}

bool LhmBridge::initialize() {
    // Prevent double initialization
    if (available_ || impl_->process_running) {
        return available_;
    }

    if (!impl_->find_helper()) {
        impl_->log("[LHM] Helper not found, using WMI fallback");
        available_ = false;
        return false;
    }

    impl_->log("[LHM] Found helper at: " + impl_->helper_path.toStdString());

    // Start the persistent helper process
    if (impl_->start_helper()) {
        available_ = true;
    } else {
        impl_->log("[LHM] Failed to start persistent helper, using WMI fallback");
        available_ = false;
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

        // Try to restart the helper process
        if (impl_->start_helper()) {
            available_ = true;
        } else {
            // Restart failed, update backoff and wait longer
            fail_count_++;
            last_fail_time_ = std::chrono::steady_clock::now();
            retry_interval_secs_ = compute_retry_interval();
            impl_->log("[LHM] Restart failed, next retry in " + std::to_string(retry_interval_secs_) + "s");
            return;
        }
    }

    if (!available_) return;

    // If helper process died, try to restart it
    if (!impl_->process_running) {
        impl_->log("[LHM] Helper crashed, restarting...");
        if (!impl_->start_helper()) {
            fail_count_++;
            last_fail_time_ = std::chrono::steady_clock::now();
            retry_interval_secs_ = compute_retry_interval();
            if (fail_count_ >= 5) {
                available_ = false;
                impl_->log("[LHM] Restart failed (fail_count: " + std::to_string(fail_count_)
                            + "), next retry in " + std::to_string(retry_interval_secs_) + "s");
            }
            return;
        }
    }

    // Read latest data from persistent process stdout
    int result = impl_->read_latest(out);
    if (result == 1) {
        // Success - data received
        if (fail_count_ > 0) {
            impl_->log("[LHM] Recovery successful after " + std::to_string(fail_count_) + " failures");
        }
        fail_count_ = 0;
        retry_interval_secs_ = 0;
        available_ = true;
    } else if (result == 0) {
        // No data yet - LHM still initializing, don't count as failure
        // Just wait for next poll cycle
    } else {
        // Process dead (-1)
        fail_count_++;
        last_fail_time_ = std::chrono::steady_clock::now();
        retry_interval_secs_ = compute_retry_interval();

        if (fail_count_ >= 5) {
            available_ = false;
            impl_->log("[LHM] Process dead (fail_count: " + std::to_string(fail_count_)
                        + "), next retry in " + std::to_string(retry_interval_secs_) + "s");
        } else {
            impl_->log("[LHM] Process dead (attempt " + std::to_string(fail_count_) + "/5)");
        }
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
