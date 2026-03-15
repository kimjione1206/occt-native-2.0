# OCCT Native - 개선 로드맵

실제 OCCT (OCBASE) 대비 갭 분석 기반 개선 계획서

---

## Phase 1: CPU 에러 검증 시스템 (최우선)

> OCCT의 핵심 가치: "부하를 주는 것"이 아니라 "에러를 감지하는 것"

### 1-1. 결과 검증 엔진 (error_verifier)

**현재**: FMA 루프만 실행, 결과 무시
**목표**: 사전 계산된 기대값과 실시간 비교, 비트 에러 감지

```
신규 파일:
  src/engines/cpu/error_verifier.h
  src/engines/cpu/error_verifier.cpp
```

**구조:**
```cpp
struct CpuError {
    int core_id;                    // 어느 코어에서 발생
    uint64_t address;               // 에러 메모리 주소
    uint64_t expected;              // 기대값
    uint64_t actual;                // 실제값
    uint64_t bit_mask;              // 플립된 비트
    std::chrono::time_point timestamp;
    int error_code;                 // 1=다른스레드정지, 2=사용자중단, 3=메모리부족, 4=계산에러
};

class ErrorVerifier {
    // 결정론적 FMA 체인 - 같은 input → 같은 output (IEEE 754 보장)
    // 시드값으로 초기 operand 생성 → N회 FMA 수행 → 결과를 기대값과 비교
    static double compute_expected(uint64_t seed, int iterations);
    bool verify(double result, double expected, int core_id);
    std::vector<CpuError> get_errors() const;
};
```

**알고리즘:**
1. 시드(seed) → 결정론적 FMA 체인 (a = a * b + c 반복)
2. IEEE 754 부동소수점 보장으로 정확한 기대값 사전 계산
3. AVX2: 4개 double 병렬 검증, AVX-512: 8개 double 병렬 검증
4. 에러 발생 시 비트 XOR로 플립 위치 특정
5. Thread affinity로 코어별 에러 리포팅

**수정 파일:**
- `src/engines/cpu_engine.h` - CpuMetrics에 error_count, errors 벡터 추가
- `src/engines/cpu_engine.cpp` - worker_thread에 검증 로직 통합
- `src/engines/cpu/avx_stress.cpp` - 검증 모드 추가
- `src/gui/panels/cpu_panel.cpp` - 에러 카운트 표시, 코어별 에러 맵

**예상 규모:** ~600줄

### 1-2. Variable/Steady 부하 패턴

**현재**: 고정 부하만
**목표**: OCCT처럼 10분마다 operand 변경 (Variable) vs 고정 (Steady)

```
수정 파일:
  src/engines/cpu_engine.h   - LoadPattern enum 추가
  src/engines/cpu_engine.cpp - cycle 기반 operand 변경 로직
```

**구조:**
```cpp
enum class LoadPattern {
    VARIABLE,  // 10분마다 operand 변경 → 다양한 에러 패턴 노출
    STEADY     // 고정 operand → 쿨링 성능 측정용
};
```

**예상 규모:** ~150줄

### 1-3. Linpack 고도화 (OpenBLAS 통합)

**현재**: 자체 타일링 DGEMM (느림)
**목표**: OpenBLAS/MKL 연동으로 진짜 Linpack 급 성능

```
수정 파일:
  src/engines/cpu/linpack.h/cpp - OpenBLAS cblas_dgemm 호출
  CMakeLists.txt               - find_package(BLAS) 추가
  vcpkg.json                   - openblas 의존성
```

**예상 규모:** ~200줄

---

## Phase 2: Vulkan 3D 렌더링 + 아티팩트 감지

> OpenCL compute만으로는 실제 GPU 안정성 테스트 불가

### 2-1. Vulkan 렌더링 백엔드

**현재**: OpenCL compute 커널만
**목표**: Vulkan 기반 실제 3D 렌더링 (크로스플랫폼)

```
신규 파일:
  src/engines/gpu/vulkan_backend.h/cpp     - VkInstance, VkDevice, Swapchain 관리
  src/engines/gpu/vulkan_renderer.h/cpp    - 렌더링 파이프라인, 셰이더, 드로우콜
  src/engines/gpu/artifact_detector.h/cpp  - 프레임버퍼 비교 에러 감지
  src/engines/gpu/shaders/stress.vert      - 버텍스 셰이더 (GLSL→SPIR-V)
  src/engines/gpu/shaders/stress.frag      - 프래그먼트 셰이더 (복잡도 1~5)
  src/engines/gpu/shaders/tessellation.tesc/tese - 테셀레이션 셰이더
```

