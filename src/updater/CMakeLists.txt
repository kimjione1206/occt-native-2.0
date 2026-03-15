# ─── Auto-Updater Library ────────────────────────────────────────────────────
add_library(occt_updater STATIC
    update_checker.h update_checker.cpp
    update_downloader.h update_downloader.cpp
    update_dialog.h update_dialog.cpp
    update_installer.h update_installer.cpp
    log_uploader.h log_uploader.cpp
    post_update_runner.h post_update_runner.cpp
)

target_include_directories(occt_updater PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_updater PUBLIC
    Qt6::Core Qt6::Widgets Qt6::Network
    occt_utils occt_report occt_engines
)
