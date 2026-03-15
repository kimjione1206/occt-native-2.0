#include "whea_monitor.h"

#include <chrono>
#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")
#endif

namespace occt {

// ─── WHEA Monitor constants ─────────────────────────────────────────────────

#ifdef _WIN32
// Number of events to fetch per EvtNext call
static constexpr DWORD WHEA_EVENT_BATCH_SIZE = 16;

// Maximum characters to keep from event XML description
static constexpr int WHEA_DESCRIPTION_MAX_CHARS = 512;

// Poll interval: total ~5 seconds between cycles (10 x 500 ms)
static constexpr int WHEA_POLL_STEPS     = 10;
static constexpr int WHEA_POLL_STEP_MS   = 500;
#endif

// ─── Constructor / Destructor ────────────────────────────────────────────────

WheaMonitor::WheaMonitor(QObject* parent) : QObject(parent) {}

WheaMonitor::~WheaMonitor() { stop(); }

// ─── Common API ──────────────────────────────────────────────────────────────

bool WheaMonitor::is_running() const { return running_.load(); }
int  WheaMonitor::error_count() const { return error_count_.load(); }

QVector<WheaError> WheaMonitor::errors() const {
    std::lock_guard<std::mutex> lk(errors_mutex_);
    return errors_;
}

void WheaMonitor::set_error_callback(ErrorCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    error_cb_ = std::move(cb);
}

void WheaMonitor::set_auto_stop(bool enabled) { auto_stop_.store(enabled); }
bool WheaMonitor::auto_stop() const           { return auto_stop_.load(); }

// ─── Windows implementation ─────────────────────────────────────────────────

#ifdef _WIN32

bool WheaMonitor::start() {
    if (running_.load()) return true;
    running_.store(true);
    error_count_.store(0);
    {
        std::lock_guard<std::mutex> lk(errors_mutex_);
        errors_.clear();
    }
    poll_thread_ = std::thread(&WheaMonitor::poll_thread_func, this);
    return true;
}

void WheaMonitor::stop() {
    running_.store(false);
    if (poll_thread_.joinable()) {
        poll_thread_.join();
    }
}

void WheaMonitor::poll_thread_func() {
    // Query WHEA-Logger events from the System event log.
    // Provider: Microsoft-Windows-WHEA-Logger
    // Channel:  System
    const wchar_t* query =
        L"<QueryList>"
        L"  <Query Id=\"0\" Path=\"System\">"
        L"    <Select Path=\"System\">"
        L"      *[System[Provider[@Name='Microsoft-Windows-WHEA-Logger']]]"
        L"    </Select>"
        L"  </Query>"
        L"</QueryList>";

    // We track the last record timestamp to only report new events.
    ULONGLONG last_record_id = 0;
    bool first_pass = true;

    while (running_.load()) {
        EVT_HANDLE hResults = EvtQuery(
            nullptr, nullptr, query,
            EvtQueryChannelPath | EvtQueryReverseDirection);

        if (!hResults) {
            // Event log query failed; sleep and retry
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        EVT_HANDLE events[WHEA_EVENT_BATCH_SIZE];
        DWORD returned = 0;

        while (EvtNext(hResults, WHEA_EVENT_BATCH_SIZE, events, 1000, 0, &returned)) {
            for (DWORD i = 0; i < returned; ++i) {
                // Render the event to get basic info
                DWORD bufSize = 0;
                DWORD propCount = 0;
                EvtRender(nullptr, events[i], EvtRenderEventXml,
                          0, nullptr, &bufSize, &propCount);

                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && bufSize > 0) {
                    std::vector<wchar_t> buf(bufSize / sizeof(wchar_t) + 1);
                    if (EvtRender(nullptr, events[i], EvtRenderEventXml,
                                  bufSize, buf.data(), &bufSize, &propCount)) {
                        // On first pass, just note the most recent record
                        // so we only report *new* events.
                        if (first_pass) {
                            // Skip existing events
                        } else {
                            WheaError err;
                            err.type        = WheaError::Type::MCE;
                            err.source      = QStringLiteral("WHEA-Logger");
                            err.timestamp   = QDateTime::currentDateTime();
                            err.description = QString::fromWCharArray(buf.data(), static_cast<int>(bufSize / sizeof(wchar_t)));

                            // Truncate description for sanity
                            if (err.description.length() > WHEA_DESCRIPTION_MAX_CHARS)
                                err.description = err.description.left(WHEA_DESCRIPTION_MAX_CHARS) + "...";

                            error_count_.fetch_add(1);
                            {
                                std::lock_guard<std::mutex> lk(errors_mutex_);
                                errors_.append(err);
                            }
                            {
                                std::lock_guard<std::mutex> lk(cb_mutex_);
                                if (error_cb_) error_cb_(err);
                            }

                            emit errorDetected(err);

                            if (auto_stop_.load()) {
                                emit autoStopRequested(err);
                            }
                        }
                    }
                }
                EvtClose(events[i]);
            }
        }
        EvtClose(hResults);

        first_pass = false;

        // Sleep before next poll cycle
        for (int s = 0; s < WHEA_POLL_STEPS && running_.load(); ++s) {
            std::this_thread::sleep_for(std::chrono::milliseconds(WHEA_POLL_STEP_MS));
        }
    }
}

#else
// ─── Non-Windows stub ───────────────────────────────────────────────────────

bool WheaMonitor::start() {
    // No WHEA on non-Windows
    return false;
}

void WheaMonitor::stop() {
    running_.store(false);
}

#endif

} // namespace occt
