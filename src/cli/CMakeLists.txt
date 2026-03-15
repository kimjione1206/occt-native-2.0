# ─── CLI Mode Library ────────────────────────────────────────────────────────
add_library(occt_cli STATIC
    cli_args.h
    cli_args.cpp
    cli_runner.h
    cli_runner.cpp
)

target_include_directories(occt_cli PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_cli PUBLIC
    Qt6::Core
    occt_engines
    occt_monitor
    occt_report
    occt_utils
    occt_scheduler
    occt_certification
    occt_safety
    occt_api
    occt_leaderboard
    Threads::Threads
)
