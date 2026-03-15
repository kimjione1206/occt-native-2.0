# ─── Report Generation Library ───────────────────────────────────────────────
add_library(occt_report STATIC
    png_report.h
    png_report.cpp
    html_report.h
    html_report.cpp
    csv_exporter.h
    csv_exporter.cpp
    report_manager.h
    report_manager.cpp
    report_comparator.h
    report_comparator.cpp
)

target_include_directories(occt_report PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_report PUBLIC
    Qt6::Core
    Qt6::Gui
)