**렌더링 씬:**
- 고밀도 테셀레이션 구체 (64K+ 삼각형)
- 복잡한 프래그먼트 셰이더 (phong + noise + shadow)
- 다수 드로우콜 (100~1000개 오브젝트)
- 포스트프로세싱 (bloom, SSAO)

**Shader 복잡도 레벨:**
```cpp
enum class ShaderComplexity {
    LEVEL_1,  // 기본 phong 셰이딩
    LEVEL_2,  // + 노말맵 + 스펙큘러
    LEVEL_3,  // + 테셀레이션 + 셰도우맵
    LEVEL_4,  // + 볼류메트릭 + SSR
    LEVEL_5   // + 레이마칭 + 글로벌 일루미네이션 근사
};
```

**예상 규모:** ~2,500줄

### 2-2. 아티팩트 감지 시스템

**알고리즘:**
1. 기준 프레임 렌더링 (첫 프레임을 reference로 저장)
2. 이후 프레임을 reference와 픽셀 단위 비교
3. 허용 오차 이상의 차이 = 아티팩트 감지
4. VRAM 비트 플립, 셰이더 유닛 에러 구분

```cpp
class ArtifactDetector {
    void set_reference_frame(const uint8_t* pixels, int w, int h);
    ArtifactResult compare_frame(const uint8_t* pixels);
    // → 에러 픽셀 수, 위치, 심각도 반환
};
```

**예상 규모:** ~400줄

### 2-3. Adaptive 부하 테스트

```cpp
enum class AdaptiveMode {
    VARIABLE,  // 점진적 증가 (+5% / 20초)
    SWITCH     // 급격한 스파이크 (20%↔80% 교차)
};
```

- Variable: GPU가 피크 클럭에 도달하는 40~60% 부하 대역에서 불안정성 노출
- Switch: 전력 공급 급변으로 VRM 안정성 테스트

**예상 규모:** ~300줄

### 2-4. 멀티 GPU 동시 테스트

```cpp
class MultiGpuManager {
    std::vector<std::unique_ptr<GpuEngine>> gpu_engines_;
    void start_all(GpuStressMode mode);  // 모든 GPU 동시 시작
    void stop_all();
    std::vector<GpuMetrics> get_all_metrics();
};
```

**예상 규모:** ~200줄

---

## Phase 3: 센서 모니터링 고도화

### 3-1. LibreHardwareMonitor 통합 (Windows)

**현재**: 자체 WMI 구현 (기본 센서만)
**목표**: LHM 수준의 200+ 센서 커버리지

```
신규 파일:
  src/monitor/lhm_bridge.h/cpp  - LibreHardwareMonitor DLL 브릿지
```

**접근 방식:**
- LibreHardwareMonitorLib.dll을 동적 로딩 (LoadLibrary)
- COM Interop으로 .NET 객체 접근
- 또는: LHM CLI 서브프로세스 (lhm-cli --json) → JSON 파싱
- 폴백: 현재 WMI 구현 유지

**추가 센서:**
- VRM 온도, 칩셋 온도
- 모든 전압 (12V, 5V, 3.3V, Vcore, VDIMM)
- 팬 RPM (모든 팬 헤더)
- 메모리 클럭/타이밍
- 디스크 SMART 데이터

**예상 규모:** ~800줄

### 3-2. 센서 데이터 모델 개선

**현재**: 평면 리스트 (SensorReading)
**목표**: 계층적 트리 (Hardware → Sensor → Reading)

```cpp
struct HardwareNode {
    std::string name;           // "Intel Core i9-14900K"
    std::string type;           // "CPU", "GPU", "Motherboard"
    std::vector<SensorGroup> groups;  // "Temperatures", "Voltages", "Clocks"
};

struct SensorGroup {
    std::string name;
    std::vector<SensorReading> readings;
};
```

**예상 규모:** ~300줄

### 3-3. WHEA 에러 모니터링 (Windows)

```
신규 파일:
  src/monitor/whea_monitor.h/cpp
```

- Windows Event Log 구독 (WHEA-Logger)
- Machine Check Exception (MCE) 감지
- 드라이버 레벨 하드웨어 에러 리포팅
- 자동 중지 옵션

