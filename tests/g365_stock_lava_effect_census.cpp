#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) try {
    if (argc < 2) {
        std::cout << "G365_STOCK_LAVA_EFFECT_CENSUS PASS unit-only\n";
        return 0;
    }
    auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    const auto lava = thandor::LevelAsset::parse(catalog.load("level/lavam.lev"));
    if (thandor::normalize_asset_path(lava.paths().surface_textures).find("lava") ==
        std::string::npos)
        throw std::runtime_error("lavam.lev does not select the lava surface family");

    std::map<std::uint32_t, std::size_t> placements;
    for (const auto& placement : lava.placements()) ++placements[placement.army_asset_id];
    if (placements[647U] != 1U || placements[300U] != 3U ||
        placements[810U] != 2U || placements[811U] != 1U || placements[814U] != 9U)
        throw std::runtime_error("stock lava placement census changed");

    const auto* preset647 = catalog.army_preset(647U);
    const auto* preset300 = catalog.army_preset(300U);
    if (preset647 == nullptr || preset647->timed_effect_id != 25U ||
        preset647->timed_effect_interval_ticks != 270U)
        throw std::runtime_error("ARM 647 no longer routes to periodic EFF 25");
    if (preset300 == nullptr || preset300->timed_effect_id != 131U ||
        preset300->timed_effect_interval_ticks != 8U)
        throw std::runtime_error("ARM 300 no longer routes to periodic EFF 131");

    const auto* effect25 = catalog.effect_definition(25U);
    const auto* effect31 = catalog.effect_definition(31U);
    const auto* effect2 = catalog.effect_definition(2U);
    const auto* effect111 = catalog.effect_definition(111U);
    const auto* effect131 = catalog.effect_definition(131U);
    const auto* shot28 = catalog.shot_definition(28U);
    if (effect25 == nullptr || effect31 == nullptr || effect2 == nullptr ||
        effect111 == nullptr || effect131 == nullptr || shot28 == nullptr)
        throw std::runtime_error("lava effect/shot registry chain is incomplete");
    if (effect25->transition_kind != 1U || effect25->runtime_value_0c != 257U ||
        effect25->linked_shot_present != 32U || effect25->linked_shot_id != 28U)
        throw std::runtime_error("EFF 25 controller fields changed");
    if (shot28->trajectory_mode != 1 || shot28->launch_speed_q12 != 450 ||
        shot28->primary_effect_id != 31U || shot28->terrain_impact_effect_id(0U) != 2U ||
        shot28->secondary_effect_id != 111U || shot28->secondary_effect_interval_ticks != 1U ||
        shot28->ballistic_divisor_q12 <= 0)
        throw std::runtime_error("Shot 28 visual chain changed");
    if (effect131->transition_kind != 2U || effect131->runtime_value_0c != 8U ||
        effect131->frame_advance_threshold_q4 != 160U)
        throw std::runtime_error("EFF 131 timed arc fields changed");

    const auto controller = thandor::SpriteAsset::parse(catalog.load(effect25->sprite_path));
    std::array<bool, 8> kind2{};
    for (const auto& attachment : controller.attachment_records())
        if (attachment.kind() == 2U && attachment.child_index() < kind2.size())
            kind2[attachment.child_index()] = true;
    if (!std::all_of(kind2.begin(), kind2.end(), [](bool value) { return value; }))
        throw std::runtime_error("EFF 25 controller lost one of its eight type-2 arc targets");

    std::cout << "G365_STOCK_LAVA_EFFECT_CENSUS surface=lava arm647_eff25=1 "
                 "arm300_eff131=3 linked_shot28=1 type2_targets=8 PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G365_STOCK_LAVA_EFFECT_CENSUS FAIL: " << error.what() << '\n';
    return 1;
}
