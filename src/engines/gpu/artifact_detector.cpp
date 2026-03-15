#include "artifact_detector.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace occt { namespace gpu {

ArtifactDetector::ArtifactDetector() = default;
ArtifactDetector::~ArtifactDetector() = default;

void ArtifactDetector::set_reference_frame(const uint8_t* pixels,
                                            uint32_t width, uint32_t height) {
    size_t size = static_cast<size_t>(width) * height * 4;
    reference_.assign(pixels, pixels + size);
    ref_width_ = width;
    ref_height_ = height;
}

ArtifactResult ArtifactDetector::compare_frame(const uint8_t* pixels,
                                                uint32_t width, uint32_t height,
                                                int tolerance) {
    ArtifactResult result;
    result.total_pixels = static_cast<uint64_t>(width) * height;
    total_frames_.fetch_add(1, std::memory_order_relaxed);

    if (reference_.empty() || width != ref_width_ || height != ref_height_) {
        result.description = "No reference frame or dimension mismatch";
        return result;
    }

    size_t pixel_count = static_cast<size_t>(width) * height;
    std::vector<bool> error_map(pixel_count, false);

    // Compare pixel by pixel
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t offset = i * 4;
        bool has_error = false;

        for (int c = 0; c < 4; ++c) {
            int diff = static_cast<int>(pixels[offset + c]) -
                       static_cast<int>(reference_[offset + c]);
            if (std::abs(diff) > tolerance) {
                has_error = true;
                break;
            }
        }

        if (has_error) {
            error_map[i] = true;
            result.error_pixels++;
        }
    }

    result.error_rate = static_cast<double>(result.error_pixels) /
                        static_cast<double>(result.total_pixels);

    // Classify severity
    if (result.error_pixels == 0) {
        result.severity = ArtifactSeverity::NONE;
        result.primary_type = ArtifactType::NONE;
        result.description = "No artifacts detected";
    } else {
        total_artifacts_.fetch_add(result.error_pixels, std::memory_order_relaxed);

        if (result.error_rate < 0.0001) {
            result.severity = ArtifactSeverity::LOW;
        } else if (result.error_rate < 0.01) {
            result.severity = ArtifactSeverity::MEDIUM;
        } else if (result.error_rate < 0.1) {
            result.severity = ArtifactSeverity::HIGH;
        } else {
            result.severity = ArtifactSeverity::CRITICAL;
        }

        // Classify artifact types and find locations
        classify_artifacts(result, width, height, error_map, pixels);

        // Build description
        std::ostringstream oss;
        oss << result.error_pixels << " error pixels ("
            << (result.error_rate * 100.0) << "%), "
            << result.locations.size() << " regions";
        result.description = oss.str();
    }

    return result;
}

ArtifactResult ArtifactDetector::compare_frame(const std::vector<uint8_t>& pixels,
                                                uint32_t width, uint32_t height,
                                                int tolerance) {
    return compare_frame(pixels.data(), width, height, tolerance);
}

void ArtifactDetector::reset_statistics() {
    total_artifacts_.store(0, std::memory_order_relaxed);
    total_frames_.store(0, std::memory_order_relaxed);
}

void ArtifactDetector::classify_artifacts(ArtifactResult& result,
                                           uint32_t width, uint32_t height,
                                           const std::vector<bool>& error_map,
                                           const uint8_t* pixels) {
    // Check for full-frame corruption (>50% error pixels)
    if (result.error_rate > 0.5) {
        result.primary_type = ArtifactType::FULL_FRAME;
        ArtifactLocation loc;
        loc.x = 0;
        loc.y = 0;
        loc.width = width;
        loc.height = height;
        loc.type = ArtifactType::FULL_FRAME;
        result.locations.push_back(loc);
        return;
    }

    // Flood-fill to find connected error regions
    std::vector<bool> visited(error_map.size(), false);

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t idx = static_cast<size_t>(y) * width + x;
            if (!error_map[idx] || visited[idx]) continue;

            // BFS to find connected region
            uint32_t min_x = x, max_x = x, min_y = y, max_y = y;
            uint32_t pixel_count_in_region = 0;
            float max_error = 0.0f;

            std::vector<std::pair<uint32_t, uint32_t>> stack;
            stack.push_back({x, y});
            visited[idx] = true;

            while (!stack.empty()) {
                auto [cx, cy] = stack.back();
                stack.pop_back();
                pixel_count_in_region++;

                min_x = std::min(min_x, cx);
                max_x = std::max(max_x, cx);
                min_y = std::min(min_y, cy);
                max_y = std::max(max_y, cy);

                // Compute max error for this pixel
                size_t pix_offset = (static_cast<size_t>(cy) * width + cx) * 4;
                for (int c = 0; c < 4; ++c) {
                    float err = std::abs(
                        static_cast<float>(pixels[pix_offset + c]) -
                        static_cast<float>(reference_[pix_offset + c]));
                    max_error = std::max(max_error, err);
                }

                // Check 4-connected neighbors
                const int dx[] = {-1, 1, 0, 0};
                const int dy[] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    int nx = static_cast<int>(cx) + dx[d];
                    int ny = static_cast<int>(cy) + dy[d];
                    if (nx < 0 || nx >= static_cast<int>(width) ||
                        ny < 0 || ny >= static_cast<int>(height))
                        continue;
                    size_t nidx = static_cast<size_t>(ny) * width + nx;
                    if (error_map[nidx] && !visited[nidx]) {
                        visited[nidx] = true;
                        stack.push_back({static_cast<uint32_t>(nx),
                                         static_cast<uint32_t>(ny)});
                    }
                }

                // Limit region search to prevent excessive computation
                if (pixel_count_in_region > 10000) break;
            }

            ArtifactLocation loc;
            loc.x = min_x;
            loc.y = min_y;
            loc.width = max_x - min_x + 1;
            loc.height = max_y - min_y + 1;
            loc.max_error = max_error;

            if (pixel_count_in_region == 1) {
                loc.type = ArtifactType::SINGLE_PIXEL;
            } else if (pixel_count_in_region < 64) {
                loc.type = ArtifactType::SINGLE_PIXEL; // Small cluster
            } else {
                loc.type = ArtifactType::BLOCK;
            }

            result.locations.push_back(loc);

            // Limit number of tracked regions
            if (result.locations.size() >= 100) return;
        }
    }

    // Determine primary type from majority of regions
    int single_count = 0, block_count = 0;
    for (const auto& loc : result.locations) {
        if (loc.type == ArtifactType::SINGLE_PIXEL) single_count++;
        else if (loc.type == ArtifactType::BLOCK) block_count++;
    }

    if (block_count > single_count) {
        result.primary_type = ArtifactType::BLOCK;
    } else {
        result.primary_type = ArtifactType::SINGLE_PIXEL;
    }
}

}} // namespace occt::gpu
