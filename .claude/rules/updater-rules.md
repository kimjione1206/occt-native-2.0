# 업데이터 수정 규칙 (2.0 전용)

- GitHub API 호출은 QNetworkAccessManager 사용 (Qt6::Network)
- SHA256 검증 필수 (QCryptographicHash::Sha256)
- Windows 파일 교체: bat 스크립트로 프로세스 종료 대기 → 파일 복사 → 재시작
- macOS/Linux: 직접 파일 복사 (실행 중 바이너리 교체 가능)
- UpdateDialog: 사용자에게 모든 단계가 보여야 함 (다운로드 → 검증 → 설치 → 재시작)
- LogUploader: Secret Gist 사용 (public: false), 토큰 없으면 graceful skip
- PostUpdateRunner: GUI 모드에서만 동작, QTimer::singleShot 폴링
- 로그 전송 트리거 B: 5개 패널 모두 `testStopRequested()` → MainWindow::onTestStopped() 연결 필수
