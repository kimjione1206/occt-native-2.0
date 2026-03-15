#include "cli_runner.h"
#include "report/report_manager.h"
#include "engines/cpu_engine.h"
#include "engines/ram_engine.h"
#include "engines/storage_engine.h"
#include "engines/gpu_engine.h"
#include "engines/psu_engine.h"
#include "engines/benchmark/cache_benchmark.h"
#include "engines/benchmark/memory_benchmark.h"
#include "monitor/sensor_manager.h"
#include "monitor/whea_monitor.h"
#include "utils/cpuid.h"
#include "report/csv_exporter.h"
#include "scheduler/test_scheduler.h"
#include "certification/certificate.h"
#include "certification/cert_generator.h"
#include "scheduler/preset_schedules.h"
#include "report/report_comparator.h"
#include "api/cert_store.h"
#include "benchmark/leaderboard.h"
#include "safety/guardian.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTimer>
#include <QDateTime>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstdio>
#include <chrono>

namespace occt {

void CliRunner::emit_json(const QString& type, const QString& key, const QVariant& value)
{
    QJsonObject obj;
    obj["type"] = type;
    obj[key] = QJsonValue::fromVariant(value);
    obj["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(obj);
    std::fprintf(stdout, "%s\n", doc.toJson(QJsonDocument::Compact).constData());
    std::fflush(stdout);
}

SystemInfoData CliRunner::collect_system_info()
{
    SystemInfoData info;
    auto cpuInfo = occt::utils::detect_cpu();
    info.cpu_name = QString::fromStdString(cpuInfo.brand);
    info.cpu_cores = cpuInfo.physical_cores;
    info.cpu_threads = cpuInfo.logical_cores;

#if defined(Q_OS_MACOS)
    info.os_name = "macOS";
#elif defined(Q_OS_WIN)
    info.os_name = "Windows";
#elif defined(Q_OS_LINUX)
    info.os_name = "Linux";
#else
    info.os_name = "Unknown";
#endif

    info.gpu_name = "N/A";
    info.ram_total = "N/A";

    return info;
}

int CliRunner::run(const CliOptions& opts)
{
    if (opts.show_help) {
        print_usage();
        return 0;
    }

    if (opts.show_version) {
        std::fprintf(stdout, "{\"type\":\"result\",\"version\":\"1.0.0\"}\n");
        return 0;
    }

    if (opts.monitor_only) {
        return run_monitor(opts);
    }

    // Report comparison dispatch (P4-3)
    if (!opts.compare_a.isEmpty() && !opts.compare_b.isEmpty()) {
        return run_compare(opts);
    }

    // Certificate store dispatch (P4-4)
    if (!opts.upload_cert.isEmpty()) {
        return run_cert_upload(opts);
    }
    if (!opts.verify_hash.isEmpty()) {
        return run_cert_verify(opts);
    }
    if (opts.list_certs) {
        return run_cert_list(opts);
    }

    // Leaderboard dispatch (P4-5)
    if (!opts.leaderboard_cmd.isEmpty()) {
        return run_leaderboard(opts);
    }

    if (opts.test.isEmpty()) {
        emit_json("error", "message", "No test specified. Use --test <type> or --help.");
        return 2;
    }

    // Schedule dispatch (Fix 3-1) and preset schedule (Gap 9)
    if (opts.test == "schedule" && !opts.schedule_file.isEmpty()) {
        return run_schedule(opts);
    }
    if (opts.test == "schedule" && !opts.preset.isEmpty()) {
        return run_preset_schedule(opts);
    }

    // Certificate dispatch (Fix 3-2)
    if (opts.test == "certificate" && !opts.cert_tier.isEmpty()) {
        return run_certificate(opts);
    }

    // Combined test dispatch (P3-5)
    if (opts.test == "combined") {
        return run_combined(opts);
    }

    return run_test(opts);
}

int CliRunner::run_test(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    int duration = opts.duration > 0 ? opts.duration : 60;
    bool test_passed = true;

    emit_json("progress", "message", QString("Starting %1 test (duration: %2s)").arg(opts.test).arg(duration));

    // WHEA monitoring (P3-4)
    auto whea = start_whea_if_enabled(opts);

    // Safety guardian (Gap 8): set up if any limits differ from defaults
    SensorManager safety_sensors;
    std::unique_ptr<SafetyGuardian> guardian;
    bool custom_safety = (opts.cpu_temp_limit != 95 || opts.gpu_temp_limit != 90 || opts.power_limit != 300);
    if (custom_safety) {
        if (safety_sensors.initialize()) {
            guardian = std::make_unique<SafetyGuardian>(&safety_sensors);
            SafetyLimits limits;
            limits.cpu_temp_max = static_cast<double>(opts.cpu_temp_limit);
            limits.gpu_temp_max = static_cast<double>(opts.gpu_temp_limit);
            limits.cpu_power_max = static_cast<double>(opts.power_limit);
            guardian->set_limits(limits);
            guardian->set_emergency_callback([this](const std::string& reason) {
                emit_json("warning", "message",
                    QString("Safety guardian triggered: %1").arg(QString::fromStdString(reason)));
            });
            guardian->start();
            emit_json("progress", "message",
                QString("Safety guardian active (CPU:%1C GPU:%2C Power:%3W)")
                    .arg(opts.cpu_temp_limit).arg(opts.gpu_temp_limit).arg(opts.power_limit));
        } else {
            emit_json("warning", "message", "Failed to initialize sensors for safety guardian");
        }
    }

    auto start = std::chrono::steady_clock::now();

    if (opts.test == "cpu") {
        CpuEngine engine;
        if (guardian) guardian->register_engine(&engine);

        CpuStressMode mode = CpuStressMode::AVX2_FMA;
        if (opts.mode == "auto") mode = CpuStressMode::AUTO;
        else if (opts.mode == "avx") mode = CpuStressMode::AVX_FLOAT;
        else if (opts.mode == "linpack") mode = CpuStressMode::LINPACK;
        else if (opts.mode == "prime") mode = CpuStressMode::PRIME;
        else if (opts.mode == "sse") mode = CpuStressMode::SSE_FLOAT;
        else if (opts.mode == "avx512") mode = CpuStressMode::AVX512_FMA;
        else if (opts.mode == "cache") mode = CpuStressMode::CACHE_ONLY;
        else if (opts.mode == "large_data") mode = CpuStressMode::LARGE_DATA_SET;
        else if (opts.mode == "all") mode = CpuStressMode::ALL;

        int threads = opts.threads > 0 ? opts.threads : 0;

        // Parse load pattern (Fix 1-3)
        LoadPattern lp = LoadPattern::STEADY;
        if (opts.load_pattern == "variable") lp = LoadPattern::VARIABLE;
        else if (opts.load_pattern == "core_cycling") lp = LoadPattern::CORE_CYCLING;

        // Parse intensity mode (Gap 5)
        CpuIntensityMode intensity = CpuIntensityMode::EXTREME;
        if (opts.intensity == "normal") intensity = CpuIntensityMode::NORMAL;
        else if (opts.intensity == "extreme") intensity = CpuIntensityMode::EXTREME;

        CpuMetrics last_metrics{};
        engine.set_metrics_callback([this, &last_metrics](const CpuMetrics& m) {
            last_metrics = m;
            QJsonObject metric;
            metric["gflops"] = m.gflops;
            // Output null instead of 0 when temperature is unavailable
            if (m.temperature > 0.0)
                metric["temperature"] = m.temperature;
            else
                metric["temperature"] = QJsonValue(QJsonValue::Null);
            metric["power_watts"] = m.power_watts;
            if (m.power_estimated)
                metric["power_estimated"] = true;
            metric["threads"] = m.active_threads;
            metric["elapsed"] = m.elapsed_secs;
            metric["error_count"] = m.error_count;
            emit_json("metric", "cpu", QJsonDocument(metric).toVariant());
        });

        // Pass SensorManager for temperature/power readings if available
        if (custom_safety && guardian) {
            engine.set_sensor_manager(&safety_sensors);
        }

        engine.start(mode, threads, duration, lp, intensity);

        // Wait for completion
        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "CPU";
        result.mode = opts.mode.isEmpty() ? "AVX2" : opts.mode.toUpper();
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 GFLOPS").arg(metrics.peak_gflops, 0, 'f', 2);
        result.passed = (last_metrics.error_count == 0);
        result.error_count = last_metrics.error_count;
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "ram") {
        RamEngine engine;
        if (guardian) guardian->register_engine(&engine);

        RamPattern pattern = RamPattern::MARCH_C_MINUS;
        if (opts.mode == "walking") pattern = RamPattern::WALKING_ONES;
        else if (opts.mode == "walking_zeros") pattern = RamPattern::WALKING_ZEROS;
        else if (opts.mode == "random") pattern = RamPattern::RANDOM;
        else if (opts.mode == "checkerboard") pattern = RamPattern::CHECKERBOARD;
        else if (opts.mode == "bandwidth") pattern = RamPattern::BANDWIDTH;

        engine.set_metrics_callback([this](const RamMetrics& m) {
            QJsonObject metric;
            metric["tested_mb"] = m.memory_used_mb;
            metric["errors"] = static_cast<int>(m.errors_found);
            metric["elapsed"] = m.elapsed_secs;
            metric["progress"] = m.progress_pct;
            metric["pages_locked"] = m.pages_locked;
            emit_json("metric", "ram", QJsonDocument(metric).toVariant());
            if (!m.pages_locked && m.progress_pct < 1.0) {
                emit_json("warning", "ram",
                    QString("Memory lock failure - test result reliability may be reduced"));
            }
        });

        double mem_pct = opts.memory_percent / 100.0;
        engine.start(pattern, mem_pct, opts.passes);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "RAM";
        result.mode = opts.mode.isEmpty() ? "March C-" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 MB tested").arg(metrics.memory_used_mb, 0, 'f', 0);
        result.passed = (metrics.errors_found == 0);
        result.error_count = static_cast<int>(metrics.errors_found);
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "storage") {
        StorageEngine engine;
        if (guardian) guardian->register_engine(&engine);

        StorageMode smode = StorageMode::SEQ_WRITE;
        if (opts.mode == "read") smode = StorageMode::SEQ_READ;
        else if (opts.mode == "random_write") smode = StorageMode::RAND_WRITE;
        else if (opts.mode == "random_read") smode = StorageMode::RAND_READ;
        else if (opts.mode == "mixed") smode = StorageMode::MIXED;
        else if (opts.mode == "verify_seq") smode = StorageMode::VERIFY_SEQ;
        else if (opts.mode == "verify_rand") smode = StorageMode::VERIFY_RAND;
        else if (opts.mode == "fill_verify") smode = StorageMode::FILL_VERIFY;
        else if (opts.mode == "butterfly") smode = StorageMode::BUTTERFLY;

        // Log block size and direct I/O settings (Gap 6 & Gap 7)
        if (opts.block_size_kb != 4) {
            emit_json("progress", "message",
                QString("Storage block size: %1 KB").arg(opts.block_size_kb));
        }
        if (opts.direct_io) {
            emit_json("progress", "message", "Storage direct I/O: enabled");
        }

        engine.set_metrics_callback([this](const StorageMetrics& m) {
            QJsonObject metric;
            metric["read_mbps"] = m.read_mbs;
            metric["write_mbps"] = m.write_mbs;
            metric["iops"] = m.iops;
            metric["elapsed"] = m.elapsed_secs;
            if (m.verify_errors > 0) {
                metric["verify_errors"] = static_cast<qint64>(m.verify_errors);
                metric["crc_errors"] = static_cast<qint64>(m.crc_errors);
                metric["magic_errors"] = static_cast<qint64>(m.magic_errors);
                metric["index_errors"] = static_cast<qint64>(m.index_errors);
                metric["pattern_errors"] = static_cast<qint64>(m.pattern_errors);
                metric["io_errors"] = static_cast<qint64>(m.io_errors);
                metric["first_error_block"] = static_cast<qint64>(m.first_error_block);
                metric["last_error_block"] = static_cast<qint64>(m.last_error_block);
            }
            emit_json("metric", "storage", QJsonDocument(metric).toVariant());
        });

        std::string path = opts.storage_path.isEmpty() ? QDir::tempPath().toStdString() : opts.storage_path.toStdString();
        engine.set_block_size_kb(opts.block_size_kb);
        engine.set_direct_io(opts.direct_io);
        if (!engine.start(smode, path, opts.file_size_mb, opts.queue_depth, static_cast<uint64_t>(duration))) {
            emit_json("error", "storage", QVariant(QString::fromStdString(engine.last_error())));
            return 2;
        }

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "Storage";
        result.mode = opts.mode.isEmpty() ? "Sequential Write" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("R:%1 W:%2 MB/s").arg(metrics.read_mbs, 0, 'f', 1).arg(metrics.write_mbs, 0, 'f', 1);
        result.passed = (metrics.verify_errors == 0);
        result.error_count = static_cast<int>(metrics.verify_errors);
        results_.results.append(result);

    } else if (opts.test == "gpu") {
        GpuEngine engine;
        if (guardian) guardian->register_engine(&engine);
        if (!engine.initialize()) {
            emit_json("error", "message", "Failed to initialize GPU engine (no GPU or OpenCL/Vulkan unavailable)");
            return 2;
        }

        GpuStressMode gmode = GpuStressMode::MATRIX_MUL;
        if (opts.mode == "matrix_fp64" || opts.mode == "fp64") gmode = GpuStressMode::MATRIX_MUL_FP64;
        else if (opts.mode == "fma") gmode = GpuStressMode::FMA_STRESS;
        else if (opts.mode == "trig") gmode = GpuStressMode::TRIG_STRESS;
        else if (opts.mode == "vram") gmode = GpuStressMode::VRAM_TEST;
        else if (opts.mode == "mixed") gmode = GpuStressMode::MIXED;
        else if (opts.mode == "vulkan_3d") gmode = GpuStressMode::VULKAN_3D;
        else if (opts.mode == "vulkan_adaptive") gmode = GpuStressMode::VULKAN_ADAPTIVE;

        if (opts.gpu_index >= 0) engine.select_gpu(opts.gpu_index);

        // Apply backend preference (Gap 3): if --backend is specified, prefer that backend
        // by selecting an appropriate mode if the current mode is generic.
        if (!opts.backend.isEmpty()) {
            if (opts.backend == "vulkan") {
                // If mode is a generic OpenCL mode but user wants Vulkan, switch to vulkan_3d
                if (gmode != GpuStressMode::VULKAN_3D && gmode != GpuStressMode::VULKAN_ADAPTIVE) {
                    if (engine.is_vulkan_available()) {
                        emit_json("progress", "message", "Backend preference: vulkan - switching to vulkan_3d mode");
                        gmode = GpuStressMode::VULKAN_3D;
                    } else {
                        emit_json("warning", "message", "Vulkan backend requested but not available");
                    }
                }
            } else if (opts.backend == "opencl") {
                // If mode is a Vulkan mode but user wants OpenCL, switch to matrix_mul
                if (gmode == GpuStressMode::VULKAN_3D || gmode == GpuStressMode::VULKAN_ADAPTIVE) {
                    if (engine.is_opencl_available()) {
                        emit_json("progress", "message", "Backend preference: opencl - switching to matrix_mul mode");
                        gmode = GpuStressMode::MATRIX_MUL;
                    } else {
                        emit_json("warning", "message", "OpenCL backend requested but not available");
                    }
                }
            }
        }

        if (gmode == GpuStressMode::VULKAN_3D || gmode == GpuStressMode::VULKAN_ADAPTIVE)
            engine.set_shader_complexity(opts.shader_complexity);
        if (gmode == GpuStressMode::VULKAN_ADAPTIVE) {
            AdaptiveMode am = AdaptiveMode::VARIABLE;
            if (opts.adaptive_mode == "switch") am = AdaptiveMode::SWITCH;
            else if (opts.adaptive_mode == "coil_whine") am = AdaptiveMode::COIL_WHINE;
            engine.set_adaptive_mode(am);
            if (am == AdaptiveMode::COIL_WHINE) {
                engine.set_coil_whine_freq(opts.coil_freq);
            }
        }

        GpuMetrics last_gpu_metrics{};
        engine.set_metrics_callback([this, &last_gpu_metrics](const GpuMetrics& m) {
            last_gpu_metrics = m;
            QJsonObject metric;
            metric["gflops"] = m.gflops;
            if (m.temperature > 0.0)
                metric["temperature"] = m.temperature;
            else
                metric["temperature"] = QJsonValue(QJsonValue::Null);
            metric["power_watts"] = m.power_watts;
            metric["gpu_usage_pct"] = m.gpu_usage_pct;
            metric["vram_usage_pct"] = m.vram_usage_pct;
            metric["vram_errors"] = static_cast<qint64>(m.vram_errors);
            metric["elapsed"] = m.elapsed_secs;
            metric["fps"] = m.fps;
            metric["artifact_count"] = static_cast<qint64>(m.artifact_count);
            emit_json("metric", "gpu", QJsonDocument(metric).toVariant());
        });

        if (!engine.start(gmode, duration)) {
            emit_json("error", "gpu", QVariant(QString::fromStdString(engine.last_error())));
            return 2;
        }

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "GPU";
        result.mode = opts.mode.isEmpty() ? "Matrix Mul" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 GFLOPS").arg(metrics.gflops, 0, 'f', 2);
        result.passed = (last_gpu_metrics.vram_errors == 0 && last_gpu_metrics.artifact_count == 0);
        result.error_count = static_cast<int>(last_gpu_metrics.vram_errors + last_gpu_metrics.artifact_count);
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "psu") {
        PsuEngine engine;
        if (guardian) guardian->register_engine(&engine);

        PsuLoadPattern psu_pattern = PsuLoadPattern::STEADY;
        if (opts.mode == "spike") psu_pattern = PsuLoadPattern::SPIKE;
        else if (opts.mode == "ramp") psu_pattern = PsuLoadPattern::RAMP;

        if (opts.use_all_gpus) engine.set_use_all_gpus(true);

        // Pass SensorManager so the internal CpuEngine can read power/temperature
        engine.set_sensor_manager(&safety_sensors);

        PsuMetrics last_psu_metrics{};
        engine.set_metrics_callback([this, &last_psu_metrics](const PsuMetrics& m) {
            last_psu_metrics = m;
            QJsonObject metric;
            metric["total_power_watts"] = m.total_power_watts;
            metric["cpu_power_watts"] = m.cpu_power_watts;
            metric["gpu_power_watts"] = m.gpu_power_watts;
            metric["cpu_running"] = m.cpu_running;
            metric["gpu_running"] = m.gpu_running;
            metric["elapsed"] = m.elapsed_secs;
            metric["errors_cpu"] = m.errors_cpu;
            metric["errors_gpu"] = m.errors_gpu;
            metric["power_stability_pct"] = m.power_stability_pct;
            metric["max_power_drop_watts"] = m.max_power_drop_watts;
            metric["power_drop_events"] = m.power_drop_events;
            metric["power_correlated_errors"] = m.power_correlated_errors;
            switch (m.health) {
                case PsuHealthStatus::HEALTHY:  metric["health_status"] = QStringLiteral("HEALTHY"); break;
                case PsuHealthStatus::MARGINAL: metric["health_status"] = QStringLiteral("MARGINAL"); break;
                case PsuHealthStatus::UNSTABLE: metric["health_status"] = QStringLiteral("UNSTABLE"); break;
                case PsuHealthStatus::FAILED:   metric["health_status"] = QStringLiteral("FAILED"); break;
            }
            emit_json("metric", "psu", QJsonDocument(metric).toVariant());
        });

        engine.start(psu_pattern, duration);

        while (engine.is_running()) {
            QThread::msleep(500);
            QCoreApplication::processEvents();
        }

        auto metrics = engine.get_metrics();
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();

        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = "PSU";
        result.mode = opts.mode.isEmpty() ? "Steady" : opts.mode;
        result.duration = QString("%1s").arg(int(elapsed_secs));
        result.score = QString("%1 W total").arg(metrics.total_power_watts, 0, 'f', 1);
        result.passed = (last_psu_metrics.errors_cpu == 0 && last_psu_metrics.errors_gpu == 0);
        result.error_count = last_psu_metrics.errors_cpu + last_psu_metrics.errors_gpu;
        if (!result.passed) test_passed = false;
        results_.results.append(result);

    } else if (opts.test == "benchmark") {
        // Benchmarks are synchronous - run() blocks and returns result
        if (opts.mode == "storage") {
            emit_json("progress", "message", "Running storage benchmark (CrystalDiskMark-style)...");
            StorageEngine engine;

            std::string bench_path = opts.benchmark_path.isEmpty()
                ? "." : opts.benchmark_path.toStdString();
            int bench_file_size = opts.file_size_mb > 0 ? opts.file_size_mb : 1024;

            auto br = engine.run_benchmark(bench_path, bench_file_size);

            if (br.results.empty()) {
                emit_json("error", "storage_benchmark",
                    QVariant(QString::fromStdString(engine.last_error())));
                return 2;
            }

            QJsonObject bench;
            bench["device_path"] = QString::fromStdString(br.device_path);
            bench["timestamp"] = QString::fromStdString(br.timestamp);

            QJsonArray tests;
            for (const auto& t : br.results) {
                QJsonObject test_obj;
                test_obj["test_name"] = QString::fromStdString(t.test_name);
                test_obj["throughput_mbs"] = t.throughput_mbs;
                test_obj["iops"] = t.iops;
                test_obj["latency_us"] = t.latency_us;
                tests.append(test_obj);
            }
            bench["tests"] = tests;
            emit_json("result", "storage_benchmark", QJsonDocument(bench).toVariant());

            TestResultData result;
            result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            result.test_type = "Benchmark";
            result.mode = "Storage";
            // Use first sequential read result as headline score
            double seq_read_mbs = 0;
            double seq_write_mbs = 0;
            for (const auto& t : br.results) {
                if (t.test_name == "SEQ1M Q8T1 Read") seq_read_mbs = t.throughput_mbs;
                if (t.test_name == "SEQ1M Q8T1 Write") seq_write_mbs = t.throughput_mbs;
            }
            result.score = QString("SEQ R:%1 W:%2 MB/s")
                .arg(seq_read_mbs, 0, 'f', 1)
                .arg(seq_write_mbs, 0, 'f', 1);
            result.passed = true;
            result.error_count = 0;
            results_.results.append(result);
        }

        if (opts.mode == "cache" || opts.mode.isEmpty() || opts.mode == "all") {
            emit_json("progress", "message", "Running cache benchmark...");
            CacheBenchmark cb;
            auto cr = cb.run();

            QJsonObject bench;
            bench["l1_latency_ns"] = cr.l1_latency_ns;
            bench["l2_latency_ns"] = cr.l2_latency_ns;
            bench["l3_latency_ns"] = cr.l3_latency_ns;
            bench["dram_latency_ns"] = cr.dram_latency_ns;
            bench["l1_bw_gbs"] = cr.l1_bw_gbs;
            bench["l2_bw_gbs"] = cr.l2_bw_gbs;
            bench["l3_bw_gbs"] = cr.l3_bw_gbs;
            bench["dram_bw_gbs"] = cr.dram_bw_gbs;
            emit_json("result", "cache_benchmark", QJsonDocument(bench).toVariant());

            TestResultData result;
            result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            result.test_type = "Benchmark";
            result.mode = "Cache";
            result.score = QString("L1:%1ns L2:%2ns L3:%3ns DRAM:%4ns")
                .arg(cr.l1_latency_ns, 0, 'f', 2)
                .arg(cr.l2_latency_ns, 0, 'f', 2)
                .arg(cr.l3_latency_ns, 0, 'f', 2)
                .arg(cr.dram_latency_ns, 0, 'f', 2);
            result.passed = true;
            result.error_count = 0;
            results_.results.append(result);
        }
        if (opts.mode == "memory" || opts.mode.isEmpty() || opts.mode == "all") {
            emit_json("progress", "message", "Running memory benchmark...");
            MemoryBenchmark mb;
            auto mr = mb.run();

            QJsonObject bench;
            bench["read_bw_gbs"] = mr.read_bw_gbs;
            bench["write_bw_gbs"] = mr.write_bw_gbs;
            bench["copy_bw_gbs"] = mr.copy_bw_gbs;
            bench["latency_ns"] = mr.latency_ns;
            bench["channels_detected"] = mr.channels_detected;
            emit_json("result", "memory_benchmark", QJsonDocument(bench).toVariant());

            TestResultData result;
            result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
            result.test_type = "Benchmark";
            result.mode = "Memory";
            result.score = QString("R:%1 W:%2 C:%3 GB/s")
                .arg(mr.read_bw_gbs, 0, 'f', 2)
                .arg(mr.write_bw_gbs, 0, 'f', 2)
                .arg(mr.copy_bw_gbs, 0, 'f', 2);
            result.passed = true;
            result.error_count = 0;
            results_.results.append(result);
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();
        // Update duration on benchmark results
        for (auto& r : results_.results) {
            if (r.test_type == "Benchmark") {
                r.duration = QString("%1s").arg(int(elapsed_secs));
            }
        }

    } else {
        emit_json("error", "message", QString("Unknown test type: %1").arg(opts.test));
        return 2;
    }

    // Stop safety guardian (Gap 8)
    if (guardian) {
        guardian->stop();
        guardian.reset();
    }

    // Stop WHEA monitor
    stop_whea(whea);

    results_.overall_verdict = test_passed ? "PASS" : "FAIL";
    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    // Final result
    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["total_tests"] = results_.results.size();
    final_result["duration_secs"] = results_.total_duration_secs;
    if (whea_error_count_ > 0) {
        final_result["whea_errors"] = whea_error_count_;
    }
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return test_passed ? 0 : 1;
}

int CliRunner::run_schedule(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    emit_json("progress", "message", QString("Loading schedule from %1").arg(opts.schedule_file));

    TestScheduler scheduler;
    scheduler.load_from_json(opts.schedule_file);

    if (scheduler.steps().isEmpty()) {
        emit_json("error", "message", "Schedule is empty or failed to load");
        return 2;
    }

    scheduler.set_stop_on_error(opts.stop_on_error);

    bool all_passed = true;
    int total_errors = 0;

    QObject::connect(&scheduler, &TestScheduler::stepStarted,
        [this](int index, const QString& engine) {
            emit_json("progress", "message", QString("Step %1: starting %2").arg(index).arg(engine));
        });

    QObject::connect(&scheduler, &TestScheduler::stepCompleted,
        [this](int index, bool passed, int errors) {
            QJsonObject step;
            step["step"] = index;
            step["passed"] = passed;
            step["errors"] = errors;
            emit_json("step_result", "step", QJsonDocument(step).toVariant());
        });

    QObject::connect(&scheduler, &TestScheduler::progressChanged,
        [this](double pct) {
            emit_json("progress", "percent", pct);
        });

    auto start = std::chrono::steady_clock::now();
    scheduler.start();

    while (scheduler.is_running()) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }

    // Collect results from scheduler
    const auto& step_results = scheduler.results();
    for (const auto& sr : step_results) {
        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = sr.engine;
        result.mode = "schedule";
        result.duration = QString("%1s").arg(int(sr.duration_secs));
        result.passed = sr.passed;
        result.error_count = sr.errors;
        if (!sr.passed) {
            all_passed = false;
        }
        total_errors += sr.errors;
        results_.results.append(result);
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();
    results_.overall_verdict = all_passed ? "PASS" : "FAIL";

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["total_steps"] = step_results.size();
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = results_.total_duration_secs;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return all_passed ? 0 : 1;
}

int CliRunner::run_preset_schedule(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    QVector<TestStep> steps;
    QString preset_name;

    if (opts.preset == "quick") {
        steps = preset_quick_check();
        preset_name = "Quick Check";
    } else if (opts.preset == "standard") {
        steps = preset_standard();
        preset_name = "Standard";
    } else if (opts.preset == "extreme") {
        steps = preset_extreme();
        preset_name = "Extreme";
    } else if (opts.preset == "oc_validation") {
        steps = preset_oc_validation();
        preset_name = "OC Validation";
    } else {
        emit_json("error", "message",
            QString("Unknown preset: %1. Use quick, standard, extreme, or oc_validation.").arg(opts.preset));
        return 2;
    }

    emit_json("progress", "message", QString("Starting preset schedule: %1 (%2 steps)")
        .arg(preset_name).arg(steps.size()));

    TestScheduler scheduler;
    scheduler.load_schedule(steps);
    scheduler.set_stop_on_error(opts.stop_on_error);

    bool all_passed = true;
    int total_errors = 0;

    QObject::connect(&scheduler, &TestScheduler::stepStarted,
        [this](int index, const QString& engine) {
            emit_json("progress", "message", QString("Step %1: starting %2").arg(index).arg(engine));
        });

    QObject::connect(&scheduler, &TestScheduler::stepCompleted,
        [this](int index, bool passed, int errors) {
            QJsonObject step;
            step["step"] = index;
            step["passed"] = passed;
            step["errors"] = errors;
            emit_json("step_result", "step", QJsonDocument(step).toVariant());
        });

    QObject::connect(&scheduler, &TestScheduler::progressChanged,
        [this](double pct) {
            emit_json("progress", "percent", pct);
        });

    auto start = std::chrono::steady_clock::now();
    scheduler.start();

    while (scheduler.is_running()) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }

    // Collect results from scheduler
    const auto& step_results = scheduler.results();
    for (const auto& sr : step_results) {
        TestResultData result;
        result.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        result.test_type = sr.engine;
        result.mode = QString("preset:%1").arg(opts.preset);
        result.duration = QString("%1s").arg(int(sr.duration_secs));
        result.passed = sr.passed;
        result.error_count = sr.errors;
        if (!sr.passed) {
            all_passed = false;
        }
        total_errors += sr.errors;
        results_.results.append(result);
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();
    results_.overall_verdict = all_passed ? "PASS" : "FAIL";

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["preset"] = opts.preset;
    final_result["total_steps"] = step_results.size();
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = results_.total_duration_secs;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return all_passed ? 0 : 1;
}

int CliRunner::run_certificate(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    CertTier tier = CertTier::BRONZE;
    QVector<TestStep> steps;

    if (opts.cert_tier == "bronze") {
        tier = CertTier::BRONZE;
        steps = preset_cert_bronze();
    } else if (opts.cert_tier == "silver") {
        tier = CertTier::SILVER;
        steps = preset_cert_silver();
    } else if (opts.cert_tier == "gold") {
        tier = CertTier::GOLD;
        steps = preset_cert_gold();
    } else if (opts.cert_tier == "platinum") {
        tier = CertTier::PLATINUM;
        steps = preset_cert_platinum();
    } else {
        emit_json("error", "message", QString("Unknown certificate tier: %1. Use bronze, silver, gold, or platinum.").arg(opts.cert_tier));
        return 2;
    }

    emit_json("progress", "message", QString("Starting %1 certification").arg(cert_tier_name(tier)));

    TestScheduler scheduler;
    scheduler.load_schedule(steps);
    scheduler.set_stop_on_error(true); // Certification always stops on error

    QObject::connect(&scheduler, &TestScheduler::stepStarted,
        [this](int index, const QString& engine) {
            emit_json("progress", "message", QString("Cert step %1: %2").arg(index).arg(engine));
        });

    QObject::connect(&scheduler, &TestScheduler::stepCompleted,
        [this](int index, bool passed, int errors) {
            QJsonObject step;
            step["step"] = index;
            step["passed"] = passed;
            step["errors"] = errors;
            emit_json("step_result", "step", QJsonDocument(step).toVariant());
        });

    QObject::connect(&scheduler, &TestScheduler::progressChanged,
        [this](double pct) {
            emit_json("progress", "percent", pct);
        });

    auto start = std::chrono::steady_clock::now();
    scheduler.start();

    while (scheduler.is_running()) {
        QThread::msleep(500);
        QCoreApplication::processEvents();
    }

    // Build certificate
    bool all_passed = true;
    int total_errors = 0;
    QVector<occt::TestResult> cert_results;
    const auto& step_results = scheduler.results();
    for (const auto& sr : step_results) {
        occt::TestResult tr;
        tr.engine = sr.engine;
        tr.mode = "cert";
        tr.passed = sr.passed;
        tr.errors = sr.errors;
        tr.duration_secs = sr.duration_secs;
        cert_results.append(tr);

        if (!sr.passed) all_passed = false;
        total_errors += sr.errors;

        // Also add to report results
        TestResultData rd;
        rd.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        rd.test_type = sr.engine;
        rd.mode = "cert";
        rd.duration = QString("%1s").arg(int(sr.duration_secs));
        rd.passed = sr.passed;
        rd.error_count = sr.errors;
        results_.results.append(rd);
    }

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    results_.total_duration_secs = std::chrono::duration<double>(total_elapsed).count();
    results_.overall_verdict = all_passed ? "PASS" : "FAIL";

    // Generate certificate
    Certificate cert;
    cert.tier = tier;
    cert.system_info_json = CertGenerator::collect_system_info();
    cert.results = cert_results;
    cert.passed = all_passed;
    cert.issued_at = QDateTime::currentDateTime();
    cert.expires_at = cert.issued_at.addDays(90);

    CertGenerator gen;
    QJsonObject cert_json = gen.generate_json(cert);
    cert.hash_sha256 = CertGenerator::compute_hash(cert.system_info_json, cert_json);

    // Output certificate JSON
    emit_json("result", "certificate", QJsonDocument(cert_json).toVariant());

    // Save certificate if output path specified
    if (!opts.output_path.isEmpty()) {
        QDir dir(opts.output_path);
        if (opts.output_path.endsWith('/') || opts.output_path.endsWith('\\') || QFileInfo(opts.output_path).isDir()) {
            dir.mkpath(".");
        }
        QString base = "occt_cert_" + cert_tier_name(tier).toLower() + "_" +
                       QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");

        // Save HTML certificate
        QString html = gen.generate_html(cert);
        QString html_path = dir.filePath(base + ".html");
        QFile htmlFile(html_path);
        if (htmlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            htmlFile.write(html.toUtf8());
            htmlFile.close();
            emit_json("result", "cert_html_path", html_path);
        }

        // Save JSON certificate
        QJsonDocument json_doc(cert_json);
        QString json_path = dir.filePath(base + ".json");
        QFile jsonFile(json_path);
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            jsonFile.write(json_doc.toJson());
            jsonFile.close();
            emit_json("result", "cert_json_path", json_path);
        }
    }

    // Also generate standard report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["tier"] = cert_tier_name(tier);
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = results_.total_duration_secs;
    final_result["hash"] = cert.hash_sha256;
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return all_passed ? 0 : 1;
}

int CliRunner::run_monitor(const CliOptions& opts)
{
    int duration = opts.duration > 0 ? opts.duration : 60;

    emit_json("progress", "message", QString("Monitoring sensors for %1s").arg(duration));

    SensorManager sensors;
    if (!sensors.initialize()) {
        emit_json("error", "message", "Failed to initialize sensor manager");
        return 2;
    }

    QVector<SensorDataPoint> collected;
    auto start = std::chrono::steady_clock::now();

    sensors.start_polling(500);

    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();
        if (elapsed_secs >= duration) break;

        QThread::msleep(1000);
        QCoreApplication::processEvents();

        auto readings = sensors.get_all_readings();
        for (const auto& r : readings) {
            SensorDataPoint dp;
            dp.timestamp_sec = elapsed_secs;
            dp.sensor_name = QString::fromStdString(r.name);
            dp.value = r.value;
            dp.unit = QString::fromStdString(r.unit);
            collected.append(dp);

            QJsonObject metric;
            metric["sensor"] = dp.sensor_name;
            metric["value"] = dp.value;
            metric["unit"] = dp.unit;
            emit_json("metric", "sensor", QJsonDocument(metric).toVariant());
        }
    }

    sensors.stop();

    // Save CSV if requested
    if (!opts.output_path.isEmpty()) {
        CsvExporter::save_sensors(collected, opts.output_path);
        emit_json("result", "message", QString("Saved %1 data points to %2").arg(collected.size()).arg(opts.output_path));
    }

    return 0;
}

// ─── WHEA monitoring helpers (P3-4) ──────────────────────────────────────────

std::unique_ptr<WheaMonitor> CliRunner::start_whea_if_enabled(const CliOptions& opts)
{
    whea_error_count_ = 0;

    if (!opts.whea) return nullptr;

#ifndef _WIN32
    emit_json("warning", "message", "WHEA monitoring only available on Windows");
    return nullptr;
#else
    auto whea = std::make_unique<WheaMonitor>();
    whea->set_auto_stop(opts.stop_on_error);

    whea->set_error_callback([this](const WheaError& err) {
        QJsonObject w;
        w["type_str"] = err.source;
        w["description"] = err.description.left(256);
        emit_json("whea_error", "whea", QJsonDocument(w).toVariant());
    });

    if (whea->start()) {
        emit_json("progress", "message", "WHEA hardware error monitoring enabled");
    } else {
        emit_json("warning", "message", "Failed to start WHEA monitor");
        return nullptr;
    }
    return whea;
#endif
}

void CliRunner::stop_whea(std::unique_ptr<WheaMonitor>& whea)
{
    if (!whea) return;

    whea->stop();
    whea_error_count_ = whea->error_count();

    if (whea_error_count_ > 0) {
        emit_json("warning", "message",
            QString("WHEA detected %1 hardware error(s) during test").arg(whea_error_count_));
    }
    whea.reset();
}

// ─── Combined test (P3-5) ───────────────────────────────────────────────────

int CliRunner::run_combined(const CliOptions& opts)
{
    results_ = TestResults{};
    results_.system_info = collect_system_info();

    if (opts.engines.isEmpty()) {
        emit_json("error", "message", "Combined test requires --engines <list> (e.g. cpu,gpu,ram,storage)");
        return 2;
    }

    int duration = opts.duration > 0 ? opts.duration : 60;
    QStringList engine_list = opts.engines.split(',', Qt::SkipEmptyParts);

    for (auto& e : engine_list) e = e.trimmed().toLower();

    emit_json("progress", "message",
        QString("Starting combined test: %1 (duration: %2s)").arg(engine_list.join(", ")).arg(duration));

    // WHEA monitoring
    auto whea = start_whea_if_enabled(opts);

    // Create engines
    std::unique_ptr<CpuEngine> cpu;
    std::unique_ptr<GpuEngine> gpu;
    std::unique_ptr<RamEngine> ram;
    std::unique_ptr<StorageEngine> storage;

    CpuMetrics last_cpu{};
    GpuMetrics last_gpu{};
    RamMetrics last_ram{};
    StorageMetrics last_storage{};

    for (const auto& eng : engine_list) {
        if (eng == "cpu") {
            cpu = std::make_unique<CpuEngine>();
            cpu->set_metrics_callback([this, &last_cpu](const CpuMetrics& m) {
                last_cpu = m;
                QJsonObject metric;
                metric["gflops"] = m.gflops;
                if (m.temperature > 0.0)
                    metric["temperature"] = m.temperature;
                else
                    metric["temperature"] = QJsonValue(QJsonValue::Null);
                if (m.power_estimated)
                    metric["power_estimated"] = true;
                metric["threads"] = m.active_threads;
                metric["error_count"] = m.error_count;
                metric["elapsed"] = m.elapsed_secs;
                emit_json("metric", "cpu", QJsonDocument(metric).toVariant());
            });
        } else if (eng == "gpu") {
            gpu = std::make_unique<GpuEngine>();
            if (!gpu->initialize()) {
                emit_json("warning", "message", "Failed to initialize GPU engine, skipping GPU");
                gpu.reset();
                continue;
            }
            gpu->set_metrics_callback([this, &last_gpu](const GpuMetrics& m) {
                last_gpu = m;
                QJsonObject metric;
                metric["gflops"] = m.gflops;
                if (m.temperature > 0.0)
                    metric["temperature"] = m.temperature;
                else
                    metric["temperature"] = QJsonValue(QJsonValue::Null);
                metric["vram_errors"] = static_cast<qint64>(m.vram_errors);
                metric["elapsed"] = m.elapsed_secs;
                emit_json("metric", "gpu", QJsonDocument(metric).toVariant());
            });
        } else if (eng == "ram") {
            ram = std::make_unique<RamEngine>();
            ram->set_metrics_callback([this, &last_ram](const RamMetrics& m) {
                last_ram = m;
                QJsonObject metric;
                metric["tested_mb"] = m.memory_used_mb;
                metric["errors"] = static_cast<int>(m.errors_found);
                metric["elapsed"] = m.elapsed_secs;
                metric["pages_locked"] = m.pages_locked;
                emit_json("metric", "ram", QJsonDocument(metric).toVariant());
                if (!m.pages_locked && m.progress_pct < 1.0) {
                    emit_json("warning", "ram",
                        QString("Memory lock failure - test result reliability may be reduced"));
                }
            });
        } else if (eng == "storage") {
            storage = std::make_unique<StorageEngine>();
            storage->set_block_size_kb(opts.block_size_kb);
            storage->set_direct_io(opts.direct_io);
            storage->set_metrics_callback([this, &last_storage](const StorageMetrics& m) {
                last_storage = m;
                QJsonObject metric;
                metric["read_mbps"] = m.read_mbs;
                metric["write_mbps"] = m.write_mbs;
                metric["elapsed"] = m.elapsed_secs;
                if (m.verify_errors > 0) {
                    metric["verify_errors"] = static_cast<qint64>(m.verify_errors);
                    metric["crc_errors"] = static_cast<qint64>(m.crc_errors);
                    metric["magic_errors"] = static_cast<qint64>(m.magic_errors);
                    metric["index_errors"] = static_cast<qint64>(m.index_errors);
                    metric["pattern_errors"] = static_cast<qint64>(m.pattern_errors);
                    metric["io_errors"] = static_cast<qint64>(m.io_errors);
                    metric["first_error_block"] = static_cast<qint64>(m.first_error_block);
                    metric["last_error_block"] = static_cast<qint64>(m.last_error_block);
                }
                emit_json("metric", "storage", QJsonDocument(metric).toVariant());
            });
        } else {
            emit_json("warning", "message", QString("Unknown engine '%1', skipping").arg(eng));
        }
    }

    // Start all engines simultaneously
    auto start = std::chrono::steady_clock::now();

    if (cpu) cpu->start(CpuStressMode::AVX2_FMA, 0, duration);
    if (gpu) gpu->start(GpuStressMode::MATRIX_MUL, duration);
    if (ram) ram->start(RamPattern::MARCH_C_MINUS, 0.70, 999);
    if (storage) {
        std::string spath = opts.storage_path.isEmpty()
            ? QDir::tempPath().toStdString() : opts.storage_path.toStdString();
        storage->start(StorageMode::MIXED, spath, opts.file_size_mb);
    }

    // Wait until duration elapsed or all engines stop
    while (true) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        double elapsed_secs = std::chrono::duration<double>(elapsed).count();
        if (elapsed_secs >= duration) break;

        QThread::msleep(500);
        QCoreApplication::processEvents();

        // Check for errors if stop_on_error
        if (opts.stop_on_error) {
            int total_err = last_cpu.error_count
                + static_cast<int>(last_gpu.vram_errors)
                + static_cast<int>(last_ram.errors_found);
            if (total_err > 0) {
                emit_json("progress", "message", "Error detected, stopping all engines");
                break;
            }
        }

        // Check if all engines finished on their own
        bool all_done = true;
        if (cpu && cpu->is_running()) all_done = false;
        if (gpu && gpu->is_running()) all_done = false;
        if (ram && ram->is_running()) all_done = false;
        if (storage && storage->is_running()) all_done = false;
        if (all_done) break;
    }

