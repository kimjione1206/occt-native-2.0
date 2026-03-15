# OCCT-Native GPU/Storage UI 먹통 + 모니터링 불가 수정 계획서 V2

## 진단 요약

Windows 실환경에서 GPU/Storage 테스트의 UI가 먹히지 않거나 모니터링이 안 되는 근본 원인 분석 결과:

---

## 발견된 문제 (총 12건, 심각도순)

### CRITICAL (4건)

#### C-1. GPU start() 무응답 — 에러 피드백 없음
- **파일**: `gpu_engine.cpp:1073-1110`, `gpu_panel.cpp:310`
- **증상**: Start 버튼 클릭 → 아무 반응 없음 (버튼 깜빡 후 원복)
- **원인**:
  - OpenCL/Vulkan 미설치 시 `start()`가 `running_`을 true로 설정하지 않고 즉시 return
  - `start()`는 void 반환 → 패널이 에러 여부를 알 수 없음
  - `isRunning_ = true` 설정 후 `engine_->start()` 호출 → 첫 `updateMonitoring()`에서 `is_running() == false` 감지 → 즉시 리셋
  - **사용자에게 에러 메시지가 전혀 표시되지 않음**

#### C-2. GPU initialize() 반환값 무시
- **파일**: `gpu_panel.cpp:15`
- **증상**: GPU 백엔드 초기화 실패 시에도 Start 버튼 활성화
- **원인**: `engine_->initialize()` 결과(bool)를 체크하지 않음
- **결과**: 초기화 실패해도 사용자가 Start 클릭 가능 → C-1 발생

#### C-3. Storage stop()이 UI 스레드 블로킹
- **파일**: `storage_engine.cpp:83-100`
- **증상**: Stop 버튼 클릭 후 UI 5~30초 멈춤
- **원인**:
  1. `worker_.join()` — 진행 중인 I/O 완료까지 대기
  2. `DeleteFileA()` — 안티바이러스/인덱서가 파일 잠금 시 블로킹
  - 둘 다 메인(Qt) 스레드에서 동기 실행됨

#### C-4. Storage 읽기 테스트 전 파일생성이 UI 블로킹
- **파일**: `storage_engine.cpp:243-268`
- **증상**: Read/Mixed 테스트 시작 시 UI 20초+ 멈춤 (대용량 파일 생성 중)
- **원인**: `start()` 내부에서 worker 스레드가 전체 파일을 먼저 생성
  - 이 동안 `metrics_`는 0으로 유지되어 UI에 아무것도 표시 안됨
  - 진행률 표시 없음 — 사용자는 프로그램이 멈춘 것으로 인식

### HIGH (4건)

#### H-1. GpuEngine이 IEngine 인터페이스 미상속
- **파일**: `gpu_engine.h:53-90` vs `base_engine.h`
- **문제**: CPU/RAM/Storage는 모두 IEngine 상속, GPU만 독립 클래스
- **영향**: 패널 코드의 일관성 깨짐, `start()` 반환 타입/시그니처 불일치

#### H-2. Storage WriteFile() 에러 미체크 (Windows)
- **파일**: `storage_engine.cpp:260`, `storage_engine.cpp:314`
- **문제**: Windows `WriteFile()` 반환값 체크 없음 → 디스크 풀/권한 에러 무시
- **비교**: Linux는 `ret <= 0` 체크 후 break

#### H-3. Metrics 콜백이 뮤텍스 보유 상태에서 호출
- **파일**: `gpu_engine.cpp:938`, `gpu_engine.cpp:958`
- **문제**: `update_metrics()`가 `metrics_mutex` lock 상태에서 콜백 호출
- **영향**: 콜백이 느린 I/O 수행 시 worker 스레드 전체 블로킹

#### H-4. Metrics 콜백 패턴이 dead code (Storage)
- **파일**: `storage_engine.cpp:115`
- **문제**: `set_metrics_callback()` 정의됨 → 저장됨 → **한 번도 호출되지 않음**
- **영향**: CLI에서 `set_metrics_callback()` 설정해도 실시간 출력 없음

### MEDIUM (4건)

#### M-1. GPU 센서 쿼리가 worker 스레드에서 블로킹 가능
- **파일**: `gpu_engine.cpp:922`, `gpu_info.cpp:291-314`
- **문제**: `gpu_query_sensors()`가 NVML/ADL DLL 호출 — Windows에서 느릴 수 있음