**예상 규모:** ~400줄

---

## Phase 4: PSU 복합 부하 + 캐시 벤치마크

### 4-1. PSU 복합 부하 테스트

```
신규 파일:
  src/engines/psu_engine.h/cpp
```

```cpp
class PsuEngine : public IEngine {
    // CPU Linpack + GPU 3D를 동시 실행
    CpuEngine cpu_;
    GpuEngine gpu_;

    enum class PsuLoadPattern {
        STEADY,    // 최대 부하 지속
        SPIKE,     // 급격한 부하 변동 (PSU 응답 테스트)
        RAMP       // 점진적 증가
    };

    void start(PsuLoadPattern pattern);
    // 전원 공급 실패 감지: 갑자기 정지 → PSU 보호 모드 진입
};
```

**예상 규모:** ~400줄

### 4-2. 캐시/메모리 레이턴시 벤치마크

```
신규 파일:
  src/engines/benchmark/cache_benchmark.h/cpp
  src/engines/benchmark/memory_benchmark.h/cpp
```

**Pointer Chasing 레이턴시 측정:**
```cpp
struct CacheLatencyResult {
    double l1_latency_ns;    // ~1ns
    double l2_latency_ns;    // ~4ns
    double l3_latency_ns;    // ~10ns
    double dram_latency_ns;  // ~60-100ns

    double l1_bandwidth_gbs; // Read/Write 분리
    double l2_bandwidth_gbs;
    double l3_bandwidth_gbs;
    double dram_bandwidth_gbs;
};

class CacheBenchmark {
    // 버퍼 크기별 pointer chasing → 각 캐시 레벨 레이턴시 측정
    // 32KB(L1) → 256KB(L2) → 8MB(L3) → 64MB+(DRAM)
    CacheLatencyResult run();
};
```

**알고리즘:**
1. N개 노드의 링크드 리스트를 랜덤 순서로 생성
2. 포인터 따라가기 (p = p->next) 시간 측정
3. 버퍼 크기에 따라 특정 캐시 레벨에 fit → 해당 레벨 레이턴시
4. 대역폭: memcpy/streaming store로 측정

**예상 규모:** ~500줄

---

## Phase 5: 테스트 스케줄링 + 인증서

### 5-1. 테스트 스케줄러

```
신규 파일:
  src/scheduler/test_scheduler.h/cpp
  src/scheduler/test_step.h
  src/scheduler/preset_schedules.h/cpp
```

```cpp
struct TestStep {
    std::string engine;      // "cpu", "gpu", "ram", "storage", "psu"
    QVariantMap settings;    // 엔진별 설정
    int duration_secs;
    bool parallel;           // true면 다음 스텝과 동시 실행
};

class TestScheduler : public QObject {
    Q_OBJECT
    void load_schedule(const std::vector<TestStep>& steps);
    void start();
    void stop();

signals:
    void step_started(int index, const TestStep& step);
    void step_completed(int index, bool passed);
    void schedule_completed(bool all_passed);
    void progress_changed(double pct);
};
```

**프리셋 스케줄:**
- Quick Check (5분): CPU AVX2 3분 + RAM 2분
- Standard (30분): CPU 10분 → GPU 10분 → RAM 10분
- Extreme (1시간): CPU+RAM 동시 20분 → GPU 20분 → PSU 20분
- OC Validation (2시간): CPU Linpack 60분 → RAM AVX 60분

**예상 규모:** ~600줄

### 5-2. 안정성 인증서

```
신규 파일:
  src/certification/certificate.h/cpp
  src/certification/cert_generator.h/cpp
```

```cpp
enum class CertTier {
    BRONZE,    // ~1시간: Quick CPU + RAM
    SILVER,    // ~3시간: CPU + GPU + RAM 순차
    GOLD,      // ~6시간: 전체 엔진 + Linpack + VRAM
    PLATINUM   // ~12시간: 극한 스트레스 장시간
};

struct Certificate {
    CertTier tier;
    std::string system_info;     // CPU, GPU, RAM, OS
    std::vector<TestResult> results;
    bool passed;                 // 에러 0건이어야 pass
    std::string hash;            // SHA256 위변조 방지
    std::chrono::time_point issued_at;
};

class CertGenerator {
    // HTML 인증서 생성
    QString generate_html(const Certificate& cert);
    // PNG 요약 이미지 생성 (QPainter)
    QImage generate_image(const Certificate& cert);
};
```

