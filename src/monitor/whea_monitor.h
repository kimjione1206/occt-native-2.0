#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace occt {

/// Represents a single WHEA (Windows Hardware Error Architecture) event.
struct WheaError {
    enum class Type { MCE, PCIe, NMI, Generic };

    Type        type        = Type::Generic;
    QString     source;         // e.g. "Machine Check Exception"
    QDateTime   timestamp;
    QString     description;
};

/// Monitor Windows Event Log for WHEA-Logger events.
///
/// Windows-only.  On other platforms every method is a harmless no-op
/// and error_count() always returns 0.
class WheaMonitor : public QObject {
    Q_OBJECT

public:
    using ErrorCallback = std::function<void(const WheaError&)>;

    explicit WheaMonitor(QObject* parent = nullptr);
    ~WheaMonitor() override;

    /// Start listening for WHEA events.
    bool start();

    /// Stop listening.
    void stop();

    /// Whether the monitor is currently running.
    bool is_running() const;

    /// Total WHEA errors detected since start().
    int error_count() const;

    /// Get all captured errors (thread-safe copy).
    QVector<WheaError> errors() const;

    /// Register callback invoked on each new error.
    void set_error_callback(ErrorCallback cb);

    /// When true, emit autoStopRequested() on any WHEA error.
    void set_auto_stop(bool enabled);
    bool auto_stop() const;

signals:
    /// Emitted when a WHEA error is detected and auto_stop is enabled.
    void autoStopRequested(const WheaError& error);

    /// Emitted for every WHEA error detected.
    void errorDetected(const WheaError& error);

private:
#ifdef _WIN32
    void poll_thread_func();
    std::thread poll_thread_;
#endif

    std::atomic<bool> running_{false};
    std::atomic<bool> auto_stop_{false};
    std::atomic<int>  error_count_{0};

    mutable std::mutex errors_mutex_;
    QVector<WheaError> errors_;

    std::mutex cb_mutex_;
    ErrorCallback error_cb_;
};

} // namespace occt