#### M-2. Storage OVERLAPPED 구조체 생성 후 미사용
- **파일**: `storage_engine.cpp:400-406`
- **문제**: `OVERLAPPED` 생성하나 `WriteFile()`에 실제 전달 안함 → 동기 I/O

#### M-3. start() 후 첫 metrics 업데이트까지의 공백
- **파일**: `storage_engine.cpp:80` (running=true), 패널 500ms 폴링
- **문제**: `running_`이 true지만 아직 I/O 시작 전 → 0값 메트릭 표시

#### M-4. 에러 검출/상태 보고의 UI-CLI 불일치
- **문제**: UI는 에러 시 아무 표시 없음, CLI는 stderr 출력만
- **필요**: 통일된 에러/상태 보고 메커니즘

---

## 수정 계획

### Phase A: 에러 피드백 체계 구축 (C-1, C-2, H-1)

#### A-1. `start()` 반환 타입을 bool로 변경
```
파일: gpu_engine.h, gpu_engine.cpp
변경: void start() → bool start()
     - 성공 시 true, 실패 시 false 반환
     - 실패 사유를 last_error_ 멤버에 저장
     - QString last_error() const 추가
```

#### A-2. GpuPanel에 에러 처리 추가
```
파일: gpu_panel.cpp
변경: onStartStopClicked()에서
     bool ok = engine_->start(mode, duration);
     if (!ok) {
         QMessageBox::warning(this, "GPU Test", engine_->last_error());
         isRunning_ = false;
         // 버튼 원복
         return;
     }
```

#### A-3. GpuPanel 생성자에서 initialize() 결과 체크
```
파일: gpu_panel.cpp
변경:
     bool initOk = engine_->initialize();
     if (!initOk) {
         // Start 버튼 비활성화
         // 상태 라벨에 "GPU 백엔드 미감지" 표시
     }
```

#### A-4. StorageEngine::start()도 bool 반환
```
파일: storage_engine.h, storage_engine.cpp
변경: void start() → bool start()
     - 파일 생성 실패 시 false + last_error_
```

**예상 변경량**: ~60줄

---

### Phase B: UI 블로킹 해소 (C-3, C-4)

#### B-1. Storage stop()을 비동기화
```
파일: storage_engine.cpp
변경:
     void stop() {
         stop_requested_.store(true);
         // join()을 별도 스레드에서 수행
         if (worker_.joinable()) {
             std::thread([w = std::move(worker_), path = test_file_path_]() mutable {
                 w.join();
                 // 파일 삭제도 비동기
                 #if defined(_WIN32)
                     DeleteFileA(path.c_str());
                 #else
                     unlink(path.c_str());
                 #endif
             }).detach();
         }
         running_.store(false);
     }
```

#### B-2. Storage 파일 생성을 worker 스레드 내부로 이동 + 진행률
```
파일: storage_engine.cpp
변경:
     run() 메서드에서 파일 생성 → 준비 상태 메트릭 업데이트

     // 파일 생성 중
     metrics_.state = "preparing";  // 새 필드
     metrics_.progress_pct = (written / file_size) * 100;

     // 실제 테스트 시작
     metrics_.state = "testing";
```

#### B-3. StoragePanel에서 준비 상태 표시
```
파일: storage_panel.cpp
변경:
     updateMonitoring()에서:
     if (m.state == "preparing") {
         statusLabel_->setText("테스트 파일 준비 중...");
         progressBar_->setValue(m.progress_pct);
     }
```

**예상 변경량**: ~80줄

---

### Phase C: Metrics 콜백 정상화 (H-3, H-4)

#### C-1. GPU 콜백을 뮤텍스 밖에서 호출
```
파일: gpu_engine.cpp
변경:
     void update_metrics(double gflops, double elapsed) {
         auto sensor = utils::gpu_query_sensors(...);

         GpuMetrics snapshot;
         MetricsCallback cb_copy;
         {
             std::lock_guard<std::mutex> lock(metrics_mutex);
             // ... update latest_metrics ...
             snapshot = latest_metrics;
             cb_copy = metrics_cb;
         }
         // 뮤텍스 해제 후 콜백 호출
         if (cb_copy) cb_copy(snapshot);
     }
```

#### C-2. Storage 콜백 실제 호출 구현
```
파일: storage_engine.cpp
변경:
     seq_write(), seq_read(), rand_*() 등의 메트릭 업데이트 지점에서:

     MetricsCallback cb_copy;
     {
         std::lock_guard<std::mutex> lk(metrics_mutex_);
         metrics_.write_mbs = ...;
         cb_copy = metrics_cb_;
     }
     if (cb_copy) cb_copy(metrics_);
```

