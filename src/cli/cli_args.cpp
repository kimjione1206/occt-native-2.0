#include "cli_args.h"

#include <QTextStream>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace occt {

// Safe integer parsing with error detection and range checking.
// Returns true on success, prints error and returns false on failure.
static bool parse_int(const char* opt_name, const char* str, int& out, int min_val, int max_val)
{
    char* end = nullptr;
    errno = 0;
    long val = std::strtol(str, &end, 10);
    if (end == str || *end != '\0' || errno == ERANGE) {
        std::fprintf(stderr, "Error: invalid integer value '%s' for %s\n", str, opt_name);
        return false;
    }
    if (val < min_val || val > max_val) {
        std::fprintf(stderr, "Error: %s value %ld out of range [%d, %d]\n",
                     opt_name, val, min_val, max_val);
        return false;
    }
    out = static_cast<int>(val);
    return true;
}

// Safe float parsing with error detection and range checking.
static bool parse_float(const char* opt_name, const char* str, float& out, float min_val, float max_val)
{
    char* end = nullptr;
    errno = 0;
    double val = std::strtod(str, &end);
    if (end == str || *end != '\0' || errno == ERANGE) {
        std::fprintf(stderr, "Error: invalid numeric value '%s' for %s\n", str, opt_name);
        return false;
    }
    if (val < static_cast<double>(min_val) || val > static_cast<double>(max_val)) {
        std::fprintf(stderr, "Error: %s value %g out of range [%g, %g]\n",
                     opt_name, val, static_cast<double>(min_val), static_cast<double>(max_val));
        return false;
    }
    out = static_cast<float>(val);
    return true;
}

CliOptions parse_args(int argc, char** argv)
{
    CliOptions opts;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (std::strcmp(arg, "--cli") == 0) {
            opts.cli_mode = true;
        } else if (std::strcmp(arg, "--test") == 0 && i + 1 < argc) {
            opts.test = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            opts.mode = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--threads") == 0 && i + 1 < argc) {
            ++i;
            if (std::strcmp(argv[i], "auto") == 0) {
                opts.threads = 0;
            } else {
                parse_int("--threads", argv[i], opts.threads, 0, 4096);
            }
        } else if (std::strcmp(arg, "--duration") == 0 && i + 1 < argc) {
            parse_int("--duration", argv[++i], opts.duration, 0, 604800);
        } else if (std::strcmp(arg, "--schedule") == 0 && i + 1 < argc) {
            opts.schedule_file = QString::fromUtf8(argv[++i]);
            opts.test = "schedule";
        } else if (std::strcmp(arg, "--report") == 0 && i + 1 < argc) {
            opts.report_format = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--output") == 0 && i + 1 < argc) {
            opts.output_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--monitor-only") == 0) {
            opts.monitor_only = true;
        } else if (std::strcmp(arg, "--memory") == 0 && i + 1 < argc) {
            parse_int("--memory", argv[++i], opts.memory_percent, 1, 100);
        } else if (std::strcmp(arg, "--csv") == 0 && i + 1 < argc) {
            opts.report_format = "csv";
            opts.output_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--load-pattern") == 0 && i + 1 < argc) {
            opts.load_pattern = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--passes") == 0 && i + 1 < argc) {
            parse_int("--passes", argv[++i], opts.passes, 1, 100000);
        } else if (std::strcmp(arg, "--file-size") == 0 && i + 1 < argc) {
            parse_int("--file-size", argv[++i], opts.file_size_mb, 1, 1048576);
        } else if (std::strcmp(arg, "--queue-depth") == 0 && i + 1 < argc) {
            parse_int("--queue-depth", argv[++i], opts.queue_depth, 1, 256);
        } else if (std::strcmp(arg, "--storage-path") == 0 && i + 1 < argc) {
            opts.storage_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--benchmark-path") == 0 && i + 1 < argc) {
            opts.benchmark_path = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--gpu-index") == 0 && i + 1 < argc) {
            parse_int("--gpu-index", argv[++i], opts.gpu_index, -1, 64);
        } else if (std::strcmp(arg, "--shader-complexity") == 0 && i + 1 < argc) {
            parse_int("--shader-complexity", argv[++i], opts.shader_complexity, 1, 5);
        } else if (std::strcmp(arg, "--adaptive-mode") == 0 && i + 1 < argc) {
            opts.adaptive_mode = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--coil-freq") == 0 && i + 1 < argc) {
            parse_float("--coil-freq", argv[++i], opts.coil_freq, 0.0f, 15000.0f);
        } else if (std::strcmp(arg, "--use-all-gpus") == 0) {
            opts.use_all_gpus = true;
        } else if (std::strcmp(arg, "--stop-on-error") == 0) {
            opts.stop_on_error = true;
        } else if (std::strcmp(arg, "--cert-tier") == 0 && i + 1 < argc) {
            opts.cert_tier = QString::fromUtf8(argv[++i]);
            opts.test = "certificate";
        } else if (std::strcmp(arg, "--whea") == 0) {
            opts.whea = true;
        } else if (std::strcmp(arg, "--engines") == 0 && i + 1 < argc) {
            opts.engines = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--compare") == 0 && i + 2 < argc) {
            opts.compare_a = QString::fromUtf8(argv[++i]);
            opts.compare_b = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--upload") == 0 && i + 1 < argc) {
            opts.upload_cert = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--verify") == 0 && i + 1 < argc) {
            opts.verify_hash = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--list-certs") == 0) {
            opts.list_certs = true;
        } else if (std::strcmp(arg, "--leaderboard") == 0 && i + 1 < argc) {
            opts.leaderboard_cmd = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--backend") == 0 && i + 1 < argc) {
            opts.backend = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--intensity") == 0 && i + 1 < argc) {
            opts.intensity = QString::fromUtf8(argv[++i]);
        } else if (std::strcmp(arg, "--block-size") == 0 && i + 1 < argc) {
            ++i;
            int bs = 0;
            if (parse_int("--block-size", argv[i], bs, 4, 4096)) {
                // Validate allowed values
                if (bs == 4 || bs == 8 || bs == 64 || bs == 128 || bs == 1024 || bs == 4096) {
                    opts.block_size_kb = bs;
                } else {
                    std::fprintf(stderr, "Error: --block-size must be one of: 4, 8, 64, 128, 1024, 4096\n");
                }
            }
        } else if (std::strcmp(arg, "--direct-io") == 0) {
            opts.direct_io = true;
        } else if (std::strcmp(arg, "--post-update") == 0) {
            opts.post_update = true;
        } else if (std::strcmp(arg, "--cpu-temp-limit") == 0 && i + 1 < argc) {
            parse_int("--cpu-temp-limit", argv[++i], opts.cpu_temp_limit, 50, 120);
        } else if (std::strcmp(arg, "--gpu-temp-limit") == 0 && i + 1 < argc) {
            parse_int("--gpu-temp-limit", argv[++i], opts.gpu_temp_limit, 50, 120);
        } else if (std::strcmp(arg, "--power-limit") == 0 && i + 1 < argc) {
            parse_int("--power-limit", argv[++i], opts.power_limit, 50, 2000);
        } else if (std::strcmp(arg, "--preset") == 0 && i + 1 < argc) {
            opts.preset = QString::fromUtf8(argv[++i]);
            opts.test = "schedule";
        } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0) {
            opts.show_help = true;
        } else if (std::strcmp(arg, "--version") == 0 || std::strcmp(arg, "-v") == 0) {
            opts.show_version = true;
        }
    }

    return opts;
}

