cmake_minimum_required(VERSION 3.21)

# ─── 프로젝트 정의 ───────────────────────────────────────────────────────────
set(OCCT_VERSION_MAJOR 1)
set(OCCT_VERSION_MINOR 7)
set(OCCT_VERSION_PATCH 4)
set(OCCT_VERSION_STRING "${OCCT_VERSION_MAJOR}.${OCCT_VERSION_MINOR}.${OCCT_VERSION_PATCH}")

project(OCCTNativeStressTest
    VERSION ${OCCT_VERSION_STRING}
    DESCRIPTION "OCCT-style Hardware Stress Test Tool"
    LANGUAGES CXX
)

# ─── C++ 표준 ────────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 자동 빌드 도구
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ─── 빌드 옵션 ───────────────────────────────────────────────────────────────
option(OCCT_ENABLE_AVX512  "Enable AVX-512 instruction set support" OFF)
option(OCCT_ENABLE_OPENCL  "Enable OpenCL GPU stress testing"       OFF)
option(OCCT_ENABLE_VULKAN  "Enable Vulkan compute stress testing"   OFF)
option(OCCT_PORTABLE       "Build portable version (static CRT, no installer)" ON)

# ─── 포터블 빌드: 정적 CRT 링크 (MSVC) ──────────────────────────────────────
if(MSVC AND OCCT_PORTABLE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    message(STATUS "Portable build: static CRT enabled")
endif()

# ─── 컴파일러별 플래그 ──────────────────────────────────────────────────────
if(MSVC)
    # 공통 MSVC 플래그
    add_compile_options(
        /W4         # 높은 경고 수준
        /MP         # 병렬 빌드
        /utf-8      # UTF-8 소스 인코딩
        /permissive- # 엄격한 표준 준수
    )

    # AVX2 (MSVC는 기본적으로 AVX2를 자동 감지하지 않음)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)
    if(COMPILER_SUPPORTS_AVX2)
        add_compile_options(/arch:AVX2)
        set(OCCT_HAS_AVX2 TRUE)
        message(STATUS "AVX2 support: enabled (MSVC)")
    endif()

    # AVX-512 (옵션이 켜져 있을 때만)
    if(OCCT_ENABLE_AVX512)
        check_cxx_compiler_flag("/arch:AVX512" COMPILER_SUPPORTS_AVX512)
        if(COMPILER_SUPPORTS_AVX512)
            add_compile_options(/arch:AVX512)
            set(OCCT_HAS_AVX512 TRUE)
            message(STATUS "AVX-512 support: enabled (MSVC)")
        else()
            message(WARNING "AVX-512 requested but not supported by compiler")
        endif()
    endif()

    # Release 최적화
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot /GL")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # 공통 GCC/Clang 플래그
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )

    # AVX2 감지
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-mavx2" COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-mfma" COMPILER_SUPPORTS_FMA)
    if(COMPILER_SUPPORTS_AVX2 AND COMPILER_SUPPORTS_FMA)
        add_compile_options(-mavx2 -mfma)
        set(OCCT_HAS_AVX2 TRUE)
        message(STATUS "AVX2+FMA support: enabled (GCC/Clang)")
    endif()

    # AVX-512 (옵션이 켜져 있을 때만)
    if(OCCT_ENABLE_AVX512)
        check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512F)
        check_cxx_compiler_flag("-mavx512dq" COMPILER_SUPPORTS_AVX512DQ)
        if(COMPILER_SUPPORTS_AVX512F AND COMPILER_SUPPORTS_AVX512DQ)
            add_compile_options(-mavx512f -mavx512dq)
            set(OCCT_HAS_AVX512 TRUE)
            message(STATUS "AVX-512 support: enabled (GCC/Clang)")
        else()
            message(WARNING "AVX-512 requested but not supported by compiler")
        endif()
    endif()

    # Release 최적화
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -flto")
endif()

