# ─── Stress Test Engines Library ─────────────────────────────────────────────
add_library(occt_engines STATIC
    # CPU 엔진
    cpu_engine.cpp
    cpu_engine.h
    cpu/avx_stress.cpp
    cpu/avx_stress.h
    cpu/error_verifier.cpp
    cpu/error_verifier.h
    cpu/linpack.cpp
    cpu/linpack.h
    cpu/prime.cpp
    cpu/prime.h

    # GPU 엔진
    gpu_engine.cpp
    gpu_engine.h
    gpu/opencl_backend.cpp
    gpu/opencl_backend.h
    gpu/vulkan_backend.cpp
    gpu/vulkan_backend.h
    gpu/vulkan_renderer.cpp
    gpu/vulkan_renderer.h
    gpu/artifact_detector.cpp
    gpu/artifact_detector.h
    gpu/multi_gpu_manager.cpp
    gpu/multi_gpu_manager.h
    gpu/shaders/stress_shaders.h

    # RAM 엔진
    ram_engine.cpp
    ram_engine.h

    # Storage 엔진
    storage_engine.cpp
    storage_engine.h
    storage/verify_block.h
    storage/storage_verify.cpp

    # PSU 엔진
    psu_engine.cpp
    psu_engine.h

    # 벤치마크
    benchmark/cache_benchmark.cpp
    benchmark/cache_benchmark.h
    benchmark/memory_benchmark.cpp
    benchmark/memory_benchmark.h

    # 공통 인터페이스
    base_engine.h
)

target_include_directories(occt_engines PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_engines PUBLIC
    occt_utils
    Threads::Threads
)

# AVX2/FMA 컴파일 플래그 (avx_stress.cpp에 적용) - x86 전용
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    if(NOT MSVC)
        # GCC/Clang: enable AVX2 + FMA target for the stress file
        set_source_files_properties(cpu/avx_stress.cpp
            PROPERTIES COMPILE_FLAGS "-mavx2 -mfma"
        )
    else()
        set_source_files_properties(cpu/avx_stress.cpp
            PROPERTIES COMPILE_FLAGS "/arch:AVX2"
        )
    endif()
endif()

# OpenCL 지원
if(OCCT_HAS_OPENCL)
    target_link_libraries(occt_engines PUBLIC OpenCL::OpenCL)
    target_compile_definitions(occt_engines PUBLIC OCCT_HAS_OPENCL)
endif()

# Vulkan 지원
if(OCCT_HAS_VULKAN)
    target_link_libraries(occt_engines PUBLIC Vulkan::Vulkan)
    target_compile_definitions(occt_engines PUBLIC OCCT_HAS_VULKAN)

    # GLSL -> SPIR-V shader compilation
    find_program(GLSLC glslc HINTS "$ENV{VULKAN_SDK}/bin")
    find_program(GLSLANG_VALIDATOR glslangValidator HINTS "$ENV{VULKAN_SDK}/bin")

    set(SHADER_COMPILER "")
    if(GLSLC)
        set(SHADER_COMPILER ${GLSLC})
        message(STATUS "Using glslc for shader compilation: ${GLSLC}")
    elseif(GLSLANG_VALIDATOR)
        set(SHADER_COMPILER ${GLSLANG_VALIDATOR})
        message(STATUS "Using glslangValidator for shader compilation: ${GLSLANG_VALIDATOR}")
    else()
        message(WARNING "No SPIR-V compiler found (glslc or glslangValidator). "
                        "Vulkan shaders will need runtime compilation.")
    endif()

    # Define output directory for compiled shaders
    set(SPIRV_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
    file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})
    target_include_directories(occt_engines PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
endif()

# OpenBLAS 지원
if(OCCT_HAS_OPENBLAS)
    target_compile_definitions(occt_engines PRIVATE OCCT_HAS_OPENBLAS)
    target_link_libraries(occt_engines PRIVATE ${OPENBLAS_LIBRARIES})
    target_include_directories(occt_engines PRIVATE ${OPENBLAS_INCLUDE_DIRS})
endif()
