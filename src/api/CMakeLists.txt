# ─── Certificate Store API Library ────────────────────────────────────────────
add_library(occt_api STATIC
    cert_store.h
    cert_store.cpp
)

target_include_directories(occt_api PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_api PUBLIC
    Qt6::Core
)
