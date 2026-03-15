# ─── Test Scheduler Library ──────────────────────────────────────────────────
add_library(occt_scheduler STATIC
    test_step.h
    test_scheduler.h
    test_scheduler.cpp
    preset_schedules.h
    preset_schedules.cpp
)

target_include_directories(occt_scheduler PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_scheduler PUBLIC
    Qt6::Core
    occt_engines
)
