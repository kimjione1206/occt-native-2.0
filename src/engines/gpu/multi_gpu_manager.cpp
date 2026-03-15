#include "multi_gpu_manager.h"

#include <iostream>

namespace occt { namespace gpu {

MultiGpuManager::MultiGpuManager() = default;
MultiGpuManager::~MultiGpuManager() {
    stop_all();
}

int MultiGpuManager::initialize() {
    // Create a temporary engine to enumerate GPUs
    auto probe = std::make_unique<GpuEngine>();
    if (!probe->initialize()) {
        return 0;
    }

    gpu_infos_ = probe->get_available_gpus();
    probe.reset();

    if (gpu_infos_.empty()) {
        return 0;
    }

    // Create one engine per GPU
    for (int i = 0; i < static_cast<int>(gpu_infos_.size()); ++i) {
        auto engine = std::make_unique<GpuEngine>();
        engine->initialize();
        engine->select_gpu(i);
        engines_.push_back(std::move(engine));
    }

    std::cout << "[MultiGPU] Found " << engines_.size() << " GPU(s)" << std::endl;
    return static_cast<int>(engines_.size());
}

void MultiGpuManager::start_all(GpuStressMode mode, int duration_secs) {
    for (auto& engine : engines_) {
        if (!engine->is_running()) {
            engine->start(mode, duration_secs);
        }
    }
}

void MultiGpuManager::stop_all() {
    for (auto& engine : engines_) {
        engine->stop();
    }
}

bool MultiGpuManager::any_running() const {
    for (const auto& engine : engines_) {
        if (engine->is_running()) return true;
    }
    return false;
}

std::vector<MultiGpuManager::GpuMetricsEntry> MultiGpuManager::get_all_metrics() const {
    std::vector<GpuMetricsEntry> results;
    for (int i = 0; i < static_cast<int>(engines_.size()); ++i) {
        GpuMetricsEntry entry;
        entry.gpu_index = i;
        entry.gpu_name = (i < static_cast<int>(gpu_infos_.size()))
                             ? gpu_infos_[i].name
                             : "Unknown";
        entry.metrics = engines_[i]->get_metrics();
        results.push_back(std::move(entry));
    }
    return results;
}

GpuEngine* MultiGpuManager::get_engine(int index) {
    if (index < 0 || index >= static_cast<int>(engines_.size())) return nullptr;
    return engines_[index].get();
}

}} // namespace occt::gpu