**예상 변경량**: ~50줄

---

### Phase D: Windows I/O 안정화 (H-2, M-2)

#### D-1. WriteFile() 에러 체크 추가
```
파일: storage_engine.cpp
변경: 모든 WriteFile()/ReadFile() 호출 후:
     DWORD bytes_written = 0;
     BOOL ok = WriteFile(..., &bytes_written, nullptr);
     if (!ok || bytes_written == 0) {
         DWORD err = GetLastError();
         // 에러 메트릭에 기록
         metrics_.error_count++;
         break;
     }
```

#### D-2. OVERLAPPED 비동기 I/O 제거 (단순화)
```
파일: storage_engine.cpp
변경: rand_write/rand_read에서 OVERLAPPED 구조체 제거
     - SetFilePointer()로 오프셋 이동 후 동기 WriteFile/ReadFile
     - 현재 코드가 이미 동기적이므로, 미사용 OVERLAPPED를 제거하여 명확화
```

**예상 변경량**: ~40줄

---

### Phase E: UI-CLI 일관성 확보 (M-4)

#### E-1. 통일된 상태 enum 추가
```
새 파일: src/engines/engine_status.h

enum class EngineState {
    IDLE,
    INITIALIZING,
    PREPARING,    // Storage 파일 생성 등
    RUNNING,
    STOPPING,
    ERROR,
    COMPLETED
};

struct EngineStatus {
    EngineState state = EngineState::IDLE;
    QString error_message;
    double progress_pct = 0.0;
};
```

#### E-2. 각 엔진에 get_status() 추가
```
파일: gpu_engine.h/cpp, storage_engine.h/cpp
변경: EngineStatus get_status() const;
     - CLI와 UI 모두 동일한 상태 정보 조회
     - 에러 사유도 동일하게 접근
```

#### E-3. CLI에서 상태 기반 로깅
```
파일: cli_runner.cpp
변경: 폴링 루프에서:
     auto status = engine.get_status();
     if (status.state == EngineState::ERROR) {
         emit_json("error", "message", status.error_message);
         return 1;
     }
     if (status.state == EngineState::PREPARING) {
         emit_json("status", "preparing", status.progress_pct);
     }
```

**예상 변경량**: ~80줄

---

## 수정 영향도

| 파일 | Phase A | Phase B | Phase C | Phase D | Phase E |
|------|---------|---------|---------|---------|---------|
| `gpu_engine.h` | start() bool + last_error | - | - | - | get_status() |
| `gpu_engine.cpp` | start() 반환값 | - | 콜백 분리 | - | status 통합 |
| `gpu_panel.cpp` | 에러 처리 + init 체크 | - | - | - | - |
| `storage_engine.h` | start() bool | state 필드 | 콜백 호출 | - | get_status() |
| `storage_engine.cpp` | start() 반환값 | 비동기 stop + 진행률 | 콜백 구현 | WriteFile 체크 | status 통합 |
| `storage_panel.cpp` | 에러 처리 | 준비 상태 표시 | - | - | - |
| `cli_runner.cpp` | 에러 코드 처리 | - | - | - | 상태 기반 로깅 |
| `engine_status.h` (신규) | - | - | - | - | enum + struct |

## 실행 순서 및 의존성

```
Phase A (에러 피드백) ─── 독립, 최우선
  ├── A-1: start() bool 반환 (GPU/Storage)
  ├── A-2: GpuPanel 에러 처리
  ├── A-3: GpuPanel init 체크
  └── A-4: StoragePanel 에러 처리

Phase B (블로킹 해소) ─── 독립
  ├── B-1: Storage stop() 비동기화
  ├── B-2: 파일 생성 진행률
  └── B-3: StoragePanel 준비 상태 표시

Phase C (콜백 정상화) ─── Phase A 이후
  ├── C-1: GPU 콜백 뮤텍스 분리
  └── C-2: Storage 콜백 구현

Phase D (Windows I/O) ─── 독립
  ├── D-1: WriteFile 에러 체크
  └── D-2: OVERLAPPED 단순화

Phase E (일관성) ─── Phase A+B 이후
  ├── E-1: EngineState enum
  ├── E-2: get_status() 구현
  └── E-3: CLI 상태 로깅
```

## 총 예상 변경량
- Phase A: ~60줄
- Phase B: ~80줄
- Phase C: ~50줄
- Phase D: ~40줄
- Phase E: ~80줄
- **합계: ~310줄**
