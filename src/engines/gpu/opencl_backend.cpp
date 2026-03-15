#include "opencl_backend.h"

#ifdef OCCT_HAS_OPENCL

#include <cstring>
#include <iostream>

namespace occt { namespace gpu {

// ─── Error string helper ─────────────────────────────────────────────────────

const char* cl_error_string(cl_int err) {
    switch (err) {
    case CL_SUCCESS:                         return "CL_SUCCESS";
    case CL_DEVICE_NOT_FOUND:                return "CL_DEVICE_NOT_FOUND";
    case CL_DEVICE_NOT_AVAILABLE:            return "CL_DEVICE_NOT_AVAILABLE";
    case CL_COMPILER_NOT_AVAILABLE:          return "CL_COMPILER_NOT_AVAILABLE";
    case CL_MEM_OBJECT_ALLOCATION_FAILURE:   return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
    case CL_OUT_OF_RESOURCES:                return "CL_OUT_OF_RESOURCES";
    case CL_OUT_OF_HOST_MEMORY:              return "CL_OUT_OF_HOST_MEMORY";
    case CL_PROFILING_INFO_NOT_AVAILABLE:    return "CL_PROFILING_INFO_NOT_AVAILABLE";
    case CL_MEM_COPY_OVERLAP:                return "CL_MEM_COPY_OVERLAP";
    case CL_IMAGE_FORMAT_MISMATCH:           return "CL_IMAGE_FORMAT_MISMATCH";
    case CL_IMAGE_FORMAT_NOT_SUPPORTED:      return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
    case CL_BUILD_PROGRAM_FAILURE:           return "CL_BUILD_PROGRAM_FAILURE";
    case CL_MAP_FAILURE:                     return "CL_MAP_FAILURE";
    case CL_INVALID_VALUE:                   return "CL_INVALID_VALUE";
    case CL_INVALID_DEVICE_TYPE:             return "CL_INVALID_DEVICE_TYPE";
    case CL_INVALID_PLATFORM:               return "CL_INVALID_PLATFORM";
    case CL_INVALID_DEVICE:                  return "CL_INVALID_DEVICE";
    case CL_INVALID_CONTEXT:                return "CL_INVALID_CONTEXT";
    case CL_INVALID_QUEUE_PROPERTIES:        return "CL_INVALID_QUEUE_PROPERTIES";
    case CL_INVALID_COMMAND_QUEUE:           return "CL_INVALID_COMMAND_QUEUE";
    case CL_INVALID_HOST_PTR:               return "CL_INVALID_HOST_PTR";
    case CL_INVALID_MEM_OBJECT:             return "CL_INVALID_MEM_OBJECT";
    case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
    case CL_INVALID_IMAGE_SIZE:             return "CL_INVALID_IMAGE_SIZE";
    case CL_INVALID_SAMPLER:                return "CL_INVALID_SAMPLER";
    case CL_INVALID_BINARY:                 return "CL_INVALID_BINARY";
    case CL_INVALID_BUILD_OPTIONS:          return "CL_INVALID_BUILD_OPTIONS";
    case CL_INVALID_PROGRAM:                return "CL_INVALID_PROGRAM";
    case CL_INVALID_PROGRAM_EXECUTABLE:     return "CL_INVALID_PROGRAM_EXECUTABLE";
    case CL_INVALID_KERNEL_NAME:            return "CL_INVALID_KERNEL_NAME";
    case CL_INVALID_KERNEL_DEFINITION:      return "CL_INVALID_KERNEL_DEFINITION";
    case CL_INVALID_KERNEL:                 return "CL_INVALID_KERNEL";
    case CL_INVALID_ARG_INDEX:              return "CL_INVALID_ARG_INDEX";
    case CL_INVALID_ARG_VALUE:              return "CL_INVALID_ARG_VALUE";
    case CL_INVALID_ARG_SIZE:               return "CL_INVALID_ARG_SIZE";
    case CL_INVALID_KERNEL_ARGS:            return "CL_INVALID_KERNEL_ARGS";
    case CL_INVALID_WORK_DIMENSION:         return "CL_INVALID_WORK_DIMENSION";
    case CL_INVALID_WORK_GROUP_SIZE:        return "CL_INVALID_WORK_GROUP_SIZE";
    case CL_INVALID_WORK_ITEM_SIZE:         return "CL_INVALID_WORK_ITEM_SIZE";
    case CL_INVALID_GLOBAL_OFFSET:          return "CL_INVALID_GLOBAL_OFFSET";
    case CL_INVALID_EVENT_WAIT_LIST:        return "CL_INVALID_EVENT_WAIT_LIST";
    case CL_INVALID_EVENT:                  return "CL_INVALID_EVENT";
    case CL_INVALID_OPERATION:              return "CL_INVALID_OPERATION";
    case CL_INVALID_BUFFER_SIZE:            return "CL_INVALID_BUFFER_SIZE";
    default:                                return "CL_UNKNOWN_ERROR";
    }
}

// ─── Helper: query a device info string ──────────────────────────────────────

static std::string get_device_info_string(cl_device_id device, cl_device_info param) {
    size_t len = 0;
    clGetDeviceInfo(device, param, 0, nullptr, &len);
    if (len == 0) return "";
    std::string result(len, '\0');
    clGetDeviceInfo(device, param, len, &result[0], nullptr);
    // Remove trailing null
    while (!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}

static std::string get_platform_info_string(cl_platform_id platform, cl_platform_info param) {
    size_t len = 0;
    clGetPlatformInfo(platform, param, 0, nullptr, &len);
    if (len == 0) return "";
    std::string result(len, '\0');
    clGetPlatformInfo(platform, param, len, &result[0], nullptr);
    while (!result.empty() && result.back() == '\0')
        result.pop_back();
    return result;
}

// ─── OpenCLContext implementation ────────────────────────────────────────────

OpenCLContext::OpenCLContext() = default;

OpenCLContext::~OpenCLContext() {
    if (queue_) {
        clFlush(queue_);
        clFinish(queue_);
        clReleaseCommandQueue(queue_);
    }
    if (context_) {
        clReleaseContext(context_);
    }
}

std::vector<OpenCLContext::DeviceEntry> OpenCLContext::enumerate_devices() const {
    std::vector<DeviceEntry> result;

    cl_uint num_platforms = 0;
    if (clGetPlatformIDs(0, nullptr, &num_platforms) != CL_SUCCESS || num_platforms == 0) {
        return result;
    }

    std::vector<cl_platform_id> platforms(num_platforms);
    clGetPlatformIDs(num_platforms, platforms.data(), nullptr);

    for (auto& plat : platforms) {
        cl_uint num_devices = 0;
        if (clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices) != CL_SUCCESS)
            continue;
        if (num_devices == 0) continue;

        std::vector<cl_device_id> devices(num_devices);
        clGetDeviceIDs(plat, CL_DEVICE_TYPE_GPU, num_devices, devices.data(), nullptr);

        std::string plat_name = get_platform_info_string(plat, CL_PLATFORM_NAME);

        for (auto& dev : devices) {
            DeviceEntry entry;
            entry.platform = plat;
            entry.device = dev;
            entry.platform_name = plat_name;
            entry.device_name = get_device_info_string(dev, CL_DEVICE_NAME);
            entry.vendor = get_device_info_string(dev, CL_DEVICE_VENDOR);
            entry.driver_version = get_device_info_string(dev, CL_DRIVER_VERSION);

            clGetDeviceInfo(dev, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(entry.global_mem_bytes),
                            &entry.global_mem_bytes, nullptr);
            clGetDeviceInfo(dev, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(entry.compute_units),
                            &entry.compute_units, nullptr);
            clGetDeviceInfo(dev, CL_DEVICE_MAX_CLOCK_FREQUENCY, sizeof(entry.max_clock_mhz),
                            &entry.max_clock_mhz, nullptr);

            result.push_back(std::move(entry));
        }
    }

    return result;
}

bool OpenCLContext::init(cl_platform_id platform, cl_device_id device) {
    platform_ = platform;
    device_ = device;

    cl_int err = CL_SUCCESS;

    // Create context
    cl_context_properties props[] = {
        CL_CONTEXT_PLATFORM, reinterpret_cast<cl_context_properties>(platform),
        0
    };
    context_ = clCreateContext(props, 1, &device, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU] clCreateContext failed: " << cl_error_string(err) << std::endl;
        return false;
    }