# ─── 의존성 찾기 ─────────────────────────────────────────────────────────────
# Qt6 (필수)
find_package(Qt6 REQUIRED COMPONENTS Widgets Charts Core Gui Network)
message(STATUS "Qt6 found: ${Qt6_VERSION}")

# OpenCL (선택적)
if(OCCT_ENABLE_OPENCL)
    find_package(OpenCL)
    if(OpenCL_FOUND)
        set(OCCT_HAS_OPENCL TRUE)
        message(STATUS "OpenCL found: ${OpenCL_VERSION_STRING}")
    else()
        message(WARNING "OpenCL requested but not found")
        set(OCCT_ENABLE_OPENCL OFF)
    endif()
endif()

# Vulkan (선택적)
if(OCCT_ENABLE_VULKAN)
    find_package(Vulkan)
    if(Vulkan_FOUND)
        set(OCCT_HAS_VULKAN TRUE)
        message(STATUS "Vulkan found: ${Vulkan_VERSION}")
    else()
        message(WARNING "Vulkan requested but not found")
        set(OCCT_ENABLE_VULKAN OFF)
    endif()
endif()

# 스레딩 (필수)
find_package(Threads REQUIRED)

# ─── config.h 생성 ───────────────────────────────────────────────────────────
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY
)

# ─── 서브디렉토리 ────────────────────────────────────────────────────────────
add_subdirectory(src/engines)
add_subdirectory(src/gui)
add_subdirectory(src/monitor)
add_subdirectory(src/safety)
add_subdirectory(src/utils)
add_subdirectory(src/scheduler)
add_subdirectory(src/certification)
add_subdirectory(src/report)
add_subdirectory(src/api)
add_subdirectory(src/benchmark)
add_subdirectory(src/cli)
add_subdirectory(src/updater)

