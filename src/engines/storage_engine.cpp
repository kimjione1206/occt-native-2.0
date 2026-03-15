#include "storage_engine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iostream>
#include <random>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QDateTime>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>

// Convert UTF-8 std::string to std::wstring for Windows Unicode APIs
static std::wstring utf8_to_wide(const std::string& s) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring ws(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], wlen);
    return ws;
}
#else
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

#if defined(__linux__)
    #include <linux/fs.h>
#endif

namespace occt {

static constexpr size_t BLOCK_SIZE_4K   = 4096;
static constexpr size_t BLOCK_SIZE_128K = 128 * 1024;
static constexpr size_t BLOCK_SIZE_1M   = 1024 * 1024;

// ─── xoshiro256** (same fast PRNG for offset generation) ─────────────────────

static inline uint64_t rotl64(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

struct FastRng {
    uint64_t s[4];

    void seed(uint64_t v) {
        for (int i = 0; i < 4; ++i) {
            v += 0x9e3779b97f4a7c15ULL;
            uint64_t z = v;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            s[i] = z ^ (z >> 31);
        }
    }

    uint64_t next() {
        const uint64_t result = rotl64(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t; s[3] = rotl64(s[3], 45);
        return result;
    }
};

// ─── Constructor / Destructor ────────────────────────────────────────────────

StorageEngine::StorageEngine() = default;

StorageEngine::~StorageEngine() {
    stop();
}

void StorageEngine::log(const std::string& msg) {
    if (log_file_.isEmpty()) {
        QString logDir = QCoreApplication::applicationDirPath() + "/logs";
        QDir().mkpath(logDir);
        log_file_ = logDir + "/storage_engine.log";
    }
    QFile f(log_file_);
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << " " << QString::fromStdString(msg) << "\n";
    }
}

void StorageEngine::set_block_size_kb(uint32_t kb) {
    block_size_kb_ = kb;
}

void StorageEngine::set_direct_io(bool enabled) {
    force_direct_io_ = enabled;
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool StorageEngine::start(StorageMode mode, const std::string& path,
                          uint64_t file_size_mb, int queue_depth,
                          uint64_t duration_secs) {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    if (running_.load()) {
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            last_error_ = "Storage test already running";
        }
        log("[Storage] Start rejected: test already running");
        return false;
    }

    queue_depth = std::max(queue_depth, 1);
    uint64_t file_size_bytes = file_size_mb * 1024ULL * 1024ULL;

    // Validate path
    std::string dir = path;
    if (dir.empty()) dir = ".";

    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_ = StorageMetrics{};
        metrics_.state = "preparing";
    }

    stop_requested_.store(false);
    running_.store(true);
    duration_secs_.store(duration_secs);
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        last_error_.clear();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    log("[Storage] Starting test: mode=" + std::to_string(static_cast<int>(mode))
        + " dir=" + dir + " file_size=" + std::to_string(file_size_bytes)
        + " queue_depth=" + std::to_string(queue_depth));

    worker_ = std::thread(&StorageEngine::run, this, mode, dir,
                          file_size_bytes, queue_depth);
    return true;
}

void StorageEngine::stop() {
    std::lock_guard<std::mutex> guard(start_stop_mutex_);

    log("[Storage] Stop requested");

    stop_requested_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
    running_.store(false);

    // Delete test file in background to avoid blocking UI
    std::string path_copy;
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        path_copy = test_file_path_;
        test_file_path_.clear();
    }
    if (!path_copy.empty()) {
        log("[Storage] Stopped. File cleanup: " + path_copy);
        std::thread([path_copy]() {
#if defined(_WIN32)
            DeleteFileW(utf8_to_wide(path_copy).c_str());
#else
            unlink(path_copy.c_str());
#endif
        }).detach();
    }
}

bool StorageEngine::is_running() const {
    return running_.load();
}

StorageMetrics StorageEngine::get_metrics() const {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    return metrics_;
}

void StorageEngine::set_metrics_callback(MetricsCallback cb) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    metrics_cb_ = std::move(cb);
}

// ─── Platform-specific helpers ───────────────────────────────────────────────

