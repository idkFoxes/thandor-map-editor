#pragma once

#include "viewport_picking.hpp"
#include <algorithm>

namespace editor {
struct WorkspaceRect {
    float x{}, y{}, width{}, height{};
    bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    float aspect() const noexcept { return width / std::max(height, 1.0F); }
};

struct EditorWorkspaceLayout {
    WorkspaceRect header, assets, inspector, ribbon, canvas, overview, status;
    static EditorWorkspaceLayout calculate(float width, float height, bool left, bool right,
                                           bool compact, bool expanded_overview, float scale=1.0F) noexcept {
        if(scale!=1.0F && scale>0.0F){
            auto layout=calculate(width/scale,height/scale,left,right,compact,expanded_overview);
            for(auto* rect:{&layout.header,&layout.assets,&layout.inspector,&layout.ribbon,
                           &layout.canvas,&layout.overview,&layout.status}){
                rect->x*=scale;rect->y*=scale;rect->width*=scale;rect->height*=scale;
            }
            return layout;
        }
        width = std::max(width, 1.0F);
        height = std::max(height, 1.0F);
        const float header_height = std::min(48.0F, height * 0.16F);
        const float footer_height = std::min(24.0F, height * 0.08F);
        // Leave usable canvas space when resizing down; hiding either panel returns its width.
        const float panel_budget = std::max(0.0F, width - std::min(380.0F, width * 0.48F));
        const float panels = (left ? 350.0F : 0.0F) + (right ? 350.0F : 0.0F);
        const float factor = panels > 0.0F ? std::min(1.0F, panel_budget / panels) : 0.0F;
        const float lw = left ? 350.0F * factor : 0.0F;
        const float rw = right ? 350.0F * factor : 0.0F;
        const float body_height = std::max(1.0F, height - header_height - footer_height);
        const float ribbon_height = std::min(compact ? 56.0F : 94.0F, body_height * 0.24F);
        const float overview_height = std::min(expanded_overview ? 188.0F : 32.0F,
                                               body_height * (expanded_overview ? 0.30F : 0.12F));
        const float center_width = std::max(1.0F, width - lw - rw);
        return {{0, 0, width, header_height}, {0, header_height, lw, body_height},
                {width-rw, header_height, rw, body_height},
                {lw, header_height, center_width, ribbon_height},
                {lw, header_height+ribbon_height, center_width,
                 std::max(1.0F, body_height-ribbon_height-overview_height)},
                {lw, height-footer_height-overview_height, center_width, overview_height},
                {0, height-footer_height, width, footer_height}};
    }
};

inline bool project_to_workspace(Vec3f point, const Mat4& vp, const WorkspaceRect& rect,
                                  float& x, float& y, float& depth) noexcept {
    if (!project_to_screen(point, vp, rect.width, rect.height, x, y, depth)) return false;
    x += rect.x; y += rect.y;
    return true;
}
inline ViewRay workspace_view_ray(const Camera& camera, float x, float y,
                                  const WorkspaceRect& rect) noexcept {
    return make_view_ray(camera, x-rect.x, y-rect.y, rect.width, rect.height);
}
} // namespace editor