**예상 규모:** ~700줄

---

## Phase 6: 리포트 + CLI + GUI 개선

### 6-1. 리포트 생성기

```
신규 파일:
  src/report/report_generator.h/cpp
  src/report/png_report.h/cpp
  src/report/html_report.h/cpp
  src/report/csv_exporter.h/cpp
```

- **PNG**: QPainter로 요약 이미지 (시스템 정보 + 테스트 결과 + 그래프 미니맵)
- **HTML**: 오프라인 단일 파일 (Chart.js 인라인, 시스템 정보, 센서 데이터 그래프)
- **CSV**: 센서 시계열 데이터 (timestamp, sensor_name, value)

**예상 규모:** ~800줄

### 6-2. CLI 모드

```
신규 파일:
  src/cli/cli_runner.h/cpp
  src/cli/cli_args.h/cpp
```

```bash
# 사용 예시
occt_native --cli --test cpu --mode avx2 --threads auto --duration 3600
occt_native --cli --test ram --pattern march --memory 90 --passes 3
occt_native --cli --schedule standard --report html --output ./results/
occt_native --cli --monitor-only --csv-output sensors.csv
```

**구조:**
```cpp
class CliRunner {
    int run(int argc, char** argv);
    // GUI 없이 엔진 직접 실행
    // JSON stdout 출력 (progress, metrics, errors)
    // 종료 코드: 0=pass, 1=fail, 2=error
};
```

**수정 파일:**
- `src/main.cpp` - `--cli` 플래그 감지 시 GUI 건너뛰기

**예상 규모:** ~500줄

### 6-3. GUI 개선

```
신규/수정 파일:
  src/gui/panels/schedule_panel.h/cpp     - 스케줄 빌더 UI
  src/gui/panels/certificate_panel.h/cpp  - 인증서 관리 UI
  src/gui/panels/sysinfo_panel.h/cpp      - 상세 시스템 정보
  src/gui/panels/benchmark_panel.h/cpp    - 벤치마크 결과 + 비교
```

- 스케줄 빌더: 드래그&드롭으로 테스트 순서 편집
- 인증서 패널: 4단계 선택 → 실행 → 결과
- 시스템 정보: CPU/GPU/RAM/Storage 상세 스펙
- 벤치마크: 캐시 레이턴시, 메모리 대역폭 결과 시각화
- 모니터링 전용 모드: 테스트 없이 센서만 관찰

**예상 규모:** ~1,500줄

---

## 구현 우선순위 및 규모 요약

| Phase | 핵심 내용 | 신규 파일 | 예상 규모 | 우선순위 |
|-------|----------|----------|----------|---------|
| **Phase 1** | CPU 에러 검증 + Linpack 고도화 | 3개 | ~950줄 | 🔴 최우선 |
| **Phase 2** | Vulkan 3D + 아티팩트 감지 | 8개+ | ~3,400줄 | 🔴 핵심 |
| **Phase 3** | 센서 고도화 + WHEA | 4개 | ~1,500줄 | 🟡 중요 |
| **Phase 4** | PSU 복합 + 캐시 벤치마크 | 4개 | ~900줄 | 🟡 중요 |
| **Phase 5** | 스케줄링 + 인증서 | 5개 | ~1,300줄 | 🟠 필요 |
| **Phase 6** | 리포트 + CLI + GUI | 8개+ | ~2,800줄 | 🟠 필요 |
| **합계** | | ~32개+ | ~10,850줄 | |

현재 코드베이스: ~9,300줄
개선 후 예상: ~20,000줄+

---

## 의존성 추가 목록

| 라이브러리 | 용도 | 필수여부 |
|-----------|------|---------|
| Vulkan SDK | 3D 렌더링 엔진 | Phase 2 필수 |
| OpenBLAS | Linpack DGEMM | Phase 1 선택 (폴백 있음) |
| glslang/shaderc | GLSL→SPIR-V 컴파일 | Phase 2 필수 |
| LibreHardwareMonitor | Windows 센서 | Phase 3 선택 (폴백 있음) |

---

## 비고

- 각 Phase는 독립적으로 구현 가능 (Phase 1부터 순차 권장)
- Phase 1만 완료해도 "에러 감지" 핵심 가치 확보
- Phase 2까지 완료하면 실제 OCCT와 경쟁 가능한 수준
- Phase 3~6은 사용자 경험 및 프로페셔널 기능
