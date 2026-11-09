#include "thandor/binary.hpp"
#include "thandor/level.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    assert(input.good());
    std::ostringstream stream;
    stream << input.rdbuf();
    return stream.str();
}

std::vector<std::byte> make_level_fixture() {
    std::vector<std::byte> bytes(0x340U);
    thandor::write_u32(bytes, 0x00U, thandor::fourcc("lev"));
    thandor::write_u32(bytes, 0x04U, static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes, 0xD8U, 0U);
    thandor::write_u32(bytes, 0xDCU, static_cast<uint32_t>(bytes.size()));
    for (std::size_t index = 0U; index < 10U; ++index) {
        thandor::write_u32(bytes, 0xB0U + index * 4U,
                           static_cast<uint32_t>(0x318U + index * 4U));
    }
    thandor::write_u32(bytes, 0x310U, 2U); // human/assignable prefix
    thandor::write_u32(bytes, 0x314U, 4U); // active factions
    for (uint32_t faction = 1U; faction <= thandor::LevelAsset::kFactionCameraStartCount; ++faction) {
        const std::size_t offset = 0x200U + static_cast<std::size_t>(faction - 1U) * 0x20U;
        thandor::write_i32(bytes, offset + 0x00U, static_cast<int32_t>(faction * 0x1000U));
        thandor::write_i32(bytes, offset + 0x04U, static_cast<int32_t>(faction * 0x2000U));
        thandor::write_i32(bytes, offset + 0x08U, static_cast<int32_t>(faction * 0x3000U));
        thandor::write_u32(bytes, offset + 0x0CU, 0x1000U);
        thandor::write_u32(bytes, offset + 0x10U, faction * 0x01000100U);
        thandor::write_u32(bytes, offset + 0x14U, 0xA0000000U + faction);
        thandor::write_u32(bytes, offset + 0x18U, 0xB0000000U + faction);
        thandor::write_u32(bytes, offset + 0x1CU, 0xC0000000U + faction);
    }
    return bytes;
}

} // namespace

int main() {
    const auto source_root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const std::string editor = read_text(source_root / "src/editor/editor_app.cpp");
    const std::string header = read_text(source_root / "src/editor/editor_app.hpp");


    assert(header.find("void draw_faction_camera_start_cards();") != std::string::npos);
    assert(editor.find("draw_faction_camera_start_cards();") != std::string::npos);
    assert(editor.find("Player / AI starting cameras") != std::string::npos);
    assert(editor.find("frontend_active_faction_count()") != std::string::npos);
    assert(editor.find("frontend_assignable_faction_count()") != std::string::npos);
    assert(editor.find("human?\"Player\":\"AI\"") != std::string::npos);
    assert(editor.find("void EditorApp::draw_faction_camera_start_cards()") != std::string::npos);
    assert(editor.find("View stored") != std::string::npos);
    assert(editor.find("Aim at start") != std::string::npos);
    assert(editor.find("Set from current view") != std::string::npos);
    assert(editor.find("focus_faction_camera_start(faction)") != std::string::npos);
    assert(editor.find("aim_camera_at_faction_start_vehicle(faction)") != std::string::npos);
    assert(editor.find("capture_faction_camera_start(faction)") != std::string::npos);


    // User-provided G366 build log regression: std::span has operator[] but no at().
    assert(editor.find("subresources().at(") == std::string::npos);
    assert(editor.find("EFF frame selects missing GFX subresource") != std::string::npos);
    assert(editor.find("dynamic actor selects missing GFX subresource") != std::string::npos);

    auto level = thandor::LevelAsset::parse(make_level_fixture());
    assert(level.frontend_assignable_faction_count() == 2U);
    assert(level.frontend_active_faction_count() == 4U);
    for (uint32_t faction = 1U; faction <= 4U; ++faction) {
        const std::size_t offset = 0x200U + static_cast<std::size_t>(faction - 1U) * 0x20U;
        const auto original = level.serialize();
        const uint32_t resource_a = thandor::read_u32(original, offset + 0x14U);
        const uint32_t resource_b = thandor::read_u32(original, offset + 0x18U);
        const uint32_t ai_mode = thandor::read_u32(original, offset + 0x1CU);
        auto camera = level.faction_camera_start(faction);
        camera.position_x_q12 += 0x321;
        camera.heading_turn16 = static_cast<uint16_t>(camera.heading_turn16 + 0x123U);
        level.set_faction_camera_start(faction, camera);
        const auto saved = level.serialize();
        assert(thandor::read_u32(saved, offset + 0x14U) == resource_a);
        assert(thandor::read_u32(saved, offset + 0x18U) == resource_b);
        assert(thandor::read_u32(saved, offset + 0x1CU) == ai_mode);
    }

    std::cout << "G367 General player/AI camera-start cards and G366 compile fix PASS\n";
    return 0;
}
