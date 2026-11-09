#pragma once

namespace editor {

inline constexpr double kSimulationLabUiRefreshIntervalSeconds = 0.5;

constexpr bool simulation_lab_ui_refresh_due(double now_seconds,
                                              double last_refresh_seconds,
                                              bool initialized) noexcept {
    if (!initialized) return true;
    if (now_seconds < last_refresh_seconds) return true;
    return now_seconds - last_refresh_seconds >= kSimulationLabUiRefreshIntervalSeconds;
}

} // namespace editor
