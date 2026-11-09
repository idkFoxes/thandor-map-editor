#pragma once
#include <algorithm>
#include <cmath>

namespace editor {
struct EditorUiScale {
    float geometry{1.0F};
    float font_density{1.0F};
};
inline EditorUiScale editor_ui_scale(float monitor_scale, float framebuffer_ratio,
                                     int requested_percent) noexcept {
    if(!std::isfinite(monitor_scale)||monitor_scale<=0.0F)monitor_scale=1.0F;
    if(!std::isfinite(framebuffer_ratio)||framebuffer_ratio<=0.0F)framebuffer_ratio=1.0F;
    const float density=std::clamp(framebuffer_ratio,1.0F,4.0F);
    const float requested=requested_percent==0?monitor_scale:
        static_cast<float>(std::clamp(requested_percent,100,300))/100.0F;
    // Retina windows already use logical points. Windows/X11 windows use physical pixels.
    // Account for the framebuffer ratio once, rather than doubling a Retina UI twice.
    return {std::clamp(requested/framebuffer_ratio,0.5F,3.0F),density};
}
inline float editor_ui_scale_factor=1.0F;
inline float ui_px(float value) noexcept {
    // ImGui reserves zero for automatic size and negative values for right alignment.
    return value>0.0F?value*editor_ui_scale_factor:value;
}
} // namespace editor