intptr_t StorageEngine::open_direct(const std::string& path, bool read_only) {
#if defined(_WIN32)
    std::wstring wpath = utf8_to_wide(path);

    DWORD access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD creation = read_only ? OPEN_EXISTING : CREATE_ALWAYS;

    if (!force_direct_io_) {
        HANDLE h = CreateFileW(wpath.c_str(), access, 0, nullptr,
                               creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return -1;
        log("[Storage] Active mode: buffered I/O (direct I/O disabled)");
        return reinterpret_cast<intptr_t>(h);
    }

    DWORD flags = FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH;

    HANDLE h = CreateFileW(wpath.c_str(), access, 0, nullptr,
                           creation, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        log("[Storage] Direct I/O failed (error " + std::to_string(err) + "), retrying buffered");
        flags = FILE_FLAG_WRITE_THROUGH;
        h = CreateFileW(wpath.c_str(), access, 0, nullptr,
                        creation, flags, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            h = CreateFileW(wpath.c_str(), access, 0, nullptr,
                            creation, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return -1;
            log("[Storage] Active mode: fully buffered I/O");
        } else {
            log("[Storage] Active mode: buffered I/O with write-through");
        }
    } else {
        log("[Storage] Active mode: direct I/O (unbuffered)");
    }
    return reinterpret_cast<intptr_t>(h);
#else
    int flags_val = read_only ? O_RDONLY : (O_RDWR | O_CREAT | O_TRUNC);

    if (!force_direct_io_) {
        // Buffered mode requested — skip O_DIRECT / F_NOCACHE
        int fd = open(path.c_str(), flags_val, 0644);
        if (fd < 0) return -1;
        std::cerr << "[Storage] Active mode: buffered I/O (direct I/O disabled)" << std::endl;
        return static_cast<intptr_t>(fd);
    }

#if defined(__linux__)
    flags_val |= O_DIRECT | O_SYNC;
#endif
    int fd = open(path.c_str(), flags_val, 0644);

#if defined(__linux__)
    if (fd < 0 && (flags_val & O_DIRECT)) {
        // O_DIRECT may fail on some filesystems (tmpfs, USB drives, etc.)
        std::cerr << "[Storage] Warning: O_DIRECT not supported, "
                  << "falling back to buffered I/O" << std::endl;
        flags_val &= ~(O_DIRECT | O_SYNC);
        fd = open(path.c_str(), flags_val, 0644);
        if (fd >= 0) {
            std::cerr << "[Storage] Active mode: buffered I/O" << std::endl;
        }
    } else if (fd >= 0) {
        std::cerr << "[Storage] Active mode: direct I/O (O_DIRECT | O_SYNC)" << std::endl;
    }
#endif

    if (fd < 0) return -1;

#if defined(__APPLE__)
    // macOS: F_NOCACHE disables buffer cache (similar to O_DIRECT)
    if (fcntl(fd, F_NOCACHE, 1) == 0) {
        std::cerr << "[Storage] Active mode: direct I/O (F_NOCACHE)" << std::endl;
    } else {
        std::cerr << "[Storage] Active mode: buffered I/O (F_NOCACHE failed)" << std::endl;
    }
#endif
    return static_cast<intptr_t>(fd);
#endif
}

void StorageEngine::close_file(intptr_t fd) {
    if (fd < 0) return;
#if defined(_WIN32)
    CloseHandle(reinterpret_cast<HANDLE>(fd));
#else
    close(static_cast<int>(fd));
#endif
}

uint8_t* StorageEngine::alloc_aligned(size_t size) {
    // Alignment must be at least the configured block size (for direct I/O),
    // and at least 4096 (typical sector size requirement).
    size_t alignment = std::max(static_cast<size_t>(block_size_kb_) * 1024, static_cast<size_t>(4096));
#if defined(_WIN32)
    return static_cast<uint8_t*>(_aligned_malloc(size, alignment));
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return static_cast<uint8_t*>(ptr);
#endif
}

void StorageEngine::free_aligned(uint8_t* ptr) {
    if (!ptr) return;
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// ─── Worker thread ───────────────────────────────────────────────────────────

void StorageEngine::run(StorageMode mode, const std::string& path,
                        uint64_t file_size_bytes, int queue_depth) {
    try {
    log("[Storage] Worker thread started");
    // Build test file path
    std::string dir = path;
    if (dir.empty()) dir = ".";
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        test_file_path_ = dir + "occt_storage_test.bin";
    }

    // Allocate I/O buffer (1 MB for sequential, configured block size for random)
    size_t buf_size = BLOCK_SIZE_1M;
    uint8_t* io_buf = alloc_aligned(buf_size);
    if (!io_buf) {
        running_.store(false);
        return;
    }

    // Fill buffer with pattern
    std::memset(io_buf, 0xA5, buf_size);

    bool is_verify_mode = (mode == StorageMode::VERIFY_SEQ ||
                           mode == StorageMode::VERIFY_RAND ||
                           mode == StorageMode::FILL_VERIFY ||
                           mode == StorageMode::BUTTERFLY);

    bool needs_existing_file = (mode == StorageMode::SEQ_READ ||
                                mode == StorageMode::RAND_READ ||
                                mode == StorageMode::MIXED);

    // For read modes, create the file first
    if (needs_existing_file) {
        intptr_t wfd = open_direct(test_file_path_, false);
        if (wfd < 0) {
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.state = "error";
            }
            {
                std::lock_guard<std::mutex> elk(metrics_mutex_);
                last_error_ = "Failed to create test file at: " + test_file_path_;
            }
            log("[Storage] Error: Failed to create test file at: " + test_file_path_);
            free_aligned(io_buf);
            running_.store(false);
            return;
        }

        uint64_t written = 0;
        while (written < file_size_bytes && !stop_requested_.load()) {
            size_t to_write = std::min(static_cast<uint64_t>(buf_size),
                                       file_size_bytes - written);
#if defined(_WIN32)
            DWORD bytes_written = 0;
            BOOL ok = WriteFile(reinterpret_cast<HANDLE>(wfd),
                      io_buf, static_cast<DWORD>(to_write), &bytes_written, nullptr);
            if (!ok || bytes_written == 0) break;
            written += bytes_written;
#else
            ssize_t ret = write(static_cast<int>(wfd), io_buf, to_write);
            if (ret <= 0) break;
            written += static_cast<uint64_t>(ret);
#endif
            // Update preparation progress
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.state = "preparing";
                metrics_.progress_pct = (static_cast<double>(written) / file_size_bytes) * 100.0;
            }
        }
        close_file(wfd);
    }

    // Verify modes manage their own file I/O
    if (is_verify_mode) {
        free_aligned(io_buf);
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.state = "testing";
            metrics_.progress_pct = 0.0;
        }

        switch (mode) {
            case StorageMode::VERIFY_SEQ:
                verify_seq(test_file_path_, file_size_bytes, queue_depth);
                break;
            case StorageMode::VERIFY_RAND:
                verify_rand(test_file_path_, file_size_bytes, queue_depth);
                break;
            case StorageMode::FILL_VERIFY:
                fill_verify(test_file_path_, file_size_bytes, queue_depth);
                break;
            case StorageMode::BUTTERFLY:
                butterfly_verify(test_file_path_, file_size_bytes, queue_depth);
                break;
            default:
                break;
        }
    } else {
        // Open file for the actual test
        bool ro = (mode == StorageMode::SEQ_READ || mode == StorageMode::RAND_READ);
        intptr_t fd = open_direct(test_file_path_, ro);
        if (fd < 0) {
            {
                std::lock_guard<std::mutex> lock(metrics_mutex_);
                last_error_ = "Failed to open test file: " + test_file_path_;
                metrics_.state = "error";
                metrics_.error_count++;
            }
            log("[Storage] Error: Failed to open test file: " + test_file_path_);
            free_aligned(io_buf);
            running_.store(false);
            return;
        }

        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.state = "testing";
            metrics_.progress_pct = 0.0;
        }

        switch (mode) {
            case StorageMode::SEQ_WRITE:
                seq_write(fd, io_buf, buf_size, file_size_bytes, queue_depth);
                break;
            case StorageMode::SEQ_READ:
                seq_read(fd, io_buf, buf_size, file_size_bytes, queue_depth);
                break;
            case StorageMode::RAND_WRITE:
                rand_write(fd, io_buf, file_size_bytes, queue_depth);
                break;
            case StorageMode::RAND_READ:
                rand_read(fd, io_buf, file_size_bytes, queue_depth);
                break;
            case StorageMode::MIXED:
                mixed_io(fd, io_buf, file_size_bytes, queue_depth);
                break;
            default:
                break;
        }

        close_file(fd);
        free_aligned(io_buf);
    }

    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.state = "completed";
    }
    log("[Storage] Worker completed. State: completed");

    running_.store(false);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            last_error_ = std::string("Exception: ") + e.what();
            metrics_.state = "error";
        }
        log("[Storage] Exception: " + std::string(e.what()));
        running_.store(false);
    } catch (...) {
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            last_error_ = "Unknown exception in storage worker";
            metrics_.state = "error";
        }
        log("[Storage] Unknown exception in worker thread");
        running_.store(false);
    }
}

