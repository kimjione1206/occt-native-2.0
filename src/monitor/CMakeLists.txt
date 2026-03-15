# ─── Hardware Monitor Library ────────────────────────────────────────────────
add_library(occt_monitor STATIC
    sensor_manager.h
    sensor_manager.cpp
    sensor_model.h
    sensor_model.cpp
    lhm_bridge.h
    lhm_bridge.cpp
    whea_monitor.h
    whea_monitor.cpp
    system_info.h
    system_info.cpp
)

target_include_directories(occt_monitor PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_monitor PUBLIC
    Qt6::Core
    Threads::Threads
)

# Windows: WMI / PDH / WEVTAPI 라이브러리
if(WIN32)
    target_link_libraries(occt_monitor PUBLIC
        pdh
        wbemuuid
        ole32
        oleaut32
        wevtapi
    )
endif()

# macOS: IOKit framework
if(APPLE)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    if(IOKIT_FRAMEWORK AND COREFOUNDATION_FRAMEWORK)
        target_link_libraries(occt_monitor PUBLIC
            ${IOKIT_FRAMEWORK}
            ${COREFOUNDATION_FRAMEWORK}
        )
    endif()
endif()

# Linux: dl for dynamic loading (NVML/ADL)
if(UNIX AND NOT APPLE)
    target_link_libraries(occt_monitor PUBLIC dl)
endif()