    // Create command queue
#ifdef CL_VERSION_2_0
    cl_queue_properties qprops[] = { 0 };
    queue_ = clCreateCommandQueueWithProperties(context_, device, qprops, &err);
#else
    queue_ = clCreateCommandQueue(context_, device, 0, &err);
#endif
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU] clCreateCommandQueue failed: " << cl_error_string(err) << std::endl;
        clReleaseContext(context_);
        context_ = nullptr;
        return false;
    }

    return true;
}

cl_int OpenCLContext::compile_kernel(const std::string& source,
                                     const std::string& kernel_name,
                                     const std::string& build_options,
                                     cl_kernel& out_kernel) {
    cl_int err = CL_SUCCESS;
    const char* src = source.c_str();
    size_t len = source.size();

    cl_program program = clCreateProgramWithSource(context_, 1, &src, &len, &err);
    if (err != CL_SUCCESS) return err;

    err = clBuildProgram(program, 1, &device_, build_options.c_str(), nullptr, nullptr);
    if (err != CL_SUCCESS) {
        // Retrieve build log for diagnostics
        size_t log_size = 0;
        clGetProgramBuildInfo(program, device_, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        if (log_size > 1) {
            std::string log(log_size, '\0');
            clGetProgramBuildInfo(program, device_, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            std::cerr << "[GPU] OpenCL build log:\n" << log << std::endl;
        }
        clReleaseProgram(program);
        return err;
    }

    out_kernel = clCreateKernel(program, kernel_name.c_str(), &err);
    clReleaseProgram(program);
    return err;
}

cl_mem OpenCLContext::create_buffer(size_t size_bytes, cl_mem_flags flags) {
    cl_int err = CL_SUCCESS;
    cl_mem buf = clCreateBuffer(context_, flags, size_bytes, nullptr, &err);
    if (err != CL_SUCCESS) {
        std::cerr << "[GPU] clCreateBuffer(" << size_bytes << ") failed: "
                  << cl_error_string(err) << std::endl;
        return nullptr;
    }
    return buf;
}

cl_int OpenCLContext::enqueue_ndrange(cl_kernel kernel,
                                      cl_uint work_dim,
                                      const size_t* global_work_size,
                                      const size_t* local_work_size) {
    return clEnqueueNDRangeKernel(queue_, kernel, work_dim, nullptr,
                                  global_work_size, local_work_size,
                                  0, nullptr, nullptr);
}

cl_int OpenCLContext::write_buffer(cl_mem buffer, const void* host_ptr, size_t size_bytes) {
    return clEnqueueWriteBuffer(queue_, buffer, CL_TRUE, 0, size_bytes,
                                host_ptr, 0, nullptr, nullptr);
}

cl_int OpenCLContext::read_buffer(cl_mem buffer, void* host_ptr, size_t size_bytes) {
    return clEnqueueReadBuffer(queue_, buffer, CL_TRUE, 0, size_bytes,
                               host_ptr, 0, nullptr, nullptr);
}

cl_int OpenCLContext::finish() {
    return clFinish(queue_);
}

void OpenCLContext::release_buffer(cl_mem buffer) {
    if (buffer) clReleaseMemObject(buffer);
}

void OpenCLContext::release_kernel(cl_kernel kernel) {
    if (kernel) clReleaseKernel(kernel);
}

}} // namespace occt::gpu

#endif // OCCT_HAS_OPENCL
