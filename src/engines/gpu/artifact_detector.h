#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace occt { namespace gpu {

/// Classification of detected artifacts.
enum class ArtifactType {
    NONE,
    SINGLE_PIXEL,  // Individual pixel errors (VRAM bit-flip)
    BLOCK,         // Block of erroneous pixels (shader unit error)
    FULL_FRAME,    // Entire frame corrupted (driver crash / hang)
};

/// Severity level of detected artifacts.
enum class ArtifactSeverity {
    NONE,
    LOW,       // < 0.01% pixels affected
    MEDIUM,    // 0.01% - 1% pixels affected
    HIGH,      // 1% - 10% pixels affected
    CRITICAL,  // > 10% pixels affected
};

/// Location of a detected artifact region.
struct ArtifactLocation {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    ArtifactType type = ArtifactType::NONE;
    float max_error = 0.0f;  // Max per-channel error in region
};

/// Result of artifact comparison.
struct ArtifactResult {
    uint64_t total_pixels = 0;
    uint64_t error_pixels = 0;
    double error_rate = 0.0;     // error_pixels / total_pixels
    ArtifactSeverity severity = ArtifactSeverity::NONE;
    ArtifactType primary_type = ArtifactType::NONE;
    std::vector<ArtifactLocation> locations;
    std::string description;
};

/// Detects rendering artifacts by comparing frames against a reference.
class ArtifactDetector {
public:
    ArtifactDetector();
    ~ArtifactDetector();

    /// Set the reference frame to compare against.
    /// pixels: RGBA8 pixel data.
    void set_reference_frame(const uint8_t* pixels, uint32_t width, uint32_t height);

    /// Compare a frame against the reference.
    /// Returns artifact analysis results.
    /// tolerance: per-channel tolerance (default 1 for quantization noise).
    ArtifactResult compare_frame(const uint8_t* pixels, uint32_t width, uint32_t height,
                                  int tolerance = 1);

    /// Compare a frame against the reference using vectors.
    ArtifactResult compare_frame(const std::vector<uint8_t>& pixels,
                                  uint32_t width, uint32_t height,
                                  int tolerance = 1);

    /// Check if a reference frame has been set.
    bool has_reference() const { return !reference_.empty(); }

    /// Get cumulative artifact statistics.
    uint64_t total_artifacts_detected() const { return total_artifacts_.load(std::memory_order_relaxed); }
    uint64_t total_frames_compared() const { return total_frames_.load(std::memory_order_relaxed); }

    /// Reset cumulative statistics.
    void reset_statistics();

private:
    void classify_artifacts(ArtifactResult& result, uint32_t width, uint32_t height,
                            const std::vector<bool>& error_map,
                            const uint8_t* pixels);

    std::vector<uint8_t> reference_;
    uint32_t ref_width_ = 0;
    uint32_t ref_height_ = 0;

    std::atomic<uint64_t> total_artifacts_{0};
    std::atomic<uint64_t> total_frames_{0};
};

}} // namespace occt::gpu
