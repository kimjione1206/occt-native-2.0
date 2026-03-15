#pragma once

#include <atomic>
#include <string>

namespace occt {

/// Abstract base interface for all stress-test engines.
/// Allows SafetyGuardian to uniformly stop any registered engine.
class IEngine {
public:
    virtual ~IEngine() = default;

    /// Immediately stop the stress test.
    virtual void stop() = 0;

    /// Returns true if the engine is currently running a test.
    virtual bool is_running() const = 0;

    /// Human-readable engine name (e.g. "RAM", "Storage", "CPU").
    virtual std::string name() const = 0;

    /// When enabled, the engine will automatically stop when an error is detected.
    void set_stop_on_error(bool enable) { stop_on_error_.store(enable); }
    bool stop_on_error() const { return stop_on_error_.load(); }

protected:
    std::atomic<bool> stop_on_error_{false};
};

} // namespace occt