    // Stop all engines
    if (cpu && cpu->is_running()) cpu->stop();
    if (gpu && gpu->is_running()) gpu->stop();
    if (ram && ram->is_running()) ram->stop();
    if (storage && storage->is_running()) storage->stop();

    stop_whea(whea);

    auto total_elapsed = std::chrono::steady_clock::now() - start;
    double total_secs = std::chrono::duration<double>(total_elapsed).count();

    // Collect results
    int total_errors = 0;
    bool test_passed = true;

    if (cpu) {
        TestResultData r;
        r.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        r.test_type = "CPU";
        r.mode = "combined";
        r.duration = QString("%1s").arg(int(total_secs));
        r.error_count = last_cpu.error_count;
        r.passed = (r.error_count == 0);
        r.score = QString("%1 GFLOPS").arg(last_cpu.gflops, 0, 'f', 2);
        if (!r.passed) test_passed = false;
        total_errors += r.error_count;
        results_.results.append(r);
    }
    if (gpu) {
        TestResultData r;
        r.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        r.test_type = "GPU";
        r.mode = "combined";
        r.duration = QString("%1s").arg(int(total_secs));
        r.error_count = static_cast<int>(last_gpu.vram_errors + last_gpu.artifact_count);
        r.passed = (r.error_count == 0);
        r.score = QString("%1 GFLOPS").arg(last_gpu.gflops, 0, 'f', 2);
        if (!r.passed) test_passed = false;
        total_errors += r.error_count;
        results_.results.append(r);
    }
    if (ram) {
        auto rm = ram->get_metrics();
        TestResultData r;
        r.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        r.test_type = "RAM";
        r.mode = "combined";
        r.duration = QString("%1s").arg(int(total_secs));
        r.error_count = static_cast<int>(rm.errors_found);
        r.passed = (r.error_count == 0);
        r.score = QString("%1 MB tested").arg(rm.memory_used_mb, 0, 'f', 0);
        if (!r.passed) test_passed = false;
        total_errors += r.error_count;
        results_.results.append(r);
    }
    if (storage) {
        auto sm = storage->get_metrics();
        TestResultData r;
        r.timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
        r.test_type = "Storage";
        r.mode = "combined";
        r.duration = QString("%1s").arg(int(total_secs));
        r.error_count = static_cast<int>(sm.verify_errors);
        r.passed = (sm.verify_errors == 0);
        r.score = QString("R:%1 W:%2 MB/s").arg(sm.read_mbs, 0, 'f', 1).arg(sm.write_mbs, 0, 'f', 1);
        if (!r.passed) test_passed = false;
        total_errors += r.error_count;
        results_.results.append(r);
    }