void print_usage()
{
    std::fprintf(stdout,
        "OCCT 네이티브 스트레스 테스트 - CLI 모드\n"
        "\n"
        "사용법:\n"
        "  occt_native --cli --test <유형> [옵션]\n"
        "  occt_native --cli --schedule <파일.json> [옵션]\n"
        "  occt_native --cli --cert-tier <등급> [옵션]\n"
        "  occt_native --cli --monitor-only [옵션]\n"
        "\n"
        "테스트 유형: cpu, gpu, ram, storage, psu, benchmark, certificate\n"
        "\n"
        "일반 옵션:\n"
        "  --cli                  CLI 모드로 실행 (GUI 없음)\n"
        "  --test <유형>          실행할 테스트 유형\n"
        "  --mode <모드>          테스트 모드 (아래 모드 참조)\n"
        "  --duration <초>        테스트 시간 (초 단위)\n"
        "  --report <형식>        보고서 형식: html, png, csv, json\n"
        "  --output <경로>        출력 파일/디렉토리 경로\n"
        "  --monitor-only         센서 모니터링만 수행 (스트레스 테스트 없음)\n"
        "  --csv <파일>           --report csv --output <파일>의 단축 표기\n"
        "  --help, -h             이 도움말 메시지 표시\n"
        "  --version, -v          버전 표시\n"
        "\n"
        "CPU 옵션:\n"
        "  --threads <N|auto>     스레드 수 (0 또는 auto = 전체 코어)\n"
        "  --load-pattern <p>     부하 패턴: steady (기본), variable\n"
        "  --intensity <수준>     강도: normal, extreme (기본: extreme)\n"
        "  모드: auto, avx2, avx, avx512, sse, linpack, prime, cache, large_data, all\n"
        "\n"
        "RAM 옵션:\n"
        "  --memory <퍼센트>      메모리 비율 (기본: 90)\n"
        "  --passes <N>           테스트 패스 횟수 (기본: 1)\n"
        "  모드: march_c (기본), walking, walking_zeros, checkerboard, random, bandwidth\n"
        "\n"
        "저장장치 옵션:\n"
        "  --file-size <MB>       테스트 파일 크기 (MB 단위, 기본: 256)\n"
        "  --queue-depth <N>      I/O 큐 깊이 (기본: 4)\n"
        "  --storage-path <경로>  테스트 디렉토리 (기본: 시스템 임시 폴더)\n"
        "  --block-size <KB>      블록 크기 (KB): 4, 8, 64, 128, 1024, 4096 (기본: 4)\n"
        "  --direct-io            직접 I/O 사용 (OS 파일 캐시 우회)\n"
        "  모드: write (기본), read, random_write, random_read, mixed,\n"
        "        verify_seq, verify_rand, fill_verify, butterfly\n"
        "\n"
        "GPU 옵션:\n"
        "  --gpu-index <N>        GPU 인덱스 (기본: auto)\n"
        "  --backend <유형>       GPU 백엔드: opencl, vulkan (기본: auto)\n"
        "  --shader-complexity <N> Vulkan 셰이더 복잡도 1-5 (기본: 1)\n"
        "  --adaptive-mode <m>    적응형 모드: variable (기본), switch, coil_whine\n"
        "  --coil-freq <Hz>       코일 주파수 10-15000 Hz (기본: 100, 0 = 스윕)\n"
        "  모드: matrix_mul, fp64/matrix_fp64, fma, trig, vram, mixed, vulkan_3d, vulkan_adaptive\n"
        "\n"
        "PSU 옵션:\n"
        "  --use-all-gpus         모든 GPU 사용 (기본: 첫 번째만)\n"
        "  모드: steady (기본), spike, ramp\n"
        "\n"
        "벤치마크 옵션:\n"
        "  --benchmark-path <경로> 저장장치 벤치마크 대상 경로 (기본: 현재 디렉토리)\n"
        "  모드: cache, memory, storage, all (기본: cache+memory)\n"
        "\n"
        "스케줄 옵션:\n"
        "  --schedule <파일>      JSON 스케줄 파일 실행\n"
        "  --preset <이름>        프리셋 스케줄 실행: quick, standard, extreme, oc_validation\n"
        "  --stop-on-error        첫 번째 오류 시 스케줄 중지\n"
        "\n"
        "인증서 옵션:\n"
        "  --cert-tier <등급>     인증 실행: bronze, silver, gold, platinum\n"
        "\n"
        "결합 테스트 옵션:\n"
        "  --test combined        여러 엔진을 동시에 실행\n"
        "  --engines <목록>       쉼표로 구분된 엔진: cpu,gpu,ram,storage\n"
        "\n"
        "모니터링 옵션:\n"
        "  --whea                 WHEA 하드웨어 오류 모니터링 활성화 (Windows 전용)\n"
        "\n"
        "안전 임계값:\n"
        "  --cpu-temp-limit <C>   CPU 온도 제한 (섭씨, 기본: 95)\n"
        "  --gpu-temp-limit <C>   GPU 온도 제한 (섭씨, 기본: 90)\n"
        "  --power-limit <W>      전력 제한 (와트, 기본: 300)\n"
        "\n"
        "보고서 비교 (P4-3):\n"
        "  --compare <a> <b>      두 JSON 보고서 파일 비교\n"
        "\n"
        "인증서 저장소 (P4-4):\n"
        "  --upload <cert.json>   로컬 저장소에 인증서 업로드\n"
        "  --verify <해시>        SHA-256 해시로 인증서 검증\n"
        "  --list-certs           저장된 모든 인증서 목록 표시\n"
        "\n"
        "리더보드 (P4-5):\n"
        "  --leaderboard show     현재 벤치마크 순위 표시\n"
        "  --leaderboard submit   현재 벤치마크 결과 제출\n"
        "\n"
        "종료 코드:\n"
        "  0 = 통과 (모든 테스트 통과)\n"
        "  1 = 실패 (테스트 중 오류 감지)\n"
        "  2 = 오류 (실행 실패)\n"
        "\n"
        "예제:\n"
        "  occt_native --cli --test cpu --mode avx2 --duration 3600 --threads auto\n"
        "  occt_native --cli --test cpu --mode all --load-pattern variable --duration 1800\n"
        "  occt_native --cli --test ram --memory 90 --passes 3 --duration 600\n"
        "  occt_native --cli --test storage --file-size 1024 --queue-depth 8 --storage-path /tmp\n"
        "  occt_native --cli --test gpu --mode vulkan_3d --gpu-index 0 --shader-complexity 3\n"
        "  occt_native --cli --test psu --mode spike --use-all-gpus --duration 600\n"
        "  occt_native --cli --test benchmark --mode all\n"
        "  occt_native --cli --test benchmark --mode storage --benchmark-path /tmp --file-size 1024\n"
        "  occt_native --cli --schedule schedule.json --stop-on-error --report html --output ./results/\n"
        "  occt_native --cli --cert-tier gold --output ./cert/\n"
        "  occt_native --cli --monitor-only --csv sensors.csv --duration 60\n"
        "  occt_native --cli --test combined --engines cpu,gpu --duration 60 --stop-on-error\n"
        "  occt_native --cli --test cpu --whea --duration 300\n"
        "  occt_native --cli --test cpu --intensity normal --cpu-temp-limit 85\n"
        "  occt_native --cli --test gpu --backend vulkan --gpu-temp-limit 80\n"
        "  occt_native --cli --test storage --mode fill_verify --block-size 1024 --direct-io\n"
        "  occt_native --cli --preset quick --stop-on-error\n"
    );
}

} // namespace occt
