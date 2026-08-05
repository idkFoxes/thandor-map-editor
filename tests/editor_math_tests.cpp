#include "math.hpp"
#include "thandor/level.hpp"

#include <cassert>
#include <cmath>

namespace {
bool close(float left, float right, float epsilon = 0.001F) {
    return std::abs(left - right) <= epsilon;
}
} // namespace

int main() {
    const editor::Mat4 identity = editor::identity();
    const editor::Vec4f input{1.0F, 2.0F, 3.0F, 1.0F};
    const auto output = editor::multiply(identity, input);
    assert(output.x == input.x);
    assert(output.y == input.y);
    assert(output.z == input.z);
    assert(output.w == input.w);

    float screen_x = 0.0F;
    float screen_y = 0.0F;
    float depth = 0.0F;
    const bool visible = editor::project_to_screen(
        thandor::Vec3f{0.0F, 0.0F, 0.5F}, identity, 1280.0F, 720.0F,
        screen_x, screen_y, depth);
    assert(visible);
    assert(close(screen_x, 640.0F));
    assert(close(screen_y, 360.0F));
    assert(close(depth, 0.5F));

    // A correct column-vector look-at matrix stores the camera basis in rows. The target must land
    // on the view Z axis, and a point on camera-right must remain on screen-right rather than being
    // mirrored by a transposed basis.
    editor::Camera camera;
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.yaw = 0.0F;
    camera.pitch = 0.75F;
    camera.distance = 12.0F;
    const auto view = camera.view();
    const auto target_view = editor::multiply(view, editor::Vec4f{0.0F, 0.0F, 0.0F, 1.0F});
    assert(close(target_view.x, 0.0F));
    assert(close(target_view.y, 0.0F));
    assert(target_view.z < 0.0F);

    const auto view_projection = camera.view_projection(16.0F / 9.0F);
    float center_x = 0.0F, center_y = 0.0F, center_depth = 0.0F;
    float right_x = 0.0F, right_y = 0.0F, right_depth = 0.0F;
    assert(editor::project_to_screen(camera.target, view_projection, 1600.0F, 900.0F,
                                     center_x, center_y, center_depth));
    assert(editor::project_to_screen(editor::add(camera.target, camera.planar_right()),
                                     view_projection, 1600.0F, 900.0F,
                                     right_x, right_y, right_depth));
    assert(right_x > center_x);

    // Orbiting is bounded to the upper hemisphere even when an input source supplies an extreme
    // delta, so the map cannot be rolled through an upside-down camera state.
    camera.orbit(0.0F, 100.0F);
    assert(close(camera.pitch, editor::Camera::kMaximumPitch));
    assert(camera.eye().z > camera.target.z);
    camera.orbit(0.0F, -200.0F);
    assert(close(camera.pitch, editor::Camera::kMinimumPitch));
    assert(camera.eye().z > camera.target.z);

    camera.target = {0.0F, 0.0F, 0.0F};
    camera.yaw = 0.0F;
    camera.move_planar(2.0F, 3.0F);
    assert(close(camera.target.x, -3.0F));
    assert(close(camera.target.y, 2.0F));
    assert(close(camera.target.z, 0.0F));

    // The serialized LEV pair is engine world-Y at +0x08 and engine world-X at +0x0C. The game
    // model transform maps those to render X and render Y respectively.
    thandor::LevelPlacement placement{};
    placement.set_render_position_q12(thandor::float_to_q12(12.5F),
                                      thandor::float_to_q12(-7.25F));
    assert(close(placement.render_x(), 12.5F));
    assert(close(placement.render_y(), -7.25F));
    assert(close(placement.world_y(), 12.5F));
    assert(close(placement.world_x(), -7.25F));
    return 0;
}
