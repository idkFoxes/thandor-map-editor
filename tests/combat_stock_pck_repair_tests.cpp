#include "thandor/catalog.hpp"
#include "thandor/simulation_lab_environment.hpp"

#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>

using namespace thandor;
namespace {
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// This is a stock SHT-backed repair contact. The collision fixture is deliberately
// synthetic and deterministic; it is NOT a retail placement or an end-to-end
// renderer/animation test. It checks that the signed per-class damage and the
// beam lifetime reach the real Lab projectile -> hull -> integrity path.
void check_contact(const ShotDefinitionRecord& shot, uint32_t faction,
                   uint32_t flags, int32_t integrity, bool intercept_enemy) {
    auto field = FieldGrid::create(60, 60, 0, 0);
    const auto& cell = field.at(25, 25);
    const Vec3f origin{q12_to_float(cell.world_x_q12),
                       q12_to_float(cell.world_y_q12), 3.0F};
    auto catalog = std::make_shared<SimulationLabRuntimeCatalog>();
    auto& sprite = catalog->sprites["stock_repair_contact_fixture"];
    sprite.has_bounds = true;
    sprite.bounds_min = {-0.2F, -0.2F, -0.2F};
    sprite.bounds_max = { 0.2F,  0.2F,  0.2F};

    ModelVisualNode node;
    node.parent_index = UINT32_MAX;
    node.model_definition_id = 20;
    node.runtime_class_id = 10;
    node.sprite_path = "stock_repair_contact_fixture";
    node.shot_impact_class_index = 1;
    node.simulation.placement_contact_kind = 3;
    const auto hierarchy = std::make_shared<const std::vector<ModelVisualNode>>(
        std::vector<ModelVisualNode>{node});

    SimulationLabSeedEntity source;
    source.army_asset_id = 1;
    source.faction_index = 1;
    source.runtime_model_class_id = 10;
    source.shot_impact_class_index = 1;
    source.root_model_maximum_integrity = 1000;
    source.actor.position = origin;
    SimulationLabWeaponSeed weapon;
    weapon.shot = shot;
    source.combat_weapons = {weapon};

    auto target = source;
    target.combat_weapons.clear();
    target.faction_index = faction;
    target.source_placement_index = 1;
    target.actor.position.x += 2.0F;
    auto crossing = target;
    crossing.faction_index = 2;
    crossing.source_placement_index = 2;
    crossing.actor.position.y += 5.0F;
    std::array<SimulationLabSeedEntity, 3> seeds{source, target, crossing};

    SimulationLabScenarioRuntimeSeed scenario;
    scenario.runtime_catalog = catalog;
    SimulationLabConfig config;
    config.run_count = 1;
    config.duration_ticks = 16;
    std::array<uint32_t, AiKnowledgeData::kParameterCount> ki{};
    SimulationLabExperiment experiment;
    experiment.reset(config, ki, seeds, {}, {}, {}, {}, {}, scenario);
    auto& run = experiment.runs()[0];
    run.entities[1].runtime_model_hierarchy = hierarchy;
    run.entities[2].runtime_model_hierarchy = hierarchy;
    auto& hit = run.entities[1];
    hit.root_model_integrity = integrity;
    hit.runtime_flags = flags;
    require(!hit.model_components.empty(), "target root missing");
    hit.model_components[hit.root_model_component_index].integrity = integrity;
    run.entities[2].root_model_integrity = 600;
    run.entities[2].model_components[run.entities[2].root_model_component_index].integrity = 600;
    if (intercept_enemy) {
        run.entities[2].actor.position = hit.actor.position;
        hit.actor.position.y += 5.0F;
    }

    SimulationProjectileState projectile;
    projectile.serial = 1;
    projectile.owner_placement_index = 0;
    projectile.weapon_profile_index = 0;
    projectile.shot_definition_id = shot.definition_id;
    projectile.launch_position = origin;
    projectile.launch_velocity = {1, 0, 0};
    projectile.target_position = target.actor.position;
    projectile.live_target_placement_index = 1;
    projectile.lifetime_ticks = shot.projectile_lifetime_ticks;
    projectile.travel_ticks = 1;
    run.projectiles = {projectile};

    experiment.step(field, 1);
    require(run.metrics.projectile_impacts == 1U, "stock SHT beam did not hit the fixture hull");
    if (intercept_enemy) {
        require(run.projectiles[0].impact_target_placement_index == 2U,
                "intervening enemy not identified as actual contact");
        require(run.entities[2].root_model_integrity == 600 &&
                    run.metrics.projectile_repair_contacts_rejected == 1U,
                "stock SHT healed an intervening enemy");
        return;
    }
    const bool should_heal = faction == 1 && flags == 0 && integrity < 1000;
    const int32_t expected = should_heal ? integrity -
        shot.target_impact_damage_q12[1] / static_cast<int32_t>(shot.projectile_lifetime_ticks)
        : integrity;
    require(run.entities[1].root_model_integrity == expected,
            "stock SHT class-1 repair amount differs from signed damage/lifetime");
    require(run.metrics.projectile_repair_contacts_healed == (should_heal ? 1U : 0U),
            "stock SHT healed-contact counter differs from actual integrity");
}
}

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "usage: combat_stock_pck_repair_tests STOCK_PCK_DIRECTORY\n";
        return 2;
    }
    // scan_archive_root deliberately does not require an executable or write to
    // the retail PCKs; FILME.PCK is ignored by the catalog.
    const auto catalog = AssetCatalog::scan_archive_root(argv[1]);
    std::map<uint32_t, ShotDefinitionRecord> repairs;
    std::unordered_set<std::string> seen_archive_paths;
    for (const auto& asset : catalog.assets()) {
        if (asset.kind != AssetKind::Shot ||
            !seen_archive_paths.insert(asset.normalized_path).second) continue;
        const auto file = ShotAsset::parse(catalog.load(asset));
        for (const auto& shot : file.definitions())
            if (shot.is_repair_or_recovery_shot())
                repairs.try_emplace(shot.definition_id, shot);
    }
    constexpr std::array<uint32_t, 4> stock_repair_ids{33U, 1033U, 2033U, 3033U};
    require(repairs.size() == stock_repair_ids.size(),
            "unexpected stock repair definition census; inspect assets before changing semantics");
    for (const auto id : stock_repair_ids) {
        const auto found = repairs.find(id);
        require(found != repairs.end(), "stock repair SHT ID missing");
        const auto* selected = catalog.shot_definition(id);
        require(selected != nullptr, "stock repair SHT absent from canonical catalog");
        const auto& shot = *selected;
        require(shot.trajectory_kind() == ShotTrajectoryMode::DirectLine,
                "stock repair shot changed trajectory; non-beam stock test now required");
        require(shot.launch_speed_q12 == 1125 && shot.projectile_lifetime_ticks == 10,
                "stock repair SHT launch/lifetime changed");
        require(shot.target_impact_damage_q12[1] < 0,
                "stock repair damage class 1 is no longer negative");
        check_contact(shot, 1, 0, 600, false);
        check_contact(shot, 2, 0, 600, false);
        check_contact(shot, 1, 0x400U, 600, false);
        check_contact(shot, 1, 0, 1000, false);
        check_contact(shot, 1, 0, 600, true);
    }
    std::cout << "V720_STOCK_REPAIR_PCK PASS definitions=" << repairs.size()
              << " beam_contact_cases=20\n";
} catch (const std::exception& error) {
    std::cerr << "V720_STOCK_REPAIR_PCK FAIL: " << error.what() << '\n';
    return 1;
}