    results_.overall_verdict = test_passed ? "PASS" : "FAIL";
    results_.total_duration_secs = total_secs;

    // Generate report if requested
    if (!opts.report_format.isEmpty() && !opts.output_path.isEmpty()) {
        if (!generate_report(results_, opts)) {
            emit_json("error", "message", "Failed to generate report");
        }
    }

    // Final result
    QJsonObject final_result;
    final_result["verdict"] = results_.overall_verdict;
    final_result["engines"] = opts.engines;
    final_result["total_errors"] = total_errors;
    final_result["duration_secs"] = total_secs;
    if (whea_error_count_ > 0) {
        final_result["whea_errors"] = whea_error_count_;
    }
    emit_json("result", "summary", QJsonDocument(final_result).toVariant());

    return test_passed ? 0 : 1;
}

bool CliRunner::generate_report(const TestResults& results, const CliOptions& opts)
{
    ReportManager mgr;
    QString path = opts.output_path;

    // If output is a directory, generate filename
    QFileInfo fi(path);
    if (fi.isDir() || path.endsWith('/') || path.endsWith('\\')) {
        QDir dir(path);
        dir.mkpath(".");
        QString base = "occt_report_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        if (opts.report_format == "html") path = dir.filePath(base + ".html");
        else if (opts.report_format == "png") path = dir.filePath(base + ".png");
        else if (opts.report_format == "csv") path = dir.filePath(base + ".csv");
        else if (opts.report_format == "json") path = dir.filePath(base + ".json");
        else path = dir.filePath(base + ".html");
    }

    bool ok = false;
    if (opts.report_format == "html") ok = mgr.save_html(results, path);
    else if (opts.report_format == "png") ok = mgr.save_png(results, path);
    else if (opts.report_format == "csv") ok = mgr.save_csv(results, path);
    else if (opts.report_format == "json") ok = mgr.save_json(results, path);
    else ok = mgr.save_html(results, path);

    if (ok) {
        emit_json("result", "report_path", path);
    }
    return ok;
}

