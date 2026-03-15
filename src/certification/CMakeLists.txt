# ─── Certification Library ───────────────────────────────────────────────────
add_library(occt_certification STATIC
    certificate.h
    cert_generator.h
    cert_generator.cpp
)

target_include_directories(occt_certification PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_certification PUBLIC
    Qt6::Core
    Qt6::Gui
)
