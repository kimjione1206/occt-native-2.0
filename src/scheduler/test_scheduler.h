#pragma once

#include "test_step.h"

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QElapsedTimer>

#include <memory>
#include <atomic>

namespace occt {

class IEngine;
class CpuEngine;
class GpuEngine;
class RamEngine;
class StorageEngine;

class TestScheduler : public QObject {
    Q_OBJECT

public:
    explicit TestScheduler(QObject* parent = nullptr);
    ~TestScheduler() override;

    void load_schedule(const QVector<TestStep>& steps);
    void load_from_json(const QString& path);
    void save_to_json(const QString& path) const;

    void start();
    void stop();
    bool is_running() const;
    int current_step() const;
    double overall_progress() const;

    const QVector<TestStep>& steps() const { return steps_; }

    void set_stop_on_error(bool v) { stop_on_error_ = v; }
    bool stop_on_error() const { return stop_on_error_; }

    struct StepResult {
        int index = -1;
        QString engine;
        bool passed = false;
        int errors = 0;
        double duration_secs = 0.0;
    };
    const QVector<StepResult>& results() const { return results_; }

signals:
    void stepStarted(int index, const QString& engine);
    void stepCompleted(int index, bool passed, int errors);
    void scheduleCompleted(bool all_passed, int total_errors);
    void progressChanged(double pct);

private slots:
    void onTick();

private:
    void startStep(int index);
    void stopCurrentEngines();
    void advanceToNextStep();

    QVector<TestStep> steps_;
    QVector<StepResult> results_;

    int current_step_ = -1;
    bool running_ = false;
    bool stop_on_error_ = false;
    int total_errors_ = 0;

    // Engine instances (owned, created on demand)
    std::unique_ptr<CpuEngine> cpu_engine_;
    std::unique_ptr<GpuEngine> gpu_engine_;
    std::unique_ptr<RamEngine> ram_engine_;
    std::unique_ptr<StorageEngine> storage_engine_;

    QTimer* tick_timer_ = nullptr;
    QElapsedTimer step_elapsed_;
    QElapsedTimer schedule_elapsed_;
    int total_duration_secs_ = 0; // sum of all step durations

    // Track which engines are active in current parallel group
    QVector<int> active_step_indices_;
};

} // namespace occt
