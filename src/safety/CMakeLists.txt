# ─── Safety System Library ───────────────────────────────────────────────────
add_library(occt_safety STATIC
    guardian.h
    guardian.cpp
)

target_include_directories(occt_safety PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_safety PUBLIC
    Qt6::Core
    Threads::Threads
    occt_monitor
    occt_engines
)