// ─── Sequential Write ────────────────────────────────────────────────────────

void StorageEngine::seq_write(intptr_t fd, uint8_t* buf, size_t buf_size,
                              uint64_t file_size, int /*queue_depth*/) {
    auto start = std::chrono::steady_clock::now();
    uint64_t total_written = 0;
    uint64_t ops = 0;
    const uint64_t dur = duration_secs_.load();
    uint64_t pass_written = 0; // bytes written in current pass

    while (!stop_requested_.load()) {
        if (dur > 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= static_cast<double>(dur)) break;
        }
        // If we've written the entire file, seek back to start for another pass
        if (pass_written >= file_size) {
            if (dur == 0) break; // No duration = single pass only
#if defined(_WIN32)
            LARGE_INTEGER zero = {};
            SetFilePointerEx(reinterpret_cast<HANDLE>(fd), zero, nullptr, FILE_BEGIN);
#else
            lseek(static_cast<int>(fd), 0, SEEK_SET);
#endif
            pass_written = 0;
        }
        size_t to_write = std::min(static_cast<uint64_t>(buf_size),
                                   file_size - pass_written);

#if defined(_WIN32)
        DWORD bytes_written = 0;
        BOOL ok = WriteFile(reinterpret_cast<HANDLE>(fd),
                  buf, static_cast<DWORD>(to_write), &bytes_written, nullptr);
        if (!ok || bytes_written == 0) {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.error_count++;
            if (stop_on_error()) {
                stop_requested_.store(true);
            }
            break;
        }
        total_written += bytes_written;
        pass_written += bytes_written;
#else
        ssize_t ret = write(static_cast<int>(fd), buf, to_write);
        if (ret <= 0) {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.error_count++;
            if (stop_on_error()) {
                stop_requested_.store(true);
            }
            break;
        }
        total_written += static_cast<uint64_t>(ret);
        pass_written += static_cast<uint64_t>(ret);
#endif
        ops++;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(total_written) / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(total_written) / file_size) * 100.0;
        }

        // Invoke metrics callback
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            cb_copy = metrics_cb_;
        }
        if (cb_copy) {
            StorageMetrics snap;
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                snap = metrics_;
            }
            cb_copy(snap);
        }
    }
}

