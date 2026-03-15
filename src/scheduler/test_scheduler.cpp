#include "test_scheduler.h"

#include "engines/cpu_engine.h"
#include "engines/gpu_engine.h"
#include "engines/ram_engine.h"
#include "engines/storage_engine.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>

namespace occt {

TestScheduler::TestScheduler(QObject* parent)
    : QObject(parent)
{
    tick_timer_ = new QTimer(this);
    tick_timer_->setInterval(500);
    connect(tick_timer_, &QTimer::timeout, this, &TestScheduler::onTick);
}

TestScheduler::~TestScheduler()
{
    stop();
}

void TestScheduler::load_schedule(const QVector<TestStep>& steps)
{
    if (running_) return;
    steps_ = steps;
    results_.clear();
    total_duration_secs_ = 0;
    for (const auto& s : steps_) {
        total_duration_secs_ += s.duration_secs;
    }
}

void TestScheduler::load_from_json(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return;

    auto root = doc.object();
    stop_on_error_ = root.value("stop_on_error").toBool(false);

    QVector<TestStep> steps;
    auto arr = root.value("steps").toArray();
    for (const auto& val : arr) {
        auto obj = val.toObject();
        TestStep step;
        step.engine = obj.value("engine").toString();
        step.duration_secs = obj.value("duration_secs").toInt(60);
        step.parallel_with_next = obj.value("parallel_with_next").toBool(false);

        auto settings = obj.value("settings").toObject();
        for (auto it = settings.begin(); it != settings.end(); ++it) {
            step.settings[it.key()] = it.value().toVariant();
        }
        steps.append(step);
    }

    load_schedule(steps);
}

void TestScheduler::save_to_json(const QString& path) const
{
    QJsonObject root;
    root["stop_on_error"] = stop_on_error_;

    QJsonArray arr;
    for (const auto& step : steps_) {
        QJsonObject obj;
        obj["engine"] = step.engine;
        obj["duration_secs"] = step.duration_secs;
        obj["parallel_with_next"] = step.parallel_with_next;

        QJsonObject settings;
        for (auto it = step.settings.begin(); it != step.settings.end(); ++it) {
            settings[it.key()] = QJsonValue::fromVariant(it.value());
        }
        obj["settings"] = settings;
        arr.append(obj);
    }
    root["steps"] = arr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void TestScheduler::start()
{
    if (running_ || steps_.isEmpty()) return;

    running_ = true;
    current_step_ = -1;
    total_errors_ = 0;
    results_.clear();
    active_step_indices_.clear();

    schedule_elapsed_.start();
    advanceToNextStep();
    tick_timer_->start();
}

void TestScheduler::stop()
{
    if (!running_) return;

    tick_timer_->stop();
    stopCurrentEngines();
    running_ = false;
    current_step_ = -1;
    active_step_indices_.clear();
}

bool TestScheduler::is_running() const
{
    return running_;
}

int TestScheduler::current_step() const
{
    return current_step_;
}

double TestScheduler::overall_progress() const
{
    if (!running_ || total_duration_secs_ <= 0) return 0.0;

    double elapsed = schedule_elapsed_.elapsed() / 1000.0;
    double pct = (elapsed / total_duration_secs_) * 100.0;
    return qBound(0.0, pct, 100.0);
}

void TestScheduler::startStep(int index)
{
    if (index < 0 || index >= steps_.size()) return;

    const auto& step = steps_[index];
    current_step_ = index;
    active_step_indices_.append(index);

    emit stepStarted(index, step.engine);

    QString engine = step.engine.toLower();

    if (engine == "cpu") {
        if (!cpu_engine_) cpu_engine_ = std::make_unique<CpuEngine>();

        CpuStressMode mode = CpuStressMode::AVX2_FMA;
        QString modeStr = step.settings.value("mode", "avx2").toString().toLower();
        if (modeStr == "auto")         mode = CpuStressMode::AUTO;
        else if (modeStr == "avx512")  mode = CpuStressMode::AVX512_FMA;
        else if (modeStr == "avx")     mode = CpuStressMode::AVX_FLOAT;
        else if (modeStr == "sse")     mode = CpuStressMode::SSE_FLOAT;
        else if (modeStr == "linpack") mode = CpuStressMode::LINPACK;
        else if (modeStr == "prime")   mode = CpuStressMode::PRIME;
        else if (modeStr == "all")     mode = CpuStressMode::ALL;

        int threads = step.settings.value("threads", 0).toInt();
        cpu_engine_->start(mode, threads, step.duration_secs);

    } else if (engine == "gpu") {
        if (!gpu_engine_) {
            gpu_engine_ = std::make_unique<GpuEngine>();
            gpu_engine_->initialize();
        }

        GpuStressMode mode = GpuStressMode::MATRIX_MUL;
        QString modeStr = step.settings.value("mode", "matrix").toString().toLower();
        if (modeStr == "fp64")        mode = GpuStressMode::MATRIX_MUL_FP64;
        else if (modeStr == "fma")    mode = GpuStressMode::FMA_STRESS;
        else if (modeStr == "trig")   mode = GpuStressMode::TRIG_STRESS;
        else if (modeStr == "vram")   mode = GpuStressMode::VRAM_TEST;
        else if (modeStr == "mixed")  mode = GpuStressMode::MIXED;
        else if (modeStr == "vulkan_3d")      mode = GpuStressMode::VULKAN_3D;
        else if (modeStr == "vulkan_adaptive") mode = GpuStressMode::VULKAN_ADAPTIVE;

        gpu_engine_->start(mode, step.duration_secs);

    } else if (engine == "ram") {
        if (!ram_engine_) ram_engine_ = std::make_unique<RamEngine>();

        RamPattern pattern = RamPattern::MARCH_C_MINUS;
        QString modeStr = step.settings.value("mode", "march").toString().toLower();
        if (modeStr == "walking_ones")       pattern = RamPattern::WALKING_ONES;
        else if (modeStr == "walking_zeros") pattern = RamPattern::WALKING_ZEROS;
        else if (modeStr == "checkerboard")  pattern = RamPattern::CHECKERBOARD;
        else if (modeStr == "random")        pattern = RamPattern::RANDOM;
        else if (modeStr == "bandwidth")     pattern = RamPattern::BANDWIDTH;

        double pct = step.settings.value("memory_pct", 0.70).toDouble();
        ram_engine_->start(pattern, pct, 999); // many passes, will be stopped by timer

    } else if (engine == "storage") {
        if (!storage_engine_) storage_engine_ = std::make_unique<StorageEngine>();

        StorageMode mode = StorageMode::MIXED;
        QString modeStr = step.settings.value("mode", "mixed").toString().toLower();
        if (modeStr == "seq_write")        mode = StorageMode::SEQ_WRITE;
        else if (modeStr == "seq_read")    mode = StorageMode::SEQ_READ;
        else if (modeStr == "rand_write")  mode = StorageMode::RAND_WRITE;
        else if (modeStr == "rand_read")   mode = StorageMode::RAND_READ;
        else if (modeStr == "verify_seq")  mode = StorageMode::VERIFY_SEQ;
        else if (modeStr == "verify_rand") mode = StorageMode::VERIFY_RAND;
        else if (modeStr == "fill_verify") mode = StorageMode::FILL_VERIFY;
        else if (modeStr == "butterfly")   mode = StorageMode::BUTTERFLY;

        QString path = step.settings.value("path", "/tmp/occt_storage_test").toString();
        int sizeMb = step.settings.value("file_size_mb", 1024).toInt();
        storage_engine_->start(mode, path.toStdString(), sizeMb);
    }
    // PSU engine: forward declaration only; skipped if not available

    step_elapsed_.start();
}

void TestScheduler::stopCurrentEngines()
{
    for (int idx : active_step_indices_) {
        const auto& step = steps_[idx];
        QString engine = step.engine.toLower();

        int errors = 0;

        if (engine == "cpu" && cpu_engine_ && cpu_engine_->is_running()) {
            auto m = cpu_engine_->get_metrics();
            errors = static_cast<int>(m.error_count);
            cpu_engine_->stop();
        } else if (engine == "gpu" && gpu_engine_ && gpu_engine_->is_running()) {
            auto m = gpu_engine_->get_metrics();
            errors = static_cast<int>(m.vram_errors + m.artifact_count);
            gpu_engine_->stop();
        } else if (engine == "ram" && ram_engine_ && ram_engine_->is_running()) {
            auto m = ram_engine_->get_metrics();
            errors = static_cast<int>(m.errors_found);
            ram_engine_->stop();
        } else if (engine == "storage" && storage_engine_ && storage_engine_->is_running()) {
            auto m = storage_engine_->get_metrics();
            errors = static_cast<int>(m.error_count);
            storage_engine_->stop();
        }

        StepResult result;
        result.index = idx;
        result.engine = step.engine;
        result.errors = errors;
        result.passed = (errors == 0);
        result.duration_secs = step_elapsed_.elapsed() / 1000.0;
        results_.append(result);
        total_errors_ += errors;

        emit stepCompleted(idx, result.passed, errors);
    }
    active_step_indices_.clear();
}

void TestScheduler::advanceToNextStep()
{
    int nextIdx = (current_step_ < 0) ? 0 : current_step_ + 1;

    // If we had parallel steps, advance past all of them
    if (!active_step_indices_.isEmpty()) {
        nextIdx = active_step_indices_.last() + 1;
    }

    if (nextIdx >= steps_.size()) {
        // Schedule complete
        tick_timer_->stop();
        running_ = false;
        bool all_passed = (total_errors_ == 0);
        emit scheduleCompleted(all_passed, total_errors_);
        return;
    }

    active_step_indices_.clear();

    // Start this step and any following parallel steps
    startStep(nextIdx);

    while (nextIdx < steps_.size() && steps_[nextIdx].parallel_with_next) {
        nextIdx++;
        if (nextIdx < steps_.size()) {
            startStep(nextIdx);
        }
    }

    step_elapsed_.start();
}

void TestScheduler::onTick()
{
    if (!running_ || active_step_indices_.isEmpty()) return;

    // Check if the current step(s) duration has elapsed
    double elapsed = step_elapsed_.elapsed() / 1000.0;

    // Use max duration among active parallel steps
    int maxDuration = 0;
    for (int idx : active_step_indices_) {
        maxDuration = qMax(maxDuration, steps_[idx].duration_secs);
    }

    // Check if any engine has stopped on its own (duration-based)
    bool allDone = true;
    for (int idx : active_step_indices_) {
        const auto& step = steps_[idx];
        QString eng = step.engine.toLower();
        bool engineRunning = false;

        if (eng == "cpu" && cpu_engine_)           engineRunning = cpu_engine_->is_running();
        else if (eng == "gpu" && gpu_engine_)      engineRunning = gpu_engine_->is_running();
        else if (eng == "ram" && ram_engine_)       engineRunning = ram_engine_->is_running();
        else if (eng == "storage" && storage_engine_) engineRunning = storage_engine_->is_running();

        if (engineRunning && elapsed < step.duration_secs) {
            allDone = false;
        }
    }

    // Force-stop if time is up
    if (elapsed >= maxDuration || allDone) {
        stopCurrentEngines();

        if (stop_on_error_ && total_errors_ > 0) {
            tick_timer_->stop();
            running_ = false;
            emit scheduleCompleted(false, total_errors_);
            return;
        }

        advanceToNextStep();
    }

    emit progressChanged(overall_progress());
}

} // namespace occt
