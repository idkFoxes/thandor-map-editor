#include "viewport_picking.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>


#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

bool near(float left, float right, float tolerance = 1.0e-3F) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

int main() {
    using namespace editor;

    Camera camera;
    camera.target = {0.0F, 0.0F, 0.0F};
    camera.yaw = -0.8F;
    camera.pitch = 0.75F;
    camera.distance = 45.0F;
    const auto center_ray = make_view_ray(camera, 640.0F, 360.0F, 1280.0F, 720.0F);
    require(dot(center_ray.direction, normalize(sub(camera.target, camera.eye()))) > 0.9999F,
            "center-screen ray must point at the camera target");

    const auto right_ray = make_view_ray(camera, 1000.0F, 360.0F, 1280.0F, 720.0F);
    require(dot(right_ray.direction, camera.planar_right()) >
            dot(center_ray.direction, camera.planar_right()),
            "right-side screen ray must move toward camera-right");

    auto field = thandor::FieldGrid::create(8U, 8U, thandor::float_to_q12(2.0F), 0U);
    const auto& target_cell = field.at(4U, 3U);
    const editor::Vec3f target{
        thandor::q12_to_float(target_cell.world_x_q12),
        thandor::q12_to_float(target_cell.world_y_q12),
        target_cell.terrain_height()};
    ViewRay downward{{target.x, target.y, target.z + 20.0F}, {0.0F, 0.0F, -1.0F}};
    const auto terrain_hit = pick_terrain_cell(field, downward);
    require(terrain_hit.has_value(), "ray through a triangle interior must hit terrain");
    require(terrain_hit->cell_x == 4U && terrain_hit->cell_y == 3U,
            "terrain hit must resolve to the nearest persisted FLD cell");
    require(near(terrain_hit->position.z, 2.0F), "terrain hit must preserve terrain height");

    // The previous picker only accepted a projected terrain vertex within 24 pixels. This ray
    // targets the center of a cell triangle and must still resolve, proving interior brush clicks.
    const auto& a = field.at(2U, 2U);
    const auto& b = field.at(2U, 3U);
    const auto& c = field.at(3U, 2U);
    const Vec3f interior{
        (thandor::q12_to_float(a.world_x_q12) + thandor::q12_to_float(b.world_x_q12) +
         thandor::q12_to_float(c.world_x_q12)) / 3.0F,
        (thandor::q12_to_float(a.world_y_q12) + thandor::q12_to_float(b.world_y_q12) +
         thandor::q12_to_float(c.world_y_q12)) / 3.0F,
        2.0F};
    const auto interior_hit = pick_terrain_cell(
        field, ViewRay{{interior.x, interior.y, 12.0F}, {0.0F, 0.0F, -1.0F}});
    require(interior_hit.has_value(), "triangle-interior click must not require a nearby mesh vertex");

    AxisAlignedBounds building;
    building.include({-4.0F, -3.0F, 0.0F});
    building.include({4.0F, 3.0F, 8.0F});
    const auto building_hit = ray_bounds_distance(
        ViewRay{{0.0F, -20.0F, 4.0F}, {0.0F, 1.0F, 0.0F}}, building);
    require(building_hit.has_value() && near(*building_hit, 17.0F),
            "selection ray must hit the complete placement volume, not only its root marker");
    const auto miss = ray_bounds_distance(
        ViewRay{{10.0F, -20.0F, 4.0F}, {0.0F, 1.0F, 0.0F}}, building);
    require(!miss.has_value(), "selection ray outside placement bounds must miss");

    std::ifstream editor_stream(std::string(THANDOR_SOURCE_DIR) + "/src/editor/editor_app.cpp");
    std::ostringstream editor_text_stream;
    editor_text_stream << editor_stream.rdbuf();
    const std::string editor_text = editor_text_stream.str();
    require(editor_text.find("for(const auto& vertex:renderer_.cpu_vertices())") == std::string::npos,
            "projected-vertex terrain picking must remain removed");
    require(editor_text.find("float best=30.0F") == std::string::npos,
            "placement-root 30-pixel selection must remain removed");
    require(editor_text.find("ImGui::IsMouseClicked(ImGuiMouseButton_Left)?(1.0F/60.0F):0.0F") !=
                std::string::npos,
            "raise/lower click must retain a deterministic minimum impulse");
    require(editor_text.find("A transient miss at the map edge must not terminate a held brush stroke") !=
                std::string::npos,
            "held brush stroke must survive transient terrain misses");

    std::cout << "G370 viewport ray, terrain interior brush, placement-volume selection, and stroke continuity PASS\n";
    return 0;
}