// ─── Sequential Read ─────────────────────────────────────────────────────────

void StorageEngine::seq_read(intptr_t fd, uint8_t* buf, size_t buf_size,
                             uint64_t file_size, int /*queue_depth*/) {
    auto start = std::chrono::steady_clock::now();
    uint64_t total_read = 0;
    uint64_t ops = 0;
    const uint64_t dur = duration_secs_.load();
    uint64_t pass_read = 0; // bytes read in current pass

    while (!stop_requested_.load()) {
        if (dur > 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= static_cast<double>(dur)) break;
        }
        // If we've read the entire file, seek back to start for another pass
        if (pass_read >= file_size) {
            if (dur == 0) break; // No duration = single pass only
#if defined(_WIN32)
            LARGE_INTEGER zero = {};
            SetFilePointerEx(reinterpret_cast<HANDLE>(fd), zero, nullptr, FILE_BEGIN);
#else
            lseek(static_cast<int>(fd), 0, SEEK_SET);
#endif
            pass_read = 0;
        }
        size_t to_read = std::min(static_cast<uint64_t>(buf_size),
                                  file_size - pass_read);

#if defined(_WIN32)
        DWORD bytes_read = 0;
        BOOL ok = ReadFile(reinterpret_cast<HANDLE>(fd),
                 buf, static_cast<DWORD>(to_read), &bytes_read, nullptr);
        if (!ok || bytes_read == 0) {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.error_count++;
            if (stop_on_error()) {
                stop_requested_.store(true);
            }
            break;
        }
        total_read += bytes_read;
        pass_read += bytes_read;
#else
        ssize_t ret = read(static_cast<int>(fd), buf, to_read);
        if (ret <= 0) {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.error_count++;
            if (stop_on_error()) {
                stop_requested_.store(true);
            }
            break;
        }
        total_read += static_cast<uint64_t>(ret);
        pass_read += static_cast<uint64_t>(ret);
#endif
        ops++;

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.read_mbs = elapsed > 0 ?
                (static_cast<double>(total_read) / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.progress_pct = (static_cast<double>(total_read) / file_size) * 100.0;
        }

        // Invoke metrics callback
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            cb_copy = metrics_cb_;
        }
        if (cb_copy) {
            StorageMetrics snap;
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                snap = metrics_;
            }
            cb_copy(snap);
        }
    }
}

// ─── Random 4K Write ─────────────────────────────────────────────────────────

