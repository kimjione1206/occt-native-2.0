#pragma once

#include "config.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef OCCT_HAS_OPENCL

#ifdef OCCT_PLATFORM_MACOS
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

namespace occt { namespace gpu {

/// Convert an OpenCL error code to a human-readable string.
const char* cl_error_string(cl_int err);

/// RAII wrapper around an OpenCL context, device, and command queue.
class OpenCLContext {
public:
    OpenCLContext();
    ~OpenCLContext();

    OpenCLContext(const OpenCLContext&) = delete;
    OpenCLContext& operator=(const OpenCLContext&) = delete;

    /// Enumerate available platforms and devices.
    struct DeviceEntry {
        cl_platform_id platform;
        cl_device_id device;
        std::string platform_name;
        std::string device_name;
        std::string vendor;
        std::string driver_version;
        cl_ulong global_mem_bytes;
        cl_uint compute_units;
        cl_uint max_clock_mhz;
    };
    std::vector<DeviceEntry> enumerate_devices() const;

    /// Initialize context and queue for a specific device.
    bool init(cl_platform_id platform, cl_device_id device);

    /// Compile an OpenCL kernel from source.
    /// Returns CL_SUCCESS on success, otherwise the error code.
    cl_int compile_kernel(const std::string& source,
                          const std::string& kernel_name,
                          const std::string& build_options,
                          cl_kernel& out_kernel);

    /// Create a device buffer.
    cl_mem create_buffer(size_t size_bytes, cl_mem_flags flags);

    /// Enqueue an ND-range kernel execution.
    cl_int enqueue_ndrange(cl_kernel kernel,
                           cl_uint work_dim,
                           const size_t* global_work_size,
                           const size_t* local_work_size);

    /// Write host data to a device buffer (blocking).
    cl_int write_buffer(cl_mem buffer, const void* host_ptr, size_t size_bytes);

    /// Read device buffer to host memory (blocking).
    cl_int read_buffer(cl_mem buffer, void* host_ptr, size_t size_bytes);

    /// Block until all enqueued commands complete.
    cl_int finish();

    /// Release a buffer.
    void release_buffer(cl_mem buffer);

    /// Release a kernel.
    void release_kernel(cl_kernel kernel);

    /// Get the underlying device ID (for queries).
    cl_device_id device_id() const { return device_; }

    /// Get the underlying context.
    cl_context context() const { return context_; }

    /// Check if the context is initialized.
    bool is_valid() const { return context_ != nullptr; }

private:
    cl_platform_id platform_ = nullptr;
    cl_device_id device_ = nullptr;
    cl_context context_ = nullptr;
    cl_command_queue queue_ = nullptr;
};

}} // namespace occt::gpu

#else // !OCCT_HAS_OPENCL

// Stub declarations when OpenCL is not available
namespace occt { namespace gpu {

class OpenCLContext {
public:
    OpenCLContext() = default;
    ~OpenCLContext() = default;

    struct DeviceEntry {
        std::string platform_name;
        std::string device_name;
        std::string vendor;
        std::string driver_version;
        uint64_t global_mem_bytes = 0;
        unsigned int compute_units = 0;
        unsigned int max_clock_mhz = 0;
    };

    std::vector<DeviceEntry> enumerate_devices() const { return {}; }
    bool is_valid() const { return false; }
};

}} // namespace occt::gpu

#endif // OCCT_HAS_OPENCL