// ─── Report comparison (P4-3) ───────────────────────────────────────────────

int CliRunner::run_compare(const CliOptions& opts)
{
    emit_json("progress", "message",
        QString("Comparing reports:\n  A: %1\n  B: %2").arg(opts.compare_a).arg(opts.compare_b));

    auto result = compare_reports(opts.compare_a.toStdString(), opts.compare_b.toStdString());

    if (result.entries.empty()) {
        emit_json("error", "message", "No comparable metrics found between reports");
        return 2;
    }

    // Print text table to stdout
    std::string table = format_comparison_table(result);
    std::fprintf(stdout, "%s", table.c_str());
    std::fflush(stdout);

    // Also emit JSON result
    QJsonArray entries_json;
    for (const auto& e : result.entries) {
        QJsonObject ej;
        ej["metric"] = QString::fromStdString(e.metric_name);
        ej["value_a"] = e.value_a;
        ej["value_b"] = e.value_b;
        ej["diff_abs"] = e.diff_abs;
        ej["diff_pct"] = e.diff_pct;
        ej["direction"] = QString::fromStdString(e.direction);
        entries_json.append(ej);
    }

    QJsonObject cmp;
    cmp["entries"] = entries_json;
    cmp["summary"] = QString::fromStdString(result.summary);
    emit_json("result", "comparison", QJsonDocument(cmp).toVariant());

    return 0;
}

