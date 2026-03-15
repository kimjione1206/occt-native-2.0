# ─── GUI Library ─────────────────────────────────────────────────────────────
add_library(occt_gui STATIC
    main_window.h
    main_window.cpp

    # Panels
    panels/panel_styles.h
    panels/dashboard_panel.h
    panels/dashboard_panel.cpp
    panels/cpu_panel.h
    panels/cpu_panel.cpp
    panels/gpu_panel.h
    panels/gpu_panel.cpp
    panels/ram_panel.h
    panels/ram_panel.cpp
    panels/storage_panel.h
    panels/storage_panel.cpp
    panels/monitor_panel.h
    panels/monitor_panel.cpp
    panels/results_panel.h
    panels/results_panel.cpp
    panels/psu_panel.h
    panels/psu_panel.cpp
    panels/benchmark_panel.h
    panels/benchmark_panel.cpp
    panels/sysinfo_panel.h
    panels/sysinfo_panel.cpp
    panels/schedule_panel.h
    panels/schedule_panel.cpp
    panels/certificate_panel.h
    panels/certificate_panel.cpp

    # Dialogs
    dialogs/token_dialog.h
    dialogs/token_dialog.cpp

    # Custom widgets
    widgets/circular_gauge.h
    widgets/circular_gauge.cpp
    widgets/realtime_chart.h
    widgets/realtime_chart.cpp
)

target_include_directories(occt_gui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
    "${CMAKE_CURRENT_BINARY_DIR}/../../"
)

target_link_libraries(occt_gui PUBLIC
    Qt6::Widgets
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    occt_monitor
    occt_scheduler
    occt_certification
    occt_report
    occt_updater
)
