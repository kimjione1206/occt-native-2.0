# ─── Benchmark Leaderboard Library ────────────────────────────────────────────
add_library(occt_leaderboard STATIC
    leaderboard.h
    leaderboard.cpp
)

target_include_directories(occt_leaderboard PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_leaderboard PUBLIC
    Qt6::Core
)
