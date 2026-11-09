#include "thandor/binary.hpp"
#include "thandor/model.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (char character : value) {
        thandor::write_u16(data, offset, static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> make_mdl_fixture() {
    std::vector<std::byte> data(0x500U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);

    constexpr std::size_t record = 0x200U;
    thandor::write_u32(data, record + 0x00U, 0x300U);
    thandor::write_u32(data, record + 0x04U, 50U);
    thandor::write_u32(data, record + 0x08U, 0x123U);
    thandor::write_u32(data, record + 0x4CU, 13U);
    thandor::write_u32(data, record + 0x64U, 0x300U);
    thandor::write_u32(data, record + 0xC0U, 5U);

    constexpr std::size_t root = 0x300U;
    thandor::write_u32(data, root + 0x00U, 0x80U);
    thandor::write_u32(data, root + 0x04U, 0U);
    thandor::write_u32(data, root + 0x08U, 0U);
    thandor::write_u32(data, root + 0x0CU, 0x4000U);
    thandor::write_u32(data, root + 0x10U, 0U);
    thandor::write_u32(data, root + 0x14U, 2U);
    thandor::write_u32(data, root + 0x18U, 0x380U);
    thandor::write_u32(data, root + 0x1CU, 0x400U);
    write_ascii_utf16(data, root + 0x38U, "spr/gebaeude/root0.spr");

    constexpr std::size_t child0 = 0x380U;
    thandor::write_u32(data, child0 + 0x00U, 0x80U);
    thandor::write_u32(data, child0 + 0x04U, 0U);
    thandor::write_u32(data, child0 + 0x08U, 0x1000U);
    thandor::write_u32(data, child0 + 0x0CU, 0x4000U);
    thandor::write_u32(data, child0 + 0x10U, 0x2000U);
    thandor::write_u32(data, child0 + 0x14U, 0U);
    write_ascii_utf16(data, child0 + 0x38U, "spr/gebaeude/child0.spr");

    constexpr std::size_t child1 = 0x400U;
    thandor::write_u32(data, child1 + 0x00U, 0x80U);
    thandor::write_u32(data, child1 + 0x04U, 1U); // low-nibble flag means no runtime node
    thandor::write_u32(data, child1 + 0x0CU, 0x4000U);
    thandor::write_u32(data, child1 + 0x14U, 0U);
    write_ascii_utf16(data, child1 + 0x38U, "spr/gebaeude/disabled.spr");
    return data;
}

std::vector<std::byte> make_spr_attachment_fixture() {
    std::vector<std::byte> data(0x240U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("spr"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0x0CU, 0x00020007U);
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, 0xE4U, 0x220U);
    thandor::write_u32(data, 0xE8U, 2U);
    thandor::write_u32(data, 0x200U, 0x20U); // one empty, valid LOD group
    thandor::write_u32(data, 0x204U, 0U);

    thandor::write_u32(data, 0x220U, 0U); // kind 0, child slot 0
    thandor::write_i32(data, 0x224U, thandor::float_to_q12(1.0F));
    thandor::write_i32(data, 0x228U, thandor::float_to_q12(2.0F));
    thandor::write_i32(data, 0x22CU, thandor::float_to_q12(3.0F));
    thandor::write_u32(data, 0x230U, (3U << 4U) | 1U); // kind 1, child slot 3
    thandor::write_i32(data, 0x234U, thandor::float_to_q12(-4.0F));
    thandor::write_i32(data, 0x238U, thandor::float_to_q12(5.0F));
    thandor::write_i32(data, 0x23CU, thandor::float_to_q12(6.0F));
    return data;
}
} // namespace

int main() try {
    const auto records = thandor::parse_model_visual_records(make_mdl_fixture(), "mdl/test.mdl");
    require(records.size() == 1U, "synthetic MDL did not produce one definition");
    const auto& record = records.front();
    require(record.hierarchy_nodes.size() == 3U,
            "MDL parser did not recursively retain root and child definitions");
    require(record.has_name_text_offset && record.name_text_offset == 50U,
            "MDL +0x04 localized-name offset was not preserved");
    require(record.hierarchy_nodes[0].sprite_path == "spr/gebaeude/root0.spr",
            "root inline UTF-16 SPR path was not decoded");
    require(record.hierarchy_nodes[1].parent_index == 0U &&
                record.hierarchy_nodes[1].child_slot == 0U &&
                record.hierarchy_nodes[1].creates_runtime_node(),
            "live child relationship or low-nibble runtime-node predicate is wrong");
    require(record.hierarchy_nodes[2].parent_index == 0U &&
                record.hierarchy_nodes[2].child_slot == 1U &&
                !record.hierarchy_nodes[2].creates_runtime_node(),
            "disabled child node flag was ignored");
    require(record.runtime_class_id == 13U && record.has_primary_texture_animation &&
                record.primary_animated_subresource == 5U,
            "class-13 root texture animation selector is not preserved");

    const auto sprite = thandor::SpriteAsset::parse(make_spr_attachment_fixture());
    const auto* child0 = sprite.child_attachment(0U);
    const auto* child3 = sprite.child_attachment(3U);
    require(child0 != nullptr && child0->kind() == 0U && child0->local_translation.z == 3.0F,
            "SPR kind-0 child attachment was not recovered");
    require(child3 != nullptr && child3->kind() == 1U && child3->local_translation.x == -4.0F,
            "SPR kind-1 child attachment was not recovered");
    require(sprite.child_attachment(2U) == nullptr,
            "SPR child lookup invented a missing attachment");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto preview = read_text(root / "src/editor/asset_preview.hpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    require(editor.find("triangle_count/1400U") == std::string::npos &&
                editor.find("for(std::size_t node_index=0;node_index<hierarchy.size();++node_index)") !=
                    std::string::npos,
            "preview still skips polygons or does not traverse the recursive MDL hierarchy");
    require(editor.find("child_attachment(node.child_slot)") != std::string::npos &&
                editor.find("compose_model_transform(parent.transform,local)") != std::string::npos,
            "placed buildings do not apply parent SPR attachment transforms recursively");
    require(preview.find("element_preview ? 18000U : 12000U") != std::string::npos &&
                preview.find("element_preview ? 6U : 5U") != std::string::npos,
            "preview texture subdivision budgets regressed to the low-fidelity values");
    require(model_source.find("if (visual.runtime_class_id == 13U && visual.placement_occupancy_class != 0U)") !=
                    std::string::npos &&
                model_source.find("visual.has_primary_texture_animation = true;") != std::string::npos,
            "class-13 root texture animation selector is not preserved");
    // Class 14 now legitimately appears in the parser for its independently proven MDL +0xC8
    // resource-extraction weight. Do not resurrect the old broad text ban; instead require that
    // the class-14 block is scoped to the resource field and does not assign texture animation.
    const auto class14_pos = model_source.find("visual.runtime_class_id == 14U");
    require(class14_pos != std::string::npos, "class-14 resource semantics are missing");
    const auto class14_end = model_source.find("if (size >= 0xC4U)", class14_pos);
    require(class14_end != std::string::npos &&
                model_source.substr(class14_pos, class14_end - class14_pos)
                        .find("has_primary_texture_animation") == std::string::npos,
            "class-14 resource parsing leaked into root texture animation");

    std::cout << "G349_RECURSIVE_MODEL_HIERARCHY_PREVIEW_FIDELITY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G349 checks failed: " << error.what() << '\n';
    return 1;
}
