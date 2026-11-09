#include "game_cursor.hpp"
#include "thandor/catalog.hpp"
#include "thandor/gfx.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_synthetic_cursor() {
    thandor::RgbaImage image{2U, 2U, {
        255U, 255U, 255U, 255U, 0U, 0U, 0U, 0U,
        255U, 0U, 0U, 255U, 0U, 255U, 0U, 255U,
    }};
    const auto gfx = thandor::encode_single_rgba_gfx(image);
    std::vector<std::byte> dat(editor::GameCursorAsset::kRecordSize, std::byte{0});
    thandor::write_i32(dat, 0x00U, 3);
    thandor::write_i32(dat, 0x04U, 4);
    thandor::write_u32(dat, 0x08U, 0U);
    thandor::write_u32(dat, 0x0CU, 0U);
    thandor::write_u32(dat, 0x10U, 0U);
    thandor::write_u32(dat, 0x14U, 0U);

    const auto cursor = editor::GameCursorAsset::parse(gfx, dat);
    require(cursor.frame_count() == 1U, "synthetic cursor frame count");
    require(cursor.subresource_count() == 1U, "synthetic cursor image count");
    require(cursor.frame(0U).hotspot_x == 3 && cursor.frame(0U).hotspot_y == 4,
            "synthetic cursor hotspot");
    require(cursor.subresource_index(0U, false, 0.0) == 0U,
            "synthetic idle subresource");
    require(cursor.subresource_index(0U, true, 9.0) == 0U,
            "synthetic active subresource");
    const auto& decoded = cursor.image(0U, false, 0.0);
    require(decoded.pixels[7U] == 0U, "transparent cursor pixels must remain transparent");
}

void test_binary_scales() {
    constexpr float turn = editor::game_camera_turn_radians_per_pixel();
    constexpr float expected = 16.0F * 2.0F * std::numbers::pi_v<float> / 65536.0F;
    require(std::fabs(turn - expected) < 1.0e-7F, "turn16 camera scale");
    require(std::fabs(editor::game_camera_pan_world_units_per_pixel(45.0F) -
                      45.0F / 2048.0F) < 1.0e-7F,
            "Q12 camera pan scale");
}

void test_editor_source_contract(const std::filesystem::path& source_root) {
    const auto source = read_text(source_root / "src/editor/editor_app.cpp");
    require(source.find("GameCursorAsset::kCameraMoveFrame") != std::string::npos,
            "placed camera move cursor is absent");
    require(source.find("GameCursorAsset::kCameraTurnFrame") != std::string::npos,
            "placed camera turn cursor is absent");
    require(source.find("ImGui::SetMouseCursor(ImGuiMouseCursor_None)") != std::string::npos,
            "system cursor is not hidden over the map canvas");
    require((source.find("if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){\n"
                         "        game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;") !=
                 std::string::npos) ||
                (source.find("right_pointer_capture_active_&&right_pointer_dragged_") !=
                     std::string::npos &&
                 source.find("game_cursor_frame_=GameCursorAsset::kCameraMoveFrame;") !=
                     std::string::npos &&
                 source.find("glfwSetCursorPos(window_,right_pointer_capture_center_.x") !=
                     std::string::npos),
            "right drag is not the retail anchored camera move path");
    require(source.find("if(tool_==Tool::Select&&workspace_transform_action_==0&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,4.0F))") !=
                std::string::npos,
            "left drag is not the Select-mode camera turn path");
    require(source.find("camera_.orbit(-io.MouseDelta.x*0.008F") == std::string::npos,
            "legacy right-drag orbit scale remains");
}

void test_stock_cursor(const std::filesystem::path& game_root) {
    const auto catalog = thandor::AssetCatalog::scan_game_root(game_root);
    const auto cursor = editor::GameCursorAsset::parse(catalog.load("engine/mouse.gfx"),
                                                       catalog.load("engine/mouse.dat"));
    require(cursor.frame_count() == 55U, "stock mouse.dat must contain 55 frames");
    require(cursor.subresource_count() == 303U, "stock mouse.gfx must contain 303 subresources");

    const auto& arrow = cursor.frame(editor::GameCursorAsset::kDefaultFrame);
    require(arrow.hotspot_x == 0 && arrow.hotspot_y == 0 &&
            arrow.idle_first == 0U && arrow.idle_last == 0U &&
            arrow.active_first == 1U && arrow.active_last == 1U,
            "stock default arrow record differs");
    const auto& move = cursor.frame(editor::GameCursorAsset::kCameraMoveFrame);
    require(move.hotspot_x == 9 && move.hotspot_y == 9 &&
            move.idle_first == 2U && move.idle_last == 2U &&
            move.active_first == 2U && move.active_last == 2U,
            "stock move cursor record differs");
    const auto& turn = cursor.frame(editor::GameCursorAsset::kCameraTurnFrame);
    require(turn.hotspot_x == 8 && turn.hotspot_y == 6 &&
            turn.idle_first == 42U && turn.idle_last == 42U &&
            turn.active_first == 42U && turn.active_last == 42U,
            "stock turn cursor record differs");
}

} // namespace

int main(int argc, char** argv) {
    try {
        test_synthetic_cursor();
        test_binary_scales();
#ifdef THANDOR_SOURCE_DIR
        test_editor_source_contract(THANDOR_SOURCE_DIR);
#endif
        if (argc > 1) test_stock_cursor(argv[1]);
        std::cout << "G354 game cursor and mouse camera controls PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "G354 FAIL: " << error.what() << '\n';
        return 1;
    }
}
