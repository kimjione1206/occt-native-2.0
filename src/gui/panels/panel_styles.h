#pragma once

/// Shared stylesheet constants used across multiple panels.
/// Centralised here to avoid duplicating identical style strings in every panel.

namespace occt { namespace gui { namespace styles {

// ---- Frame backgrounds ----

/// Dark section frame (settings panels, monitoring panels, tree frames, chart frames).
inline constexpr const char* kSectionFrame =
    "QFrame { background-color: #161B22; border: 1px solid #30363D; border-radius: 8px; }";

/// Darker card frame used for individual metric cards.
inline constexpr const char* kCardFrame =
    "QFrame { background-color: #0D1117; border: 1px solid #30363D; border-radius: 6px; }";

// ---- Button styles ----

/// Green "Start Test" button.
inline constexpr const char* kStartButton =
    "QPushButton { background-color: #27AE60; color: white; border: none; "
    "border-radius: 6px; font-size: 16px; font-weight: bold; }"
    "QPushButton:hover { background-color: #2ECC71; }";

/// Red "Stop Test" button.
inline constexpr const char* kStopButton =
    "QPushButton { background-color: #C0392B; color: white; border: none; "
    "border-radius: 6px; font-size: 16px; font-weight: bold; }"
    "QPushButton:hover { background-color: #E74C3C; }";

/// Orange start button (PSU panel).
inline constexpr const char* kStartButtonOrange =
    "QPushButton { background-color: #E67E22; color: white; border: none; "
    "border-radius: 6px; font-size: 16px; font-weight: bold; }"
    "QPushButton:hover { background-color: #F39C12; }";

// ---- Label styles ----

/// Panel title (18px bold white).
inline constexpr const char* kPanelTitle =
    "color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;";

/// Panel subtitle (12px muted grey).
inline constexpr const char* kPanelSubtitle =
    "color: #8B949E; font-size: 12px; border: none; background: transparent;";

/// Section title inside monitoring area (16px bold white).
inline constexpr const char* kSectionTitle =
    "color: #F0F6FC; font-size: 16px; font-weight: bold; border: none; background: transparent;";

/// Settings label (bold, light grey).
inline constexpr const char* kSettingsLabel =
    "color: #C9D1D9; font-weight: bold; border: none; background: transparent;";

/// Small info text (muted grey 11px).
inline constexpr const char* kSmallInfo =
    "color: #8B949E; font-size: 11px; border: none; background: transparent;";

/// Metric value (18px bold white).
inline constexpr const char* kMetricValue =
    "color: #F0F6FC; font-size: 18px; font-weight: bold; border: none; background: transparent;";

/// Status label (muted, bold).
inline constexpr const char* kStatusIdle =
    "color: #8B949E; font-weight: bold; border: none; background: transparent;";

/// Status label (green, bold).
inline constexpr const char* kStatusRunning =
    "color: #27AE60; font-weight: bold; border: none; background: transparent;";

/// Error text (red, bold).
inline constexpr const char* kErrorText =
    "color: #E74C3C; font-size: 18px; font-weight: bold; border: none; background: transparent;";

/// Warning / info banner (muted yellow on dark background).
inline constexpr const char* kWarningBanner =
    "color: #F39C12; font-size: 13px; font-weight: bold; border: 1px solid #F39C12; "
    "border-radius: 6px; padding: 8px; background-color: rgba(243,156,18,0.1);";

}}} // namespace occt::gui::styles