void StorageEngine::rand_write(intptr_t fd, uint8_t* buf, uint64_t file_size,
                               int queue_depth) {
    (void)buf; // shared buffer not used; per-thread buffers allocated below
    auto start = std::chrono::steady_clock::now();
    const size_t blk = static_cast<size_t>(block_size_kb_) * 1024;
    const uint64_t max_blocks = file_size / blk;
    if (max_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    uint64_t ops = 0;
    const uint64_t target_ops = max_blocks; // one pass over all blocks

    std::vector<std::thread> workers;
    std::atomic<uint64_t> shared_ops{0};

    auto worker_fn = [&](int /*id*/) {
        // Allocate per-thread aligned buffer to avoid data race
        uint8_t* local_buf = alloc_aligned(blk);
        if (!local_buf) return;
        std::memset(local_buf, 0xA5, blk);

        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            if (dur > 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                if (elapsed >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
            }

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * blk;

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD written = 0;
            BOOL ok = WriteFile(reinterpret_cast<HANDLE>(fd),
                      local_buf, static_cast<DWORD>(blk), &written, &ov);
            if (!ok || written == 0) {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.error_count++;
                if (stop_on_error()) {
                    stop_requested_.store(true);
                }
            }
#else
            ssize_t ret = pwrite(static_cast<int>(fd), local_buf, blk, static_cast<off_t>(offset));
            if (ret <= 0) {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.error_count++;
                if (stop_on_error()) {
                    stop_requested_.store(true);
                }
            }
#endif
        }

        free_aligned(local_buf);
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    // Monitor progress
    while (!stop_requested_.load()) {
        ops = shared_ops.load();
        if (ops >= target_ops) break;

        if (dur > 0) {
            auto now = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(now - start).count();
            if (elapsed >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.write_mbs = metrics_.iops * blk / (1024.0 * 1024.0);
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        // Invoke metrics callback
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            cb_copy = metrics_cb_;
        }
        if (cb_copy) {
            StorageMetrics snap;
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                snap = metrics_;
            }
            cb_copy(snap);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    // Final metrics
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.write_mbs = metrics_.iops * blk / (1024.0 * 1024.0);
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

// ─── Random 4K Read ──────────────────────────────────────────────────────────

void StorageEngine::rand_read(intptr_t fd, uint8_t* buf, uint64_t file_size,
                              int queue_depth) {
    auto start = std::chrono::steady_clock::now();
    const size_t blk = static_cast<size_t>(block_size_kb_) * 1024;
    const uint64_t max_blocks = file_size / blk;
    if (max_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    uint64_t target_ops = max_blocks;
    std::atomic<uint64_t> shared_ops{0};
    std::vector<std::thread> workers;

    // Per-thread aligned read buffer
    auto worker_fn = [&](int /*id*/) {
        uint8_t* local_buf = alloc_aligned(blk);
        if (!local_buf) return;

        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            if (dur > 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                if (elapsed >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
            }

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * blk;

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD bytes_read = 0;
            BOOL ok = ReadFile(reinterpret_cast<HANDLE>(fd),
                     local_buf, static_cast<DWORD>(blk), &bytes_read, &ov);
            if (!ok || bytes_read == 0) {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.error_count++;
                if (stop_on_error()) {
                    stop_requested_.store(true);
                }
            }
#else
            ssize_t ret = pread(static_cast<int>(fd), local_buf, blk, static_cast<off_t>(offset));
            if (ret <= 0) {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                metrics_.error_count++;
                if (stop_on_error()) {
                    stop_requested_.store(true);
                }
            }
#endif
        }

        free_aligned(local_buf);
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    while (!stop_requested_.load()) {
        uint64_t ops = shared_ops.load();
        if (ops >= target_ops) break;

        if (dur > 0) {
            auto now_chk = std::chrono::steady_clock::now();
            double elapsed_chk = std::chrono::duration<double>(now_chk - start).count();
            if (elapsed_chk >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
            metrics_.read_mbs = metrics_.iops * blk / (1024.0 * 1024.0);
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        // Invoke metrics callback
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            cb_copy = metrics_cb_;
        }
        if (cb_copy) {
            StorageMetrics snap;
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                snap = metrics_;
            }
            cb_copy(snap);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    uint64_t ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.read_mbs = metrics_.iops * blk / (1024.0 * 1024.0);
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

// ─── Mixed I/O (70% read / 30% write) ───────────────────────────────────────

void StorageEngine::mixed_io(intptr_t fd, uint8_t* buf, uint64_t file_size,
                             int queue_depth) {
    auto start = std::chrono::steady_clock::now();
    const size_t blk = static_cast<size_t>(block_size_kb_) * 1024;
    const uint64_t max_blocks = file_size / blk;
    if (max_blocks == 0) return;
    const uint64_t dur = duration_secs_.load();

    uint64_t target_ops = max_blocks;
    std::atomic<uint64_t> shared_ops{0};
    std::atomic<uint64_t> total_reads{0};
    std::atomic<uint64_t> total_writes{0};
    std::vector<std::thread> workers;

    auto worker_fn = [&](int /*id*/) {
        uint8_t* local_buf = alloc_aligned(blk);
        if (!local_buf) return;
        std::memset(local_buf, 0xCD, blk);

        FastRng local_rng;
        local_rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                       + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        while (!stop_requested_.load()) {
            uint64_t current = shared_ops.fetch_add(1);
            if (current >= target_ops) break;

            if (dur > 0) {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - start).count();
                if (elapsed >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
            }

            uint64_t block = local_rng.next() % max_blocks;
            uint64_t offset = block * blk;
            bool do_read = (local_rng.next() % 100) < 70; // 70% reads

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD bytes = 0;
            if (do_read) {
                BOOL ok = ReadFile(reinterpret_cast<HANDLE>(fd),
                         local_buf, static_cast<DWORD>(blk), &bytes, &ov);
                if (!ok || bytes == 0) {
                    std::lock_guard<std::mutex> lk(metrics_mutex_);
                    metrics_.error_count++;
                    if (stop_on_error()) stop_requested_.store(true);
                }
                total_reads.fetch_add(1);
            } else {
                BOOL ok = WriteFile(reinterpret_cast<HANDLE>(fd),
                          local_buf, static_cast<DWORD>(blk), &bytes, &ov);
                if (!ok || bytes == 0) {
                    std::lock_guard<std::mutex> lk(metrics_mutex_);
                    metrics_.error_count++;
                    if (stop_on_error()) stop_requested_.store(true);
                }
                total_writes.fetch_add(1);
            }
#else
            if (do_read) {
                ssize_t ret = pread(static_cast<int>(fd), local_buf, blk, static_cast<off_t>(offset));
                if (ret <= 0) {
                    std::lock_guard<std::mutex> lk(metrics_mutex_);
                    metrics_.error_count++;
                    if (stop_on_error()) stop_requested_.store(true);
                }
                total_reads.fetch_add(1);
            } else {
                ssize_t ret = pwrite(static_cast<int>(fd), local_buf, blk, static_cast<off_t>(offset));
                if (ret <= 0) {
                    std::lock_guard<std::mutex> lk(metrics_mutex_);
                    metrics_.error_count++;
                    if (stop_on_error()) stop_requested_.store(true);
                }
                total_writes.fetch_add(1);
            }
#endif
        }

        free_aligned(local_buf);
    };

    for (int i = 0; i < queue_depth; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    while (!stop_requested_.load()) {
        uint64_t ops = shared_ops.load();
        if (ops >= target_ops) break;

        if (dur > 0) {
            auto now_chk = std::chrono::steady_clock::now();
            double elapsed_chk = std::chrono::duration<double>(now_chk - start).count();
            if (elapsed_chk >= static_cast<double>(dur)) { stop_requested_.store(true); break; }
        }

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        double iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        {
            std::lock_guard<std::mutex> lk(metrics_mutex_);
            metrics_.elapsed_secs = elapsed;
            metrics_.iops = iops;
            uint64_t r = total_reads.load();
            uint64_t w = total_writes.load();
            metrics_.read_mbs = elapsed > 0 ?
                (static_cast<double>(r) * blk / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.write_mbs = elapsed > 0 ?
                (static_cast<double>(w) * blk / (1024.0 * 1024.0)) / elapsed : 0;
            metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
            metrics_.progress_pct = (static_cast<double>(ops) / target_ops) * 100.0;
        }

        // Invoke metrics callback
        MetricsCallback cb_copy;
        {
            std::lock_guard<std::mutex> lk(cb_mutex_);
            cb_copy = metrics_cb_;
        }
        if (cb_copy) {
            StorageMetrics snap;
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                snap = metrics_;
            }
            cb_copy(snap);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    uint64_t ops = shared_ops.load();
    {
        std::lock_guard<std::mutex> lk(metrics_mutex_);
        metrics_.elapsed_secs = elapsed;
        metrics_.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
        metrics_.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;
        metrics_.progress_pct = 100.0;
    }
}

// ─── CrystalDiskMark-style Benchmark ─────────────────────────────────────────

StorageBenchmarkResult::TestResult StorageEngine::run_bench_test(
    const std::string& test_name, const std::string& file_path,
    bool is_sequential, bool is_read, size_t block_size,
    int queue_depth, int thread_count, uint64_t file_size_bytes,
    double duration_secs)
{
    StorageBenchmarkResult::TestResult result;
    result.test_name = test_name;
    result.throughput_mbs = 0;
    result.iops = 0;
    result.latency_us = 0;

    // For write tests, open read-write; for read tests, open read-only
    intptr_t fd = open_direct(file_path, is_read);
    if (fd < 0) return result;

    const uint64_t max_blocks = file_size_bytes / block_size;
    if (max_blocks == 0) {
        close_file(fd);
        return result;
    }

    std::atomic<uint64_t> total_bytes{0};
    std::atomic<uint64_t> total_ops{0};
    std::atomic<bool> bench_done{false};

    auto worker_fn = [&](int /*id*/) {
        uint8_t* buf = alloc_aligned(block_size);
        if (!buf) return;
        std::memset(buf, 0xA5, block_size);

        FastRng rng;
        rng.seed(std::chrono::steady_clock::now().time_since_epoch().count()
                 + std::hash<std::thread::id>{}(std::this_thread::get_id()));

        uint64_t seq_offset = 0;

        while (!bench_done.load(std::memory_order_relaxed) && !stop_requested_.load(std::memory_order_relaxed)) {
            uint64_t offset;
            if (is_sequential) {
                offset = seq_offset;
                seq_offset += block_size;
                if (seq_offset >= file_size_bytes) seq_offset = 0;
            } else {
                uint64_t block = rng.next() % max_blocks;
                offset = block * block_size;
            }

#if defined(_WIN32)
            OVERLAPPED ov{};
            ov.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
            ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
            DWORD bytes_done = 0;
            if (is_read) {
                ReadFile(reinterpret_cast<HANDLE>(fd),
                         buf, static_cast<DWORD>(block_size), &bytes_done, &ov);
            } else {
                WriteFile(reinterpret_cast<HANDLE>(fd),
                          buf, static_cast<DWORD>(block_size), &bytes_done, &ov);
            }
            if (bytes_done > 0) {
                total_bytes.fetch_add(bytes_done, std::memory_order_relaxed);
                total_ops.fetch_add(1, std::memory_order_relaxed);
            }
#else
            ssize_t ret;
            if (is_read) {
                ret = pread(static_cast<int>(fd), buf, block_size, static_cast<off_t>(offset));
            } else {
                ret = pwrite(static_cast<int>(fd), buf, block_size, static_cast<off_t>(offset));
            }
            if (ret > 0) {
                total_bytes.fetch_add(static_cast<uint64_t>(ret), std::memory_order_relaxed);
                total_ops.fetch_add(1, std::memory_order_relaxed);
            }
#endif
        }

        free_aligned(buf);
    };

    // Spawn worker threads
    std::vector<std::thread> workers;
    int actual_threads = std::max(thread_count, 1);
    for (int i = 0; i < actual_threads; ++i) {
        workers.emplace_back(worker_fn, i);
    }

    // Run for the specified duration
    auto start = std::chrono::steady_clock::now();
    while (!stop_requested_.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        if (elapsed >= duration_secs) break;
    }
    bench_done.store(true, std::memory_order_relaxed);

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();

    uint64_t bytes = total_bytes.load();
    uint64_t ops = total_ops.load();

    result.throughput_mbs = elapsed > 0 ? (static_cast<double>(bytes) / (1024.0 * 1024.0)) / elapsed : 0;
    result.iops = elapsed > 0 ? static_cast<double>(ops) / elapsed : 0;
    result.latency_us = ops > 0 ? (elapsed * 1e6) / static_cast<double>(ops) : 0;

    close_file(fd);
    return result;
}

StorageBenchmarkResult StorageEngine::run_benchmark(const std::string& path,
                                                     int file_size_mb) {
    StorageBenchmarkResult bench;
    bench.device_path = path;

    // Generate timestamp
    {
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        bench.timestamp = buf;
    }

    // Build test file path
    std::string dir = path;
    if (dir.empty()) dir = ".";
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';
    std::string test_file = dir + "occt_bench_test.bin";

    uint64_t file_size_bytes = static_cast<uint64_t>(file_size_mb) * 1024ULL * 1024ULL;

    // Create the test file (fill with data for reads)
    {
        intptr_t fd = open_direct(test_file, false);
        if (fd < 0) {
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                last_error_ = "Failed to create benchmark test file at: " + test_file;
            }
            return bench;
        }

        uint8_t* buf = alloc_aligned(BLOCK_SIZE_1M);
        if (!buf) {
            close_file(fd);
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                last_error_ = "Failed to allocate I/O buffer";
            }
            return bench;
        }
        std::memset(buf, 0xA5, BLOCK_SIZE_1M);

        uint64_t written = 0;
        while (written < file_size_bytes) {
            size_t to_write = std::min(static_cast<uint64_t>(BLOCK_SIZE_1M),
                                       file_size_bytes - written);
#if defined(_WIN32)
            DWORD bytes_written = 0;
            BOOL ok = WriteFile(reinterpret_cast<HANDLE>(fd),
                      buf, static_cast<DWORD>(to_write), &bytes_written, nullptr);
            if (!ok || bytes_written == 0) break;
            written += bytes_written;
#else
            ssize_t ret = write(static_cast<int>(fd), buf, to_write);
            if (ret <= 0) break;
            written += static_cast<uint64_t>(ret);
#endif
        }

        free_aligned(buf);
        close_file(fd);

        if (written < file_size_bytes) {
            {
                std::lock_guard<std::mutex> lk(metrics_mutex_);
                last_error_ = "Failed to write full benchmark file";
            }
#if defined(_WIN32)
            DeleteFileW(utf8_to_wide(test_file).c_str());
#else
            unlink(test_file.c_str());
#endif
            return bench;
        }
    }

    static constexpr double TEST_DURATION = 5.0; // seconds per test

    // CrystalDiskMark test sequence:
    // 1. SEQ1M Q8T1 Read
    bench.results.push_back(run_bench_test(
        "SEQ1M Q8T1 Read", test_file,
        /*sequential=*/true, /*is_read=*/true,
        BLOCK_SIZE_1M, /*queue_depth=*/8, /*threads=*/1,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 2. SEQ1M Q8T1 Write
    bench.results.push_back(run_bench_test(
        "SEQ1M Q8T1 Write", test_file,
        /*sequential=*/true, /*is_read=*/false,
        BLOCK_SIZE_1M, /*queue_depth=*/8, /*threads=*/1,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 3. SEQ128K Q32T1 Read
    bench.results.push_back(run_bench_test(
        "SEQ128K Q32T1 Read", test_file,
        /*sequential=*/true, /*is_read=*/true,
        BLOCK_SIZE_128K, /*queue_depth=*/32, /*threads=*/1,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 4. SEQ128K Q32T1 Write
    bench.results.push_back(run_bench_test(
        "SEQ128K Q32T1 Write", test_file,
        /*sequential=*/true, /*is_read=*/false,
        BLOCK_SIZE_128K, /*queue_depth=*/32, /*threads=*/1,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 5. RND4K Q32T16 Read
    bench.results.push_back(run_bench_test(
        "RND4K Q32T16 Read", test_file,
        /*sequential=*/false, /*is_read=*/true,
        BLOCK_SIZE_4K, /*queue_depth=*/32, /*threads=*/16,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 6. RND4K Q32T16 Write
    bench.results.push_back(run_bench_test(
        "RND4K Q32T16 Write", test_file,
        /*sequential=*/false, /*is_read=*/false,
        BLOCK_SIZE_4K, /*queue_depth=*/32, /*threads=*/16,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 7. RND4K Q1T1 Read
    bench.results.push_back(run_bench_test(
        "RND4K Q1T1 Read", test_file,
        /*sequential=*/false, /*is_read=*/true,
        BLOCK_SIZE_4K, /*queue_depth=*/1, /*threads=*/1,
        file_size_bytes, TEST_DURATION));
    if (stop_requested_.load()) goto cleanup;

    // 8. RND4K Q1T1 Write
    bench.results.push_back(run_bench_test(
        "RND4K Q1T1 Write", test_file,
        /*sequential=*/false, /*is_read=*/false,
        BLOCK_SIZE_4K, /*queue_depth=*/1, /*threads=*/1,
        file_size_bytes, TEST_DURATION));

cleanup:
    // Cleanup test file
#if defined(_WIN32)
    DeleteFileW(utf8_to_wide(test_file).c_str());
#else
    unlink(test_file.c_str());
#endif

    return bench;
}

std::string StorageEngine::last_error() const {
    std::lock_guard<std::mutex> lk(metrics_mutex_);
    return last_error_;
}

} // namespace occt