// ─── Certificate store (P4-4) ───────────────────────────────────────────────

int CliRunner::run_cert_upload(const CliOptions& opts)
{
    QFile f(opts.upload_cert);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit_json("error", "message", QString("Cannot open certificate file: %1").arg(opts.upload_cert));
        return 2;
    }
    std::string cert_json = f.readAll().toStdString();
    f.close();

    CertStore store;
    if (store.submit(cert_json)) {
        emit_json("result", "message", "Certificate uploaded successfully");
        return 0;
    } else {
        emit_json("error", "message", "Failed to upload certificate");
        return 2;
    }
}

int CliRunner::run_cert_verify(const CliOptions& opts)
{
    CertStore store;
    std::string hash = opts.verify_hash.toStdString();

    if (store.verify(hash)) {
        emit_json("result", "verified", true);
        std::fprintf(stdout, "Certificate %s: VERIFIED\n", hash.c_str());

        // Also output the certificate data
        std::string cert = store.lookup(hash);
        if (!cert.empty()) {
            std::fprintf(stdout, "%s\n", cert.c_str());
        }
        return 0;
    } else {
        emit_json("result", "verified", false);
        std::fprintf(stdout, "Certificate %s: NOT FOUND or INVALID\n", hash.c_str());
        return 1;
    }
}