# ─── 테스트 ───────────────────────────────────────────────────────────────
option(BUILD_TESTS "Build test executables" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ─── Qt 리소스 ───────────────────────────────────────────────────────────────
qt6_add_resources(OCCT_RESOURCES resources/occt_native.qrc)

# ─── Windows 매니페스트 (UAC / DPI) ──────────────────────────────────────────
if(WIN32)
    set(OCCT_WIN_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/occt_native.manifest")
endif()

# ─── 메인 실행 파일 ──────────────────────────────────────────────────────────
add_executable(occt_native
    src/main.cpp
    ${OCCT_RESOURCES}
    $<$<PLATFORM_ID:Windows>:${OCCT_WIN_MANIFEST}>
)

# Windows: WIN32_EXECUTABLE로 콘솔 창 숨기기 (Release)
# CI/테스트 시 -DOCCT_CONSOLE=ON으로 콘솔 서브시스템 강제 (PowerShell 동기 실행 필요)
option(OCCT_CONSOLE "Force console subsystem for CI/CLI testing" OFF)
if(NOT OCCT_CONSOLE)
    set_target_properties(occt_native PROPERTIES
        WIN32_EXECUTABLE $<AND:$<PLATFORM_ID:Windows>,$<CONFIG:Release>>
    )
endif()

target_include_directories(occt_native PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

target_link_libraries(occt_native PRIVATE
    Qt6::Widgets
    Qt6::Charts
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    occt_gui
    occt_safety
    occt_cli
    occt_updater
)

if(OCCT_HAS_OPENCL)
    target_link_libraries(occt_native PRIVATE OpenCL::OpenCL)
endif()

if(OCCT_HAS_VULKAN)
    target_link_libraries(occt_native PRIVATE Vulkan::Vulkan)
endif()

# ─── 설치 규칙 ───────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS occt_native
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Windows DLL 복사 (Release 빌드에서만 windeployqt 실행)
if(WIN32)
    find_program(WINDEPLOYQT windeployqt HINTS "${Qt6_DIR}/../../../bin")
    if(WINDEPLOYQT)
        add_custom_command(TARGET occt_native POST_BUILD
            COMMAND "$<$<CONFIG:Release>:${WINDEPLOYQT}>"
                    "$<$<CONFIG:Release>:--release>"
                    "$<$<CONFIG:Release>:--no-translations>"
                    "$<$<CONFIG:Release>:--no-system-d3d-compiler>"
                    "$<$<CONFIG:Release>:--no-opengl-sw>"
                    "$<$<CONFIG:Release>:$<TARGET_FILE:occt_native>>"
            COMMAND_EXPAND_LISTS
            COMMENT "Running windeployqt (Release only)..."
        )
    endif()
endif()

# ─── CPack 포터블 ZIP 패키징 ─────────────────────────────────────────────────
if(WIN32 AND OCCT_PORTABLE)
    # 실행 파일 설치
    install(TARGETS occt_native
        RUNTIME DESTINATION .
    )

    # windeployqt가 출력하는 Qt DLL들을 함께 패키징
    install(DIRECTORY "$<TARGET_FILE_DIR:occt_native>/"
        DESTINATION .
        FILES_MATCHING
        PATTERN "*.dll"
        PATTERN "*.qm" EXCLUDE
    )

    # Qt 플러그인 디렉토리 포함
    install(DIRECTORY "$<TARGET_FILE_DIR:occt_native>/platforms"
        DESTINATION .
        OPTIONAL
    )
    install(DIRECTORY "$<TARGET_FILE_DIR:occt_native>/styles"
        DESTINATION .
        OPTIONAL
    )
    install(DIRECTORY "$<TARGET_FILE_DIR:occt_native>/imageformats"
        DESTINATION .
        OPTIONAL
    )

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_NAME "OCCTNativeStressTest")
    set(CPACK_PACKAGE_VERSION "${OCCT_VERSION_STRING}")
    set(CPACK_PACKAGE_FILE_NAME "OCCTNativeStressTest-${OCCT_VERSION_STRING}-win64-portable")
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    include(CPack)
endif()

# --- LHM Sensor Reader Helper (Windows only) ---
if(WIN32)
    option(OCCT_BUILD_LHM_HELPER "Build lhm-sensor-reader helper (requires .NET 8 SDK)" ON)
    if(OCCT_BUILD_LHM_HELPER)
        find_program(DOTNET_EXE dotnet)
        if(DOTNET_EXE)
            set(LHM_PROJECT "${CMAKE_SOURCE_DIR}/tools/lhm-sensor-reader/lhm-sensor-reader.csproj")
            set(LHM_OUTPUT_DIR "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/tools/lhm")
            add_custom_target(lhm_sensor_reader ALL
                COMMAND ${DOTNET_EXE} publish "${LHM_PROJECT}"
                    -c Release -r win-x64 --self-contained true
                    -p:PublishTrimmed=true -p:TrimMode=partial
                    -o "${LHM_OUTPUT_DIR}"
                COMMENT "Building lhm-sensor-reader helper (folder deploy)..."
                VERBATIM
            )
        else()
            message(WARNING "dotnet SDK not found — lhm-sensor-reader will not be built. CPU temperature may show N/A.")
        endif()
    endif()
endif()

# ─── 빌드 요약 ───────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "╔══════════════════════════════════════════════╗")
message(STATUS "║  OCCT Native Stress Test v${OCCT_VERSION_STRING}              ║")
message(STATUS "╠══════════════════════════════════════════════╣")
message(STATUS "║  Compiler:    ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "║  Build type:  ${CMAKE_BUILD_TYPE}")
message(STATUS "║  AVX2:        ${OCCT_HAS_AVX2}")
message(STATUS "║  AVX-512:     ${OCCT_HAS_AVX512}")
message(STATUS "║  OpenCL:      ${OCCT_HAS_OPENCL}")
message(STATUS "║  Vulkan:      ${OCCT_HAS_VULKAN}")
message(STATUS "║  Qt6:         ${Qt6_VERSION}")
message(STATUS "║  Portable:    ${OCCT_PORTABLE}")
message(STATUS "╚══════════════════════════════════════════════╝")
message(STATUS "")
