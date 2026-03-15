# ─── Utilities Library ───────────────────────────────────────────────────────
add_library(occt_utils STATIC
    cpuid.cpp
    cpuid.h
    gpu_info.cpp
    gpu_info.h
    portable_paths.cpp
    portable_paths.h
    app_config.cpp
    app_config.h
    file_logger.cpp
    file_logger.h
    crc32.cpp
    crc32.h
    secure_token_store.cpp
    secure_token_store.h
    # timer.cpp
    # timer.h
    # system_info.cpp
    # system_info.h
    # report_generator.cpp
    # report_generator.h
)

target_include_directories(occt_utils PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_utils PUBLIC
    Qt6::Core
    Threads::Threads
)

# macOS sysctl for CPU detection + Keychain for secure token storage
if(APPLE)
    find_library(SECURITY_FRAMEWORK Security)
    if(SECURITY_FRAMEWORK)
        target_link_libraries(occt_utils PUBLIC ${SECURITY_FRAMEWORK})
    endif()
endif()

# Windows DPAPI for secure token storage
if(WIN32)
    target_link_libraries(occt_utils PUBLIC crypt32)
endif()