int CliRunner::run_cert_list(const CliOptions& opts)
{
    (void)opts;
    CertStore store;
    auto hashes = store.list_hashes();

    std::fprintf(stdout, "Stored certificates: %zu\n", hashes.size());
    std::fprintf(stdout, "%-64s  %s\n", "SHA-256 Hash", "Status");
    std::fprintf(stdout, "%s\n", std::string(80, '-').c_str());

    for (const auto& h : hashes) {
        bool valid = store.verify(h);
        std::fprintf(stdout, "%-64s  %s\n", h.c_str(), valid ? "OK" : "INVALID");
    }

    QJsonArray arr;
    for (const auto& h : hashes) {
        arr.append(QString::fromStdString(h));
    }
    emit_json("result", "certs", QJsonDocument(arr).toVariant());

    return 0;
}

// ─── Leaderboard (P4-5) ─────────────────────────────────────────────────────

int CliRunner::run_leaderboard(const CliOptions& opts)
{
    Leaderboard lb;

    if (opts.leaderboard_cmd == "show") {
        std::string table = lb.format_table();
        std::fprintf(stdout, "%s", table.c_str());
        std::fflush(stdout);

        // Also emit JSON
        auto rankings = lb.get_rankings("overall");
        QJsonArray arr;
        for (const auto& e : rankings) {
            QJsonObject obj;
            obj["system_name"] = QString::fromStdString(e.system_name);
            obj["cpu_score"] = e.cpu_score;
            obj["gpu_score"] = e.gpu_score;
            obj["ram_score"] = e.ram_score;
            obj["storage_score"] = e.storage_score;
            obj["overall_score"] = e.overall_score;
            obj["timestamp"] = QString::fromStdString(e.timestamp);
            arr.append(obj);
        }
        emit_json("result", "leaderboard", QJsonDocument(arr).toVariant());
        return 0;

    } else if (opts.leaderboard_cmd == "submit") {
        // Collect system info for the entry name
        auto sysinfo = collect_system_info();

        BenchmarkEntry entry;
        entry.system_name = sysinfo.cpu_name.toStdString();
        if (sysinfo.gpu_name != "N/A") {
            entry.system_name += " + " + sysinfo.gpu_name.toStdString();
        }

        // If we have test results, extract scores from them
        for (const auto& r : results_.results) {
            if (r.test_type == "CPU" || r.test_type == "Benchmark") {
                // Try to extract GFLOPS from score string
                if (r.score.contains("GFLOPS")) {
                    bool ok = false;
                    double val = r.score.split(' ').first().toDouble(&ok);
                    if (ok && val > entry.cpu_score) entry.cpu_score = val;
                }
            }
            if (r.test_type == "GPU") {
                if (r.score.contains("GFLOPS")) {
                    bool ok = false;
                    double val = r.score.split(' ').first().toDouble(&ok);
                    if (ok && val > entry.gpu_score) entry.gpu_score = val;
                }
            }
            if (r.test_type == "RAM" || (r.test_type == "Benchmark" && r.mode == "Memory")) {
                if (r.score.contains("GB/s")) {
                    // Extract first number as read bandwidth
                    QString s = r.score;
                    s.remove("R:").remove("W:").remove("C:").remove("GB/s");
                    bool ok = false;
                    double val = s.trimmed().split(' ').first().toDouble(&ok);
                    if (ok && val > entry.ram_score) entry.ram_score = val;
                }
            }
            if (r.test_type == "Storage") {
                if (r.score.contains("MB/s")) {
                    // Extract read speed
                    QString s = r.score;
                    int idx = s.indexOf("R:");
                    if (idx >= 0) {
                        bool ok = false;
                        double val = s.mid(idx + 2).split(' ').first().toDouble(&ok);
                        if (ok && val > entry.storage_score) entry.storage_score = val;
                    }
                }
            }
        }

        entry.overall_score = Leaderboard::compute_overall_score(
            entry.cpu_score, entry.gpu_score, entry.ram_score, entry.storage_score);

        lb.submit(entry);

        emit_json("result", "message", QString("Submitted to leaderboard: %1 (score: %2)")
            .arg(QString::fromStdString(entry.system_name))
            .arg(entry.overall_score, 0, 'f', 2));

        // Show updated leaderboard
        std::string table = lb.format_table();
        std::fprintf(stdout, "%s", table.c_str());
        std::fflush(stdout);

        return 0;

    } else {
        emit_json("error", "message",
            QString("Unknown leaderboard command: %1. Use 'show' or 'submit'.").arg(opts.leaderboard_cmd));
        return 2;
    }
}

} // namespace occt
