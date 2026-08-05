#include "thandor/catalog.hpp"
#include "thandor/text_resource.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <set>
#include <functional>
#include <unordered_set>

namespace thandor {
namespace {

std::string extension_of(std::string_view path) {
    const auto dot = path.find_last_of('.');
    return dot == std::string_view::npos ? std::string{} : lower_ascii(std::string(path.substr(dot + 1)));
}

std::string stem_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    const auto begin = slash == std::string_view::npos ? 0 : slash + 1;
    const auto end = dot == std::string_view::npos || dot < begin ? path.size() : dot;
    return std::string(path.substr(begin, end - begin));
}

std::string parent_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string_view::npos ? std::string{} : std::string(path.substr(0, slash));
}

std::string asset_base_path(std::string_view path) {
    std::string normalized = normalize_asset_path(path);
    const auto slash = normalized.find_last_of('/');
    const auto dot = normalized.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) normalized.resize(dot);
    return normalized;
}

std::string expected_model_path_for_arm(std::string_view arm_path) {
    std::string result = normalize_asset_path(arm_path);
    if (result.starts_with("arm/")) result.replace(0, 4, "mdl/");
    const auto dot = result.find_last_of('.');
    if (dot != std::string::npos) result.replace(dot, std::string::npos, ".mdl");
    return result;
}

int model_source_affinity(std::string_view arm_path, std::string_view model_path) {
    const std::string arm = normalize_asset_path(arm_path);
    const std::string model = normalize_asset_path(model_path);
    if (model == expected_model_path_for_arm(arm)) return 1000;
    const auto slash = arm.find_last_of('/');
    const auto dot = arm.find_last_of('.');
    const std::string stem = arm.substr(slash == std::string::npos ? 0U : slash + 1U,
        dot == std::string::npos ? std::string::npos : dot - (slash == std::string::npos ? 0U : slash + 1U));
    std::string family = stem;
    while (!family.empty() && (std::isdigit(static_cast<unsigned char>(family.back())) != 0))
        family.pop_back();
    if (family.starts_with("building")) family = "building";
    else if (family.starts_with("unit")) family = "unit";
    std::string suffix = stem.substr(family.size());
    int score = 0;
    if (!suffix.empty() && model.starts_with("mdl/" + suffix + "/")) score += 700;
    if (suffix.empty() && parent_of(model) == "mdl") score += 500;
    const auto model_slash = model.find_last_of('/');
    const std::string model_name = model.substr(model_slash == std::string::npos ? 0U : model_slash + 1U);
    if (!family.empty() && model_name.starts_with(family)) score += 200;
    return score;
}


const ModelVisualRecord* select_model_visual_for_arm(
    const std::unordered_map<uint32_t, std::vector<ModelVisualRecord>>& model_visuals,
    uint32_t definition_id, std::string_view arm_path) {
    const auto found = model_visuals.find(definition_id);
    if (found == model_visuals.end() || found->second.empty()) return nullptr;
    const auto selected = std::max_element(found->second.begin(), found->second.end(),
        [&](const ModelVisualRecord& left, const ModelVisualRecord& right) {
            return model_source_affinity(arm_path, left.source_path) <
                   model_source_affinity(arm_path, right.source_path);
        });
    return selected == found->second.end() ? &found->second.front() : &*selected;
}

struct DeferredModelAttachment {
    uint32_t parent_index{};
    uint32_t child_slot{};
    uint32_t angle0{};
    uint32_t angle1{};
    uint32_t angle2{};
};

uint32_t base_arm_model_definition_id(std::span<const std::byte> bytes, std::size_t node_offset) {
    if (node_offset > bytes.size() || 0x40U > bytes.size() - node_offset) return 0U;
    // The runtime selector scans the eight model-definition variants. The editor has no live
    // technology transition state, so the stock/base variant is the first nonzero definition.
    for (uint32_t variant = 0U; variant < 8U; ++variant) {
        const uint32_t definition_id = read_u32(bytes, node_offset + 0x20U + variant * 4U);
        if (definition_id != 0U) return definition_id;
    }
    return 0U;
}

const TechnologyRecord* technology_record_at(std::span<const TechnologyRecord> technologies,
                                              uint32_t technology_id) noexcept {
    return technology_id < technologies.size() ? &technologies[technology_id] : nullptr;
}

std::string technology_title_at(std::span<const TechnologyRecord> technologies,
                                uint32_t technology_id) {
    const auto* record = technology_record_at(technologies, technology_id);
    return record == nullptr ? std::string{} : record->title;
}

std::string technology_family_title(std::string title) {
    if (title.empty()) return title;
    const std::string lower = lower_ascii(title);
    std::size_t cut = title.size();
    for (const std::string_view marker : {
             " range", " damage", " armour", " rotation", " +", " improve "}) {
        const auto found = lower.find(marker);
        if (found != std::string::npos && found != 0U) cut = std::min(cut, found);
    }
    title.resize(cut);
    while (!title.empty() && (title.back() == ' ' || title.back() == ',')) title.pop_back();
    return title;
}

void collect_arm_model_variant_groups(
    ArmyAssetPreset& preset, std::span<const std::byte> arm_bytes,
    std::size_t root_node_offset, std::string_view arm_path,
    const std::unordered_map<uint32_t, std::vector<ModelVisualRecord>>& model_visuals,
    std::span<const TechnologyRecord> technologies) {
    std::unordered_set<std::size_t> active_nodes;
    std::function<void(std::size_t, uint32_t, uint32_t, uint32_t)> visit;
    visit = [&](std::size_t node_offset, uint32_t parent_group, uint32_t child_slot,
                uint32_t depth) {
        if (node_offset == 0U || node_offset > arm_bytes.size() ||
            0x40U > arm_bytes.size() - node_offset) return;
        if (!active_nodes.insert(node_offset).second)
            throw FormatError("ARM technology-variant hierarchy contains a cycle");

        ArmyModelVariantGroup group;
        group.arm_node_offset = static_cast<uint32_t>(node_offset);
        group.parent_group_index = parent_group;
        group.child_slot = child_slot;
        group.depth = depth;
        for (uint32_t variant_index = 0U; variant_index < 8U; ++variant_index) {
            const uint32_t model_definition_id =
                read_u32(arm_bytes, node_offset + 0x20U + variant_index * 4U);
            if (model_definition_id == 0U) continue;
            ModelTechnologyVariant variant;
            variant.variant_index = variant_index;
            variant.model_definition_id = model_definition_id;
            if (const auto* visual = select_model_visual_for_arm(
                    model_visuals, model_definition_id, arm_path); visual != nullptr) {
                variant.technology_unlock_id = visual->technology_unlock_id;
                variant.technology_title = technology_title_at(
                    technologies, visual->technology_unlock_id);
                variant.linked_technology_id = visual->linked_technology_id;
                if (visual->linked_technology_id != 0U) {
                    variant.linked_technology_title = technology_title_at(
                        technologies, visual->linked_technology_id);
                }
                variant.model_source_path = visual->source_path;
                variant.sprite_path = visual->sprite_path;
            }
            group.variants.push_back(std::move(variant));
        }
        const uint32_t group_index = static_cast<uint32_t>(preset.model_variant_groups.size());
        preset.model_variant_groups.push_back(std::move(group));

        const uint32_t child_count = read_u32(arm_bytes, node_offset + 8U);
        if (child_count > 8U)
            throw FormatError("ARM technology-variant child count exceeds eight slots");
        if (node_offset + 0x0CU + static_cast<std::size_t>(child_count) * 4U > arm_bytes.size())
            throw FormatError("ARM technology-variant child offsets exceed payload");
        for (uint32_t child = 0U; child < child_count; ++child) {
            const uint32_t child_offset = read_u32(arm_bytes, node_offset + 0x0CU + child * 4U);
            if (child_offset != 0U) visit(child_offset, group_index, child, depth + 1U);
        }
        active_nodes.erase(node_offset);
    };
    visit(root_node_offset, std::numeric_limits<uint32_t>::max(), 0U, 0U);
}

std::vector<ModelVisualNode> resolve_arm_linked_model_hierarchy(
    std::span<const std::byte> arm_bytes, std::size_t arm_root_offset,
    std::string_view arm_path,
    const std::unordered_map<uint32_t, std::vector<ModelVisualRecord>>& model_visuals,
    uint32_t& linked_runtime_count, uint32_t& deferred_attachment_count,
    uint32_t technology_level_index = 0U) {
    std::vector<ModelVisualNode> output;
    linked_runtime_count = 0U;
    deferred_attachment_count = 0U;
    if (arm_root_offset > arm_bytes.size() || 0x40U > arm_bytes.size() - arm_root_offset) return output;

    auto append_model = [&](const ModelVisualRecord& visual, uint32_t external_parent,
                            uint32_t external_slot,
                            const DeferredModelAttachment* root_override,
                            std::vector<DeferredModelAttachment>& deferred) {
        std::vector<uint32_t> remap(visual.hierarchy_nodes.size(),
            std::numeric_limits<uint32_t>::max());
        for (std::size_t local_index = 0; local_index < visual.hierarchy_nodes.size(); ++local_index) {
            const auto& source = visual.hierarchy_nodes[local_index];
            const bool root = source.parent_index == std::numeric_limits<uint32_t>::max();
            uint32_t mapped_parent = external_parent;
            if (!root) {
                if (source.parent_index >= remap.size()) continue;
                mapped_parent = remap[source.parent_index];
                if (mapped_parent == std::numeric_limits<uint32_t>::max()) continue;
            }
            if (!source.creates_runtime_node()) {
                deferred.push_back(DeferredModelAttachment{
                    mapped_parent, source.child_slot, source.local_rotation_angle0,
                    source.local_rotation_angle1, source.local_rotation_angle2});
                ++deferred_attachment_count;
                continue;
            }
            if (source.sprite_path.empty()) continue;
            ModelVisualNode node = source;
            node.parent_index = root ? external_parent : mapped_parent;
            node.child_slot = root ? external_slot : source.child_slot;
            if (root && root_override != nullptr) {
                node.local_rotation_angle0 = root_override->angle0;
                node.local_rotation_angle1 = root_override->angle1;
                node.local_rotation_angle2 = root_override->angle2;
            }
            remap[local_index] = static_cast<uint32_t>(output.size());
            output.push_back(std::move(node));
        }
    };

    std::unordered_set<std::size_t> active_arm_nodes;
    std::function<void(std::size_t, const DeferredModelAttachment*, bool)> append_arm_node;
    append_arm_node = [&](std::size_t arm_node_offset,
                          const DeferredModelAttachment* attachment,
                          bool linked_child) {
        if (arm_node_offset > arm_bytes.size() || 0x40U > arm_bytes.size() - arm_node_offset)
            return;
        if (!active_arm_nodes.insert(arm_node_offset).second)
            throw FormatError("ARM linked model hierarchy contains a cycle");
        uint32_t model_id = 0U;
        if (technology_level_index < 8U)
            model_id = read_u32(arm_bytes, arm_node_offset + 0x20U + technology_level_index * 4U);
        if (model_id == 0U) model_id = base_arm_model_definition_id(arm_bytes, arm_node_offset);
        const ModelVisualRecord* visual = select_model_visual_for_arm(model_visuals, model_id, arm_path);
        std::vector<DeferredModelAttachment> deferred;
        if (visual != nullptr) {
            const uint32_t parent = attachment == nullptr
                ? std::numeric_limits<uint32_t>::max() : attachment->parent_index;
            const uint32_t slot = attachment == nullptr ? 0U : attachment->child_slot;
            append_model(*visual, parent, slot, attachment, deferred);
            if (linked_child) ++linked_runtime_count;
        }

        const uint32_t child_count = read_u32(arm_bytes, arm_node_offset + 8U);
        if (child_count > 8U)
            throw FormatError("ARM linked model child count exceeds the serialized eight-slot array");
        if (arm_node_offset + 0x0CU + static_cast<std::size_t>(child_count) * 4U > arm_bytes.size())
            throw FormatError("ARM linked model child offsets exceed payload");
        const uint32_t resolvable = std::min<uint32_t>(child_count,
            static_cast<uint32_t>(deferred.size()));
        for (uint32_t child = 0U; child < resolvable; ++child) {
            const uint32_t child_offset = read_u32(arm_bytes, arm_node_offset + 0x0CU + child * 4U);
            if (child_offset == 0U) continue;
            append_arm_node(child_offset, &deferred[child], true);
        }
        active_arm_nodes.erase(arm_node_offset);
    };

    append_arm_node(arm_root_offset, nullptr, false);
    return output;
}

bool generic_runtime_registry_source(std::string_view path) {
    const std::string normalized = normalize_asset_path(path);
    return normalized == "arm/unit.arm" || normalized == "arm/building.arm";
}

bool starts_with_path(std::string_view path, std::string_view prefix) {
    if (prefix.empty() || path.size() < prefix.size() || !path.starts_with(prefix)) return false;
    return path.size() == prefix.size() || path[prefix.size()] == '/' || path[prefix.size()] == '.' ||
           (prefix.back() != '/' && std::isalnum(static_cast<unsigned char>(path[prefix.size()])) != 0);
}

std::filesystem::path find_case_insensitive(const std::filesystem::path& root, std::string_view name) {
    if (!std::filesystem::is_directory(root)) return {};
    const std::string key = lower_ascii(std::string(name));
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        if (item.is_regular_file() && lower_ascii(item.path().filename().string()) == key) return item.path();
    }
    return {};
}

std::string humanize_pck_identifier(std::string value) {
    for (char& character : value) {
        if (character == '_' || character == '-') character = ' ';
    }
    bool capitalize = true;
    for (char& character : value) {
        if (character == ' ') {
            capitalize = true;
        } else if (capitalize) {
            character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
            capitalize = false;
        }
    }
    return value;
}

std::string placement_category(std::string_view source_path) {
    const std::string path = lower_ascii(std::string(source_path));
    if (path.find("rohstoff") != std::string::npos) return "Resource";
    if (path.find("building.arm") != std::string::npos) return "Building";
    if (path.find("unit.arm") != std::string::npos) return "Unit";
    if (path.find("ruinen") != std::string::npos) return "Ruin / scenery";
    if (path.find("palmen") != std::string::npos) return "Palm";
    if (path.find("kaktus") != std::string::npos) return "Cactus";
    if (path.find("farne") != std::string::npos) return "Fern";
    if (path.find("busch") != std::string::npos) return "Bush";
    if (path.find("baum") != std::string::npos) return "Tree";
    if (path.find("stein") != std::string::npos) return "Rock";
    const std::string source_stem = stem_of(path);
    return source_stem.empty() ? "Placement" : humanize_pck_identifier(source_stem);
}

std::string normalize_localized_asset_name(std::string value) {
    std::string normalized;
    normalized.reserve(value.size());
    bool pending_space = false;
    for (std::size_t cursor = 0; cursor < value.size();) {
        const auto character = static_cast<unsigned char>(value[cursor]);
        // TextResource_Resolve returns the game's rich-text command stream. Command words in
        // the U+8000..U+80FF range are formatting/newline controls, not visible name glyphs.
        if (cursor + 2U < value.size() && character == 0xE8U) {
            const auto second = static_cast<unsigned char>(value[cursor + 1U]);
            const auto third = static_cast<unsigned char>(value[cursor + 2U]);
            if ((second & 0xC0U) == 0x80U && (third & 0xC0U) == 0x80U) {
                const uint32_t codepoint =
                    ((static_cast<uint32_t>(character & 0x0FU)) << 12U) |
                    ((static_cast<uint32_t>(second & 0x3FU)) << 6U) |
                    static_cast<uint32_t>(third & 0x3FU);
                if (codepoint >= 0x8000U && codepoint <= 0x80FFU) {
                    pending_space = !normalized.empty();
                    cursor += 3U;
                    continue;
                }
            }
        }
        if (character <= 0x20U || character == 0x7FU) {
            pending_space = !normalized.empty();
            ++cursor;
            continue;
        }
        if (pending_space) {
            normalized.push_back(' ');
            pending_space = false;
        }
        normalized.push_back(value[cursor++]);
    }
    while (!normalized.empty() && normalized.back() == ' ') normalized.pop_back();
    return normalized;
}

void assign_pck_placement_name(ArmyAssetPreset& preset) {
    if (preset.visual_kind == PlacementVisualKind::PlayerStart) {
        if (!preset.localized_name.empty()) {
            preset.display_name = preset.localized_name + " (player / AI start)";
            preset.short_name = preset.localized_name;
        } else {
            preset.display_name = "Player / AI start vehicle";
            preset.short_name = "Start vehicle";
        }
        return;
    }
    if (preset.browser_kind == PlacementBrowserKind::Unit ||
        preset.browser_kind == PlacementBrowserKind::Building) {
        if (!preset.localized_name.empty()) {
            preset.display_name = preset.localized_name;
            if (!preset.loadout_name.empty()) preset.display_name += " · " + preset.loadout_name;
            preset.short_name = preset.localized_name;
            return;
        }
        // Some stock MDL name-resource slots are intentionally empty. When the recursive ARM
        // technology/attachment chain supplies an exact non-generic loadout name, use that proven
        // research-string title instead of exposing an internal SPR leaf such as gvtrm0.
        if (!preset.loadout_name.empty() && preset.loadout_name != "Unarmed") {
            preset.display_name = preset.loadout_name;
            preset.short_name = preset.loadout_name;
            return;
        }
    }
    if (preset.visual_kind == PlacementVisualKind::PowerResource) {
        preset.display_name = "Energy resource (buildable, blue)";
        preset.short_name = "Energy resource";
        return;
    }
    if (preset.visual_kind == PlacementVisualKind::BuildingResource) {
        preset.display_name = "Building resource (buildable, gold)";
        preset.short_name = "Building resource";
        return;
    }
    if (preset.visual_kind == PlacementVisualKind::ResourceScenery) {
        const std::string visual_leaf = stem_of(preset.sprite_path);
        if (preset.registry_id >= 850U) {
            preset.display_name = "Terrain / effect element · " +
                (visual_leaf.empty() ? std::to_string(preset.registry_id) : visual_leaf);
            preset.short_name = visual_leaf.empty() ? "Terrain element" : visual_leaf;
        } else {
            preset.display_name = "Resource scenery (not buildable) · " +
                (visual_leaf.empty() ? std::to_string(preset.registry_id) : visual_leaf);
            preset.short_name = visual_leaf.empty() ? "Resource scenery" : visual_leaf;
        }
        return;
    }
    const std::string category = placement_category(preset.source_path);
    const std::string visual_leaf = stem_of(preset.sprite_path);
    if (!visual_leaf.empty()) {
        // Fallback for records outside the proven unit/building localized-name path.
        // The leaf remains the exact recursively joined ARM -> MDL -> SPR PCK identifier.
        preset.display_name = category + " · " + visual_leaf;
        preset.short_name = visual_leaf;
    } else {
        const std::string source_leaf = stem_of(preset.source_path);
        preset.display_name = category + " · " +
            (source_leaf.empty() ? std::string("unresolved") : source_leaf);
        preset.short_name = source_leaf.empty() ? std::string("unresolved") : source_leaf;
    }
    if (!preset.loadout_name.empty() &&
        preset.display_name.find(preset.loadout_name) == std::string::npos) {
        preset.display_name += " · " + preset.loadout_name;
    }
}

} // namespace

AssetKind classify_asset(std::string_view normalized_path) {
    const auto ext = extension_of(normalized_path);
    if (ext == "lev") return AssetKind::Level;
    if (ext == "fld") return AssetKind::Field;
    if (ext == "gfx") return AssetKind::Texture;
    if (ext == "pal") return AssetKind::Palette;
    if (ext == "arm") return AssetKind::Army;
    if (ext == "mdl" || ext == "spr") return AssetKind::Model;
    if (ext == "eff") return AssetKind::Effect;
    if (ext == "sht") return AssetKind::Shot;
    if (ext == "sam") return AssetKind::Sound;
    if (ext == "str") return AssetKind::Text;
    return AssetKind::Other;
}

const char* asset_kind_name(AssetKind kind) noexcept {
    switch (kind) {
        case AssetKind::Level: return "Level";
        case AssetKind::Field: return "Field";
        case AssetKind::Texture: return "Texture";
        case AssetKind::Palette: return "Palette";
        case AssetKind::Army: return "Army";
        case AssetKind::Model: return "Model";
        case AssetKind::Effect: return "Effect";
        case AssetKind::Shot: return "Shot";
        case AssetKind::Sound: return "Sound";
        case AssetKind::Text: return "Text";
        case AssetKind::Other: return "Other";
    }
    return "Other";
}

const char* placement_visual_kind_name(PlacementVisualKind kind) noexcept {
    switch (kind) {
        case PlacementVisualKind::Standard: return "Standard";
        case PlacementVisualKind::Tree: return "Tree";
        case PlacementVisualKind::Rock: return "Rock";
        case PlacementVisualKind::PlayerStart: return "Player / AI start vehicle";
        case PlacementVisualKind::PowerResource: return "Energy resource (buildable, blue)";
        case PlacementVisualKind::BuildingResource: return "Building resource (buildable, gold)";
        case PlacementVisualKind::ResourceScenery: return "Resource scenery / terrain element";
    }
    return "Standard";
}

const char* placement_browser_kind_name(PlacementBrowserKind kind) noexcept {
    switch (kind) {
        case PlacementBrowserKind::General: return "General";
        case PlacementBrowserKind::Element: return "Element";
        case PlacementBrowserKind::Unit: return "Unit";
        case PlacementBrowserKind::Building: return "Building";
    }
    return "Element";
}

bool placement_is_resource(const ArmyAssetPreset& preset) noexcept {
    return preset.visual_kind == PlacementVisualKind::PowerResource ||
           preset.visual_kind == PlacementVisualKind::BuildingResource;
}

bool placement_is_resource_scenery(const ArmyAssetPreset& preset) noexcept {
    return preset.visual_kind == PlacementVisualKind::ResourceScenery;
}

std::optional<uint32_t> resource_field_occupancy_class(uint32_t registry_id) noexcept {
    switch (registry_id) {
        case 814U:
        case 815U: return 0U; // building-material extractor ARM 330 requires 0x0800
        case 810U:
        case 811U: return 1U; // energy generator ARM 332 requires 0x1000
        default: return std::nullopt;
    }
}

std::size_t stamp_resource_field_masks(FieldGrid& field,
                                       std::span<const LevelPlacement> placements,
                                       float stock_radius_cells) {
    if (!std::isfinite(stock_radius_cells) || stock_radius_cells < 0.0F)
        throw FormatError("resource-field stamp radius must be finite and nonnegative");
    Brush brush;
    brush.radius_cells = stock_radius_cells;
    brush.strength = 1.0F;
    brush.shape = BrushShape::Square;
    brush.falloff = BrushFalloff::Constant;
    std::size_t changed = 0U;
    for (const auto& placement : placements) {
        const auto occupancy_class = resource_field_occupancy_class(placement.army_asset_id);
        if (!occupancy_class) continue;
        const auto sample = field.sample_terrain(placement.render_x(), placement.render_y());
        const float center_x = static_cast<float>(std::lround(sample.grid_x));
        const float center_y = static_cast<float>(std::lround(sample.grid_y));
        changed += field.paint_placement_occupancy_class(
            center_x, center_y, brush, *occupancy_class, true);
    }
    return changed;
}

uint32_t canonical_placement_registry_id(const ArmyAssetPreset& preset) noexcept {
    return preset.canonical_registry_id == 0U ? preset.registry_id : preset.canonical_registry_id;
}

PlacementBrowserKind classify_placement_browser_kind(const ArmyAssetPreset& preset) noexcept {
    if (preset.visual_kind == PlacementVisualKind::PlayerStart || placement_is_resource(preset))
        return PlacementBrowserKind::General;
    const std::string_view source = preset.source_path;
    const auto slash = source.find_last_of('/');
    const std::string_view leaf = slash == std::string_view::npos ? source : source.substr(slash + 1U);
    // ARM source families are package authority: unit.arm and building.arm contain the stock
    // runtime registries for movable units and structures. Everything else with a visual is an
    // editor element; nonvisual helper records remain explicit marker-only entries.
    if (leaf == "unit.arm") return PlacementBrowserKind::Unit;
    if (leaf == "building.arm") return PlacementBrowserKind::Building;
    return PlacementBrowserKind::Element;
}

bool placement_uses_neutral_source_color(PlacementVisualKind kind) noexcept {
    switch (kind) {
        case PlacementVisualKind::Tree:
        case PlacementVisualKind::Rock:
        case PlacementVisualKind::PowerResource:
        case PlacementVisualKind::BuildingResource:
        case PlacementVisualKind::ResourceScenery:
            return true;
        case PlacementVisualKind::Standard:
        case PlacementVisualKind::PlayerStart:
            return false;
    }
    return false;
}

bool placement_uses_neutral_source_color(const ArmyAssetPreset& preset) noexcept {
    if (preset.browser_kind == PlacementBrowserKind::Element || placement_is_resource(preset)) return true;
    return placement_uses_neutral_source_color(preset.visual_kind);
}

uint32_t default_faction_graphics_family(uint32_t faction_index) noexcept {
    // The stock frontend player/faction record supplies this selector to
    // ArmyRuntime_InitializePoolAndGraphicsCf before armyN.gfx/armyN.pal are loaded.
    // The standard scenario colour order is not the numeric faction index.
    constexpr std::array<uint32_t, 8> families{{0U, 1U, 3U, 4U, 2U, 5U, 6U, 7U}};
    return families[std::min<std::size_t>(faction_index, families.size() - 1U)];
}

const char* default_faction_color_name(uint32_t faction_index) noexcept {
    constexpr std::array<const char*, 8> names{{
        "Neutral", "Blue", "Red", "Green", "Yellow", "Orange", "Brown", "Purple"}};
    return names[std::min<std::size_t>(faction_index, names.size() - 1U)];
}

bool has_supported_game_executable(const std::filesystem::path& game_root) {
    return !find_case_insensitive(game_root, "game.exe").empty() ||
           !find_case_insensitive(game_root, "thandor.exe").empty();
}

AssetCatalog AssetCatalog::scan_game_root(const std::filesystem::path& game_root) {
    if (!std::filesystem::is_directory(game_root)) throw std::runtime_error("game root is not a directory");
    if (!has_supported_game_executable(game_root))
        throw std::runtime_error("game.exe or thandor.exe was not found in the selected root");

    AssetCatalog catalog;
    catalog.game_root_ = std::filesystem::canonical(game_root);
    std::vector<std::filesystem::path> archives;
    for (const auto& item : std::filesystem::directory_iterator(catalog.game_root_)) {
        if (!item.is_regular_file()) continue;
        const auto filename = lower_ascii(item.path().filename().string());
        if (item.path().extension() == ".PCK" || lower_ascii(item.path().extension().string()) == ".pck") {
            if (filename == "filme.pck") continue;
            archives.push_back(item.path());
        }
    }
    std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
        const auto al = lower_ascii(a.filename().string()), bl = lower_ascii(b.filename().string());
        const bool ap = al.starts_with("patch"), bp = bl.starts_with("patch");
        if (ap != bp) return ap > bp; // patch precedence mirrors the game loader.
        return al < bl;
    });

    for (const auto& path : archives) {
        auto archive = std::make_shared<PckArchive>(PckArchive::open(path));
        const auto archive_index = catalog.archives_.size();
        const auto archive_name = path.stem().string();
        for (const auto& entry : archive->entries()) {
            if (extension_of(entry.normalized_name) == "flm") continue;
            AssetRecord record{archive_name, entry.name, entry.normalized_name,
                               classify_asset(entry.normalized_name), archive_index, entry.index,
                               entry.unpacked_size};
            // First archive wins. Patch archives are sorted before stock archives.
            catalog.lookup_.try_emplace(record.normalized_path, catalog.assets_.size());
            catalog.assets_.push_back(std::move(record));
        }
        catalog.archives_.push_back(std::move(archive));
    }

    // Count direct placements across all mounted stock LEV files before classifying ARM records.
    // Scenery records that never occur as a direct LEV placement are often internal helpers,
    // generated variants, or records whose model chain is supplied only by another scenario.
    std::unordered_map<uint32_t, uint32_t> stock_placement_counts;
    for (const auto& asset : catalog.assets_) {
        if (asset.kind != AssetKind::Level) continue;
        try {
            const auto level = LevelAsset::parse(catalog.load(asset));
            for (const auto& placement : level.placements()) {
                ++stock_placement_counts[placement.army_asset_id];
            }
        } catch (const std::exception&) {
            // Strict validation reports malformed levels later.
        }
    }

    // MDL records use a variable-size record stream. The model definition ID is at +0x08,
    // and the first serialized UTF-16 spr\... path identifies the base visual resource.
    std::unordered_map<uint32_t, std::vector<ModelVisualRecord>> model_visuals;
    for (const auto& asset : catalog.assets_) {
        if (asset.kind != AssetKind::Model || extension_of(asset.normalized_path) != "mdl") continue;
        try {
            for (auto visual : parse_model_visual_records(catalog.load(asset), asset.normalized_path)) {
                model_visuals[visual.definition_id].push_back(std::move(visual));
            }
        } catch (const std::exception&) {
            // Keep malformed optional model resources visible while refusing to invent a visual mapping.
        }
    }

    // The executable loads texte\help.str as text page 0x18. Selection-detail rendering
    // resolves each model name as resource 0x18004F + the MDL record dword at +0x04.
    std::optional<TextResourceAsset> army_asset_name_text;
    std::string army_asset_name_archive;
    if (const auto* help_text_record = catalog.find("texte/help.str")) {
        try {
            army_asset_name_text = TextResourceAsset::parse(catalog.load(*help_text_record));
            army_asset_name_archive = help_text_record->archive_name;
        } catch (const std::exception&) {
            // Preserve deterministic SPR-derived fallback labels if an optional localized
            // text page is missing or malformed. Strict validation reports the bad STR.
        }
    }

    // The game loads the fixed 256-record technology table and pairs technology ID N with
    // texte/techno.str resources 0x300000 + N*2 (title) and +1 (detail). Preserve both the
    // machine fields and localized strings so the asset browser can name exact ARM/MDL variants.
    if (const auto* technology_record = catalog.find("engine/tech.tec")) {
        try {
            const auto technology_asset = TechnologyAsset::parse(catalog.load(*technology_record));
            catalog.technologies_.assign(technology_asset.records().begin(),
                                         technology_asset.records().end());
            if (const auto* technology_text_record = catalog.find("texte/techno.str")) {
                const auto technology_text = TextResourceAsset::parse(
                    catalog.load(*technology_text_record));
                for (auto& technology : catalog.technologies_) {
                    const uint32_t title_index = technology.technology_id * 2U;
                    if (title_index < technology_text.string_count()) {
                        technology.title = normalize_technology_text(
                            technology_text.string(title_index));
                    }
                    if (title_index + 1U < technology_text.string_count()) {
                        technology.detail = technology_text.string(title_index + 1U);
                    }
                }
            }
        } catch (const std::exception&) {
            catalog.technologies_.clear();
            // Validation and the G386 stock gate report malformed or missing TEC/STR authority.
        }
    }

    // ARM assets contain variable-size records. Each record begins with byteSize and
    // stores the runtime registry ID at +0x08; this is the exact ID persisted by LEV placements.
    for (const auto& asset : catalog.assets_) {
        if (asset.kind != AssetKind::Army) continue;
        try {
            const auto bytes = catalog.load(asset);
            if (bytes.size() < 0x200 || read_u32(bytes, 0) != fourcc("arm")) continue;
            const auto count = read_u32(bytes, 0xB0);
            std::size_t offset = 0x200;
            for (uint32_t record = 0; record < count; ++record) {
                if (offset + 0x10 > bytes.size()) throw FormatError("ARM record header exceeds payload");
                const auto byte_size = read_u32(bytes, offset);
                if (byte_size < 0x10 || offset + byte_size > bytes.size()) throw FormatError("ARM record size exceeds payload");
                ArmyAssetPreset preset;
                preset.registry_id = read_u32(bytes, offset + 8);
                preset.canonical_registry_id = preset.registry_id;
                preset.source_path = asset.normalized_path;
                preset.compatible_arm_paths.push_back(asset.normalized_path);
                const auto root_node_offset = static_cast<std::size_t>(read_u32(bytes, offset + 0x0C));
                if (root_node_offset + 0x24 <= bytes.size()) {
                    preset.model_definition_id = base_arm_model_definition_id(bytes, root_node_offset);
                    if (const auto found = model_visuals.find(preset.model_definition_id); found != model_visuals.end()) {
                        for (const auto& candidate : found->second) {
                            if (!candidate.source_path.empty())
                                preset.compatible_model_paths.push_back(candidate.source_path);
                        }
                        const ModelVisualRecord* selected = select_model_visual_for_arm(
                            model_visuals, preset.model_definition_id, asset.normalized_path);
                        if (selected != nullptr) {
                            const ModelVisualRecord& visual = *selected;
                            preset.model_source_path = visual.source_path;
                            preset.model_name_text_offset = visual.name_text_offset;
                            preset.has_model_name_text_offset = visual.has_name_text_offset;
                            preset.sprite_path = visual.sprite_path;
                            preset.model_hierarchy = visual.hierarchy_nodes;
                            preset.runtime_model_class_id = visual.runtime_class_id;
                            preset.shot_impact_class_index = visual.shot_impact_class_index;
                            preset.model_runtime_render_flags = visual.runtime_render_flags;
                            preset.casts_generated_shadow = visual.casts_generated_shadow;
                            preset.placement_occupancy_class = visual.placement_occupancy_class;
                            preset.traversal_low_band_index = visual.traversal_low_band_index;
                            preset.traversal_high_band_index = visual.traversal_high_band_index;
                            preset.has_placement_occupancy_class = visual.has_placement_occupancy_class;
                            preset.has_traversal_profile = visual.has_traversal_profile;
                            preset.primary_animated_subresource = visual.primary_animated_subresource;
                            preset.secondary_animated_subresource = visual.secondary_animated_subresource;
                            preset.has_primary_texture_animation = visual.has_primary_texture_animation;
                            preset.has_secondary_texture_animation = visual.has_secondary_texture_animation;
                            preset.articulated_root_height_q12 =
                                visual.simulation.articulated_root_height_q12;
                            preset.articulated_contact_interpolation_rate_q12 = visual.simulation.articulated_contact_interpolation_rate_q12;
                            preset.articulated_contact_lift_q12 =
                                visual.simulation.articulated_contact_lift_q12;
                            preset.timed_effect_id = visual.timed_effect_id;
                            preset.timed_effect_interval_ticks = visual.timed_effect_interval_ticks;
                            preset.timed_effect_random_ticks = visual.timed_effect_random_ticks;
                            preset.linked_technology_id = visual.linked_technology_id;
                            if (visual.linked_technology_id != 0U) {
                                preset.linked_technology_title = technology_title_at(
                                    catalog.technologies_, visual.linked_technology_id);
                            }
                            for (uint32_t slot = 0U; slot < visual.technology_panel_ids.size(); ++slot) {
                                const uint32_t technology_id = visual.technology_panel_ids[slot];
                                if (technology_id == 0U) continue;
                                TechnologyPanelEntry panel_entry;
                                panel_entry.serialized_index = slot + 1U;
                                panel_entry.direction_index =
                                    technology_panel_direction_from_serialized_index(
                                        panel_entry.serialized_index);
                                panel_entry.row_index =
                                    technology_panel_row_from_serialized_index(
                                        panel_entry.serialized_index);
                                panel_entry.technology_id = technology_id;
                                if (const auto* technology = technology_record_at(
                                        catalog.technologies_, technology_id); technology != nullptr) {
                                    panel_entry.category = technology->category;
                                    panel_entry.title = technology->title;
                                }
                                preset.technology_panel_entries.push_back(std::move(panel_entry));
                            }
                            collect_arm_model_variant_groups(preset, bytes, root_node_offset,
                                asset.normalized_path, model_visuals, catalog.technologies_);
                            preset.resolved_model_hierarchy = resolve_arm_linked_model_hierarchy(
                                bytes, root_node_offset, asset.normalized_path, model_visuals,
                                preset.linked_model_runtime_count,
                                preset.deferred_model_attachment_count);
                            if (preset.resolved_model_hierarchy.empty())
                                preset.resolved_model_hierarchy = preset.model_hierarchy;
                            const auto root_group = std::find_if(preset.model_variant_groups.begin(),
                                preset.model_variant_groups.end(), [](const ArmyModelVariantGroup& group) {
                                    return group.depth == 0U;
                                });
                            if (root_group != preset.model_variant_groups.end()) {
                                for (const auto& variant : root_group->variants) {
                                    ArmyTechnologyLevelPreview preview;
                                    preview.level_index = variant.variant_index;
                                    preview.root_model_definition_id = variant.model_definition_id;
                                    preview.technology_effect_id = variant.technology_unlock_id != 0U
                                        ? variant.technology_unlock_id : variant.linked_technology_id;
                                    preview.technology_effect_title = !variant.technology_title.empty()
                                        ? variant.technology_title : variant.linked_technology_title;
                                    if (preview.technology_effect_title.empty())
                                        preview.technology_effect_title = preview.level_index == 0U
                                            ? "Base model / no STR effect" : "No named STR effect";
                                    uint32_t preview_linked_count = 0U;
                                    uint32_t preview_deferred_count = 0U;
                                    preview.resolved_model_hierarchy = resolve_arm_linked_model_hierarchy(
                                        bytes, root_node_offset, asset.normalized_path, model_visuals,
                                        preview_linked_count, preview_deferred_count,
                                        preview.level_index);
                                    if (preview.resolved_model_hierarchy.empty() && preview.level_index == 0U)
                                        preview.resolved_model_hierarchy = preset.resolved_model_hierarchy;
                                    const auto topology_matches_base = [&]() {
                                        if (preview.resolved_model_hierarchy.size() !=
                                            preset.resolved_model_hierarchy.size()) return false;
                                        for (std::size_t node_index = 0;
                                             node_index < preview.resolved_model_hierarchy.size();
                                             ++node_index) {
                                            const auto& candidate = preview.resolved_model_hierarchy[node_index];
                                            const auto& base = preset.resolved_model_hierarchy[node_index];
                                            if (candidate.parent_index != base.parent_index ||
                                                candidate.child_slot != base.child_slot ||
                                                candidate.creates_runtime_node() != base.creates_runtime_node())
                                                return false;
                                        }
                                        return true;
                                    };
                                    if (!preview.resolved_model_hierarchy.empty() && topology_matches_base())
                                        preset.technology_level_previews.push_back(std::move(preview));
                                }
                            }
                        }
                    }
                }
                const auto lower = asset.normalized_path;
                if (lower.find("rohstoff") != std::string::npos) preset.group_hint = "Resources";
                else if (lower.find("ruinen") != std::string::npos || lower.find("baum") != std::string::npos || lower.find("busch") != std::string::npos || lower.find("farne") != std::string::npos || lower.find("kaktus") != std::string::npos) preset.group_hint = "Scenery / effects";
                else preset.group_hint = "Units / structures";
                if (byte_size >= 0x2CU) {
                    preset.child0_rotation_step_turn16 = read_i32(bytes, offset + 0x10U);
                    preset.child2_vertical_step_q12 = read_i32(bytes, offset + 0x14U);
                    if (preset.runtime_model_class_id == 2U)
                        preset.track_texture_travel_scale = preset.child2_vertical_step_q12;
                    preset.child1_rotation_step_turn16 = read_i32(bytes, offset + 0x1CU);
                    preset.child2_vertical_min_q12 = read_i32(bytes, offset + 0x24U);
                    preset.child2_vertical_max_q12 = read_i32(bytes, offset + 0x28U);
                    const auto has_live_root_child = [&](uint32_t slot) {
                        const auto& hierarchy = preset.resolved_model_hierarchy.empty()
                            ? preset.model_hierarchy : preset.resolved_model_hierarchy;
                        return std::any_of(hierarchy.begin(), hierarchy.end(),
                            [&](const ModelVisualNode& node) {
                                return node.parent_index == 0U && node.child_slot == slot &&
                                       node.creates_runtime_node() && !node.sprite_path.empty();
                            });
                    };
                    preset.runtime_uses_generic_model_subnode_animation =
                        preset.runtime_model_class_id == 4U ||
                        preset.runtime_model_class_id == 11U ||
                        preset.runtime_model_class_id == 13U ||
                        preset.runtime_model_class_id == 14U;
                    preset.runtime_uses_class17_movement_banking =
                        preset.runtime_model_class_id == 17U;
                    preset.has_model_subnode_animation =
                        preset.runtime_uses_generic_model_subnode_animation &&
                        ((preset.child0_rotation_step_turn16 != 0 && has_live_root_child(0U)) ||
                         (preset.child1_rotation_step_turn16 != 0 && has_live_root_child(1U)) ||
                         (preset.child2_vertical_step_q12 != 0 && has_live_root_child(2U)));
                    const auto& resolved_hierarchy = preset.resolved_model_hierarchy.empty()
                        ? preset.model_hierarchy : preset.resolved_model_hierarchy;
                    preset.has_continuous_radar_subnode_animation =
                        std::any_of(resolved_hierarchy.begin(), resolved_hierarchy.end(),
                            [&](const ModelVisualNode& parent_node) {
                                if (parent_node.runtime_class_id != 10U) return false;
                                const bool rotates_child0 =
                                    parent_node.simulation.yaw_max_velocity_turn16 != 0 &&
                                    std::any_of(resolved_hierarchy.begin(), resolved_hierarchy.end(),
                                        [&](const ModelVisualNode& child) {
                                            return child.parent_index ==
                                                static_cast<uint32_t>(&parent_node - resolved_hierarchy.data()) &&
                                                child.child_slot == 0U && child.creates_runtime_node();
                                        });
                                const bool rotates_child1 =
                                    parent_node.simulation.yaw_acceleration_turn16 != 0 &&
                                    std::any_of(resolved_hierarchy.begin(), resolved_hierarchy.end(),
                                        [&](const ModelVisualNode& child) {
                                            return child.parent_index ==
                                                static_cast<uint32_t>(&parent_node - resolved_hierarchy.data()) &&
                                                child.child_slot == 1U && child.creates_runtime_node();
                                        });
                                return rotates_child0 || rotates_child1;
                            });
                }
                if (const auto placement_count = stock_placement_counts.find(preset.registry_id);
                    placement_count != stock_placement_counts.end())
                    preset.stock_placement_count = placement_count->second;
                catalog.army_presets_.push_back(std::move(preset));
                offset += byte_size;
            }
        } catch (const std::exception&) {
            // Malformed optional placement preset does not hide the source asset.
        }
    }
    std::sort(catalog.army_presets_.begin(), catalog.army_presets_.end(), [](const auto& a, const auto& b) {
        if (a.registry_id != b.registry_id) return a.registry_id < b.registry_id;
        if (a.stock_placement_count != b.stock_placement_count)
            return a.stock_placement_count > b.stock_placement_count;
        if (generic_runtime_registry_source(a.source_path) != generic_runtime_registry_source(b.source_path))
            return generic_runtime_registry_source(a.source_path);
        const bool a_exact = normalize_asset_path(a.model_source_path) == expected_model_path_for_arm(a.source_path);
        const bool b_exact = normalize_asset_path(b.model_source_path) == expected_model_path_for_arm(b.source_path);
        if (a_exact != b_exact) return a_exact;
        return a.source_path < b.source_path;
    });
    std::vector<ArmyAssetPreset> merged_presets;
    for (std::size_t begin = 0; begin < catalog.army_presets_.size();) {
        std::size_t end = begin + 1U;
        while (end < catalog.army_presets_.size() &&
               catalog.army_presets_[end].registry_id == catalog.army_presets_[begin].registry_id) ++end;
        ArmyAssetPreset merged = catalog.army_presets_[begin];
        for (std::size_t index = begin; index < end; ++index) {
            const auto& variant = catalog.army_presets_[index];
            if (!variant.source_path.empty()) merged.compatible_arm_paths.push_back(variant.source_path);
            merged.compatible_arm_paths.insert(merged.compatible_arm_paths.end(),
                variant.compatible_arm_paths.begin(), variant.compatible_arm_paths.end());
            if (!variant.model_source_path.empty())
                merged.compatible_model_paths.push_back(variant.model_source_path);
            merged.compatible_model_paths.insert(merged.compatible_model_paths.end(),
                variant.compatible_model_paths.begin(), variant.compatible_model_paths.end());
        }
        const auto normalize_unique = [](std::vector<std::string>& paths) {
            for (auto& path : paths) path = normalize_asset_path(path);
            std::sort(paths.begin(), paths.end());
            paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        };
        normalize_unique(merged.compatible_arm_paths);
        normalize_unique(merged.compatible_model_paths);
        merged_presets.push_back(std::move(merged));
        begin = end;
    }
    catalog.army_presets_ = std::move(merged_presets);

    // Stock LEV asset 50 is a real class-18 movable ground unit used as the faction start
    // vehicle. The seven per-faction camera records are separate LEV header records at +0x200.
    //
    // Resource buildability is not a texture-color property. The executable's timed-emitter
    // update reads MDL +0x174/+0x178/+0x17C; only EFF 114 and 116 identify the active deposits.
    // Stock maps place the corresponding extractor buildings over IDs 810/811 and 814/815.
    // Use the dominant stock variants 811 and 815 for new placements while preserving aliases
    // already present in loaded maps. All other rohstoff.arm records are neutral scenery/effects.
    std::unordered_map<std::string, ResourceEnergyKind> resource_kind_cache;
    for (auto& preset : catalog.army_presets_) {
        if (preset.registry_id == 50U && preset.source_path == "arm/unit.arm") {
            preset.visual_kind = PlacementVisualKind::PlayerStart;
            preset.group_hint = "Faction start vehicle (camera start is stored separately in LEV)";
            continue;
        }
        if (preset.source_path != "arm/rohstoff.arm") continue;

        ResourceEnergyKind visual_energy = ResourceEnergyKind::None;
        if (!preset.sprite_path.empty()) {
            if (const auto found = resource_kind_cache.find(preset.sprite_path);
                found != resource_kind_cache.end()) {
                visual_energy = found->second;
            } else {
                try {
                    visual_energy = classify_resource_energy(
                        SpriteAsset::parse(catalog.load(preset.sprite_path)).mesh());
                } catch (const std::exception&) {
                    visual_energy = ResourceEnergyKind::None;
                }
                resource_kind_cache.emplace(preset.sprite_path, visual_energy);
            }
        }

        if (preset.timed_effect_id == 114U && visual_energy == ResourceEnergyKind::Power) {
            preset.visual_kind = PlacementVisualKind::PowerResource;
            preset.group_hint = "Active energy deposit · EFF 114 · extractor-compatible";
            preset.canonical_registry_id = 811U;
            preset.compatible_registry_ids = {810U, 811U};
            preset.browser_visible = preset.registry_id == 811U;
        } else if (preset.timed_effect_id == 116U &&
                   visual_energy == ResourceEnergyKind::Building) {
            preset.visual_kind = PlacementVisualKind::BuildingResource;
            preset.group_hint = "Active building-material deposit · EFF 116 · extractor-compatible";
            preset.canonical_registry_id = 815U;
            preset.compatible_registry_ids = {814U, 815U};
            preset.browser_visible = preset.registry_id == 815U;
        } else {
            preset.visual_kind = PlacementVisualKind::ResourceScenery;
            preset.group_hint = preset.registry_id >= 850U
                ? "Terrain / effect element from rohstoff.arm"
                : "Inert/depleted resource scenery · no EFF 114/116 emitter";
        }
    }
    for (auto& preset : catalog.army_presets_) {
        if (preset.visual_kind != PlacementVisualKind::Standard) continue;
        const std::string source = lower_ascii(preset.source_path);
        if (source.find("baum") != std::string::npos || source.find("palmen") != std::string::npos ||
            source.find("kaktus") != std::string::npos || source.find("farne") != std::string::npos ||
            source.find("busch") != std::string::npos) {
            preset.visual_kind = PlacementVisualKind::Tree;
            preset.group_hint = "Scenery - trees and vegetation";
        } else if (source.find("stein") != std::string::npos) {
            preset.visual_kind = PlacementVisualKind::Rock;
            preset.group_hint = "Scenery - rocks";
        }
    }
    for (auto& preset : catalog.army_presets_) {
        preset.browser_kind = classify_placement_browser_kind(preset);
        if (preset.browser_kind == PlacementBrowserKind::Unit) preset.group_hint = "Units";
        if (preset.browser_kind == PlacementBrowserKind::Building) preset.group_hint = "Buildings";
        if (preset.browser_kind == PlacementBrowserKind::Element && preset.group_hint == "Units / structures")
            preset.group_hint = "Elements / scenery";
    }

    // Only expose records whose game-facing dependency chain and placement semantics are proven.
    // Units/buildings from the stock runtime registries are constructible even when no stock map
    // pre-places that exact ID. Elements are more dangerous: many valid-looking ARM/MDL/SPR records
    // are internal variants and render nothing when serialized as a direct LEV object. Require at
    // least one stock direct placement for Elements, except for the separately proven start/deposit
    // records. Marker-only helpers and kulisse.arm remain visible only in the raw Files browser.
    std::unordered_map<std::string, bool> direct_visual_cache;
    for (auto& preset : catalog.army_presets_) {
        bool direct_visual = false;
        const auto sprite_is_visual = [&](const std::string& path) {
            if (path.empty() || catalog.find(path) == nullptr) return false;
            if (const auto cached = direct_visual_cache.find(path); cached != direct_visual_cache.end())
                return cached->second;
            bool visual = false;
            try {
                const auto sprite = SpriteAsset::parse(catalog.load(path));
                visual = !sprite.mesh().vertices.empty() && !sprite.mesh().indices.empty();
            } catch (const std::exception&) {
                visual = false;
            }
            direct_visual_cache.emplace(path, visual);
            return visual;
        };
        if (!preset.model_hierarchy.empty()) {
            direct_visual = std::any_of(preset.model_hierarchy.begin(), preset.model_hierarchy.end(),
                [&](const ModelVisualNode& node) { return node.creates_runtime_node() && sprite_is_visual(node.sprite_path); });
        } else {
            direct_visual = sprite_is_visual(preset.sprite_path);
        }
        preset.visual_chain_complete = !preset.model_source_path.empty() &&
                                       catalog.find(preset.model_source_path) != nullptr &&
                                       direct_visual;
        const bool special_proven = preset.visual_kind == PlacementVisualKind::PlayerStart ||
                                    placement_is_resource(preset);
        preset.direct_placement_proven = special_proven || preset.stock_placement_count != 0U;
        if (preset.browser_kind == PlacementBrowserKind::Unit ||
            preset.browser_kind == PlacementBrowserKind::Building) {
            preset.game_placeable = preset.visual_chain_complete &&
                (generic_runtime_registry_source(preset.source_path) || preset.direct_placement_proven);
        } else {
            preset.game_placeable = preset.visual_chain_complete && preset.direct_placement_proven &&
                                    normalize_asset_path(preset.source_path) != "arm/kulisse.arm";
        }
        preset.browser_visible = preset.browser_visible && preset.game_placeable;
    }

    // Propagate the same PCK-backed names used by the game into the unit/building browser.
    // English country code 44 is selected first by TextResourceAsset, matching the editor UI;
    // the parser falls back to the English block or the first available locale if necessary.
    if (army_asset_name_text.has_value()) {
        for (auto& preset : catalog.army_presets_) {
            const bool named_runtime_asset =
                preset.browser_kind == PlacementBrowserKind::Unit ||
                preset.browser_kind == PlacementBrowserKind::Building ||
                preset.visual_kind == PlacementVisualKind::PlayerStart;
            if (!named_runtime_asset || !preset.has_model_name_text_offset) continue;
            if (preset.model_name_text_offset >
                std::numeric_limits<uint32_t>::max() - kArmyAssetNameTextBaseIndex) continue;
            const uint32_t text_index = army_asset_name_text_index(preset.model_name_text_offset);
            preset.localized_name_source_path = "texte/help.str";
            preset.localized_name_source_archive = army_asset_name_archive;
            preset.name_text_resource_id =
                army_asset_name_text_resource_id(preset.model_name_text_offset);
            try {
                if (text_index >= army_asset_name_text->string_count()) continue;
                auto localized = normalize_localized_asset_name(army_asset_name_text->string(text_index));
                if (localized.empty()) continue;
                preset.localized_name = std::move(localized);
            } catch (const std::exception&) {
                // Keep this one asset on the exact SPR-derived fallback label.
            }
        }
    }

    // SHT definitions are fixed 0x2E0-byte records. Keep the definition IDs and effect references
    // available to the static preview so invisible timed EFF controllers can reproduce their
    // linked projectile/effect chain without pretending the empty controller SPR is visible.
    for (const auto& asset : catalog.assets_) {
        if (asset.kind != AssetKind::Shot) continue;
        try {
            const auto shot_asset = ShotAsset::parse(catalog.load(asset));
            for (const auto& definition : shot_asset.definitions()) {
                catalog.shot_definitions_.try_emplace(definition.definition_id, definition);
            }
        } catch (const std::exception&) {
            // Strict validation reports malformed SHT assets; discovery remains best-effort.
        }
    }

    // Recover editor-only simulation profiles from the exact MDL fields consumed by the game's
    // 20 Hz movement/aim/fire callbacks. The runtime does not aim and fire from one arbitrary
    // muzzle leaf: it smooths yaw on the highest node in one repeated-definition assembly, pitch
    // on the next node, and applies recoil/attachment transforms on the leaf. Parallel leaves
    // below the same pitch joint are alternating barrels of one channel. Runtime class 21 uses
    // the separate attachment-selector callback and therefore advances one kind-2 attachment at
    // a time. These distinctions are required for correct tower casing rotation and muzzle origin.
    for (auto& preset : catalog.army_presets_) {
        const auto& hierarchy = preset.resolved_model_hierarchy.empty()
            ? preset.model_hierarchy : preset.resolved_model_hierarchy;
        if (!hierarchy.empty()) {
            const auto root_node = std::find_if(hierarchy.begin(), hierarchy.end(),
                [](const ModelVisualNode& node) {
                    return node.parent_index == std::numeric_limits<uint32_t>::max();
                });
            if (root_node != hierarchy.end() &&
                preset.browser_kind == PlacementBrowserKind::Unit) {
                preset.simulation_motion_profile.speed_q12_per_tick =
                    root_node->simulation.class_step_q12;
                preset.simulation_motion_profile.yaw_max_velocity_turn16 =
                    root_node->simulation.yaw_max_velocity_turn16;
                preset.simulation_motion_profile.yaw_acceleration_turn16 =
                    root_node->simulation.yaw_acceleration_turn16;
                preset.simulation_motion_profile.placement_contact_kind =
                    root_node->simulation.placement_contact_kind;
            }
        }
        const auto direct_child_index = [&](uint32_t parent_index, uint32_t child_slot) {
            if (parent_index == std::numeric_limits<uint32_t>::max())
                return std::numeric_limits<uint32_t>::max();
            const auto found = std::find_if(hierarchy.begin(), hierarchy.end(),
                [&](const ModelVisualNode& node) {
                    return node.creates_runtime_node() && node.parent_index == parent_index &&
                           node.child_slot == child_slot;
                });
            return found == hierarchy.end() ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(std::distance(hierarchy.begin(), found));
        };
        for (std::size_t node_index = 0; node_index < hierarchy.size(); ++node_index) {
            const auto& launch_node = hierarchy[node_index];
            if (!launch_node.simulation.has_weapon_definition() || launch_node.sprite_path.empty())
                continue;
            const auto* shot = catalog.shot_definition(launch_node.simulation.shot_definition_id);
            if (shot == nullptr) continue;
            uint32_t launch_count = 0U;
            uint32_t launch_selector_mask = 0U;
            try {
                const auto sprite = SpriteAsset::parse(catalog.load(launch_node.sprite_path));
                for (const auto& attachment : sprite.lookup_records()) {
                    if (attachment.key_class().value() != 2U) continue;
                    ++launch_count;
                    if (attachment.key_index().value() < 32U)
                        launch_selector_mask |= 1U << attachment.key_index().value();
                }
            } catch (const std::exception&) {
                launch_count = 0U;
                launch_selector_mask = 0U;
            }
            if (launch_count == 0U) continue;

            std::vector<uint32_t> same_definition_chain{
                static_cast<uint32_t>(node_index)};
            uint32_t cursor = static_cast<uint32_t>(node_index);
            while (hierarchy[cursor].parent_index != std::numeric_limits<uint32_t>::max()) {
                const uint32_t parent = hierarchy[cursor].parent_index;
                if (parent >= hierarchy.size() ||
                    hierarchy[parent].model_definition_id != launch_node.model_definition_id)
                    break;
                same_definition_chain.push_back(parent);
                cursor = parent;
            }
            std::reverse(same_definition_chain.begin(), same_definition_chain.end());
            const uint32_t yaw_node_index = same_definition_chain.front();
            const uint32_t pitch_node_index = same_definition_chain.size() >= 2U
                ? same_definition_chain[1U] : yaw_node_index;
            const auto& weapon_simulation = hierarchy[yaw_node_index].simulation;

            const auto existing = std::find_if(preset.simulation_weapon_profiles.begin(),
                preset.simulation_weapon_profiles.end(), [&](const SimulationWeaponProfile& profile) {
                    return profile.yaw_model_node_index == yaw_node_index &&
                           profile.pitch_model_node_index == pitch_node_index &&
                           profile.model_definition_id == launch_node.model_definition_id &&
                           profile.shot_definition_id == launch_node.simulation.shot_definition_id;
                });
            if (existing != preset.simulation_weapon_profiles.end()) {
                existing->launch_model_node_indices.push_back(static_cast<uint32_t>(node_index));
                existing->launch_attachment_count += launch_count;
                existing->launch_attachment_selector_mask |= launch_selector_mask;
                existing->launch_policy = SimulationLaunchPolicy::AlternateLaunchNodes;
                continue;
            }

            SimulationWeaponProfile profile;
            profile.model_node_index = static_cast<uint32_t>(node_index);
            profile.yaw_model_node_index = yaw_node_index;
            profile.pitch_model_node_index = pitch_node_index;
            profile.aim_model_node_index = pitch_node_index;
            profile.launch_model_node_index = static_cast<uint32_t>(node_index);
            profile.launch_model_node_indices.push_back(static_cast<uint32_t>(node_index));
            profile.runtime_class_id = hierarchy[yaw_node_index].runtime_class_id;
            if (profile.runtime_class_id == 9U) {
                // The callback reads aim from root->child[0] and the eight selector bits from
                // root->child[0]->child[0], independently of the kind-2 launch-point owner.
                profile.aim_model_node_index = direct_child_index(yaw_node_index, 0U);
                profile.rack_mesh_model_node_index =
                    direct_child_index(profile.aim_model_node_index, 0U);
            }
            // RUNTIME_UPDATE[9] owns eight attachment-specific reload counters and one
            // MDL +0x38 inter-shot gate. It fires one free selector, hides that rocket mesh,
            // then restores the selector when its MDL +0x30 timer expires.
            profile.launch_policy = (profile.runtime_class_id == 9U ||
                                     preset.runtime_model_class_id == 21U)
                ? SimulationLaunchPolicy::SequentialKind2Attachments
                : SimulationLaunchPolicy::AllKind2Attachments;
            profile.model_definition_id = launch_node.model_definition_id;
            profile.shot_definition_id = launch_node.simulation.shot_definition_id;
            profile.reload_ticks = weapon_simulation.reload_ticks;
            profile.inter_shot_ticks = profile.runtime_class_id == 9U
                ? static_cast<uint32_t>(std::max(0, weapon_simulation.recoil_duration_ticks))
                : 0U;
            profile.maximum_range_q12 = shot_selection_range_q12(*shot);
            profile.yaw_max_velocity_turn16 = weapon_simulation.yaw_max_velocity_turn16;
            profile.yaw_acceleration_turn16 = weapon_simulation.yaw_acceleration_turn16;
            profile.pitch_max_velocity_turn16 = weapon_simulation.pitch_max_velocity_turn16;
            profile.pitch_acceleration_turn16 = weapon_simulation.pitch_acceleration_turn16;
            profile.pitch_min_turn16 = weapon_simulation.pitch_min_turn16;
            profile.pitch_max_turn16 = weapon_simulation.pitch_max_turn16;
            profile.recoil_rotation_step_turn16 = weapon_simulation.recoil_rotation_step_turn16;
            profile.recoil_duration_ticks = profile.runtime_class_id == 9U
                ? 0 : weapon_simulation.recoil_duration_ticks;
            profile.recoil_translation_q12 = weapon_simulation.recoil_translation_q12;
            profile.launch_attachment_count = launch_count;
            profile.launch_attachment_selector_mask = launch_selector_mask;
            if (profile.valid()) preset.simulation_weapon_profiles.push_back(std::move(profile));
        }
    }

    // EFF definitions use a fixed 0xC0-byte record stream and change the stored path to .spr
    // before loading the nested sprite. Join those definitions back to ARM -> MDL -> SPR presets
    // so the UI can expose related transition/destruction effects without auto-spawning inactive
    // runtime effects on a static LEV placement.
    std::unordered_map<std::string, std::vector<uint32_t>> effect_definition_ids_by_sprite;
    for (const auto& asset : catalog.assets_) {
        if (asset.kind != AssetKind::Effect) continue;
        try {
            const auto effect_asset = EffectAsset::parse(catalog.load(asset));
            for (const auto& definition : effect_asset.definitions()) {
                catalog.effect_definitions_.try_emplace(definition.definition_id, definition);
                effect_definition_ids_by_sprite[definition.sprite_path].push_back(definition.definition_id);
            }
        } catch (const std::exception&) {
            // Strict validation reports malformed EFF assets; catalog discovery remains best-effort.
        }
    }
    for (auto& [sprite, ids] : effect_definition_ids_by_sprite) {
        (void)sprite;
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
    // Build a read-only browser description for every exact ARM technology variant. This does
    // not guess a live faction state: it preserves all eight model alternatives, their MDL unlock
    // technology, kind-2 launch points, SHT trajectory, and EFF launch/impact references.
    for (auto& preset : catalog.army_presets_) {
        for (auto& group : preset.model_variant_groups) {
            for (auto& variant : group.variants) {
                const auto* visual = select_model_visual_for_arm(
                    model_visuals, variant.model_definition_id, preset.source_path);
                if (visual == nullptr) continue;
                std::unordered_map<uint32_t, WeaponEffectPipelineInfo> weapon_pipelines_by_shot_definition;
                for (const auto& model_node : visual->hierarchy_nodes) {
                    if (!model_node.simulation.has_weapon_definition() ||
                        model_node.sprite_path.empty()) continue;
                    const auto* shot = catalog.shot_definition(
                        model_node.simulation.shot_definition_id);
                    if (shot == nullptr) continue;
                    auto& pipeline = weapon_pipelines_by_shot_definition[shot->definition_id];
                    pipeline.model_definition_id = variant.model_definition_id;
                    pipeline.shot_definition_id = shot->definition_id;
                    pipeline.trajectory_kind = shot->trajectory_kind();
                    pipeline.shot_sprite_path = shot->sprite_path;
                    const auto effect_reference = [&](uint32_t effect_definition_id) {
                        EffectPipelineReferenceInfo reference;
                        reference.effect_definition_id = effect_definition_id;
                        if (const auto* definition = catalog.effect_definition(effect_definition_id);
                            definition != nullptr) {
                            reference.transition_kind = definition->lifecycle_transition_kind();
                            reference.sprite_path = definition->sprite_path;
                        }
                        return reference;
                    };
                    pipeline.primary_effect = effect_reference(shot->primary_effect_id);
                    pipeline.launch_effect = effect_reference(shot->launch_effect_id);
                    pipeline.secondary_effect = effect_reference(shot->secondary_effect_id);
                    try {
                        const auto sprite = SpriteAsset::parse(catalog.load(model_node.sprite_path));
                        for (const auto& attachment : sprite.lookup_records()) {
                            if (attachment.key_class().value() != 2U) continue;
                            ++pipeline.launch_attachment_count;
                            if (attachment.key_index().value() < 32U)
                                pipeline.launch_attachment_selector_mask |=
                                    1U << attachment.key_index().value();
                        }
                    } catch (const std::exception&) {
                        // The strict asset validator reports malformed SPR data. Keep the SHT path.
                    }
                    const auto append_unique_effect_reference = [&](
                        std::vector<EffectPipelineReferenceInfo>& references,
                        uint32_t effect_definition_id) {
                        if (effect_definition_id == 0U) return;
                        const auto duplicate = std::find_if(references.begin(), references.end(),
                            [&](const EffectPipelineReferenceInfo& reference) {
                                return reference.effect_definition_id == effect_definition_id;
                            });
                        if (duplicate == references.end())
                            references.push_back(effect_reference(effect_definition_id));
                    };
                    for (const uint32_t id : shot->terrain_impact_effect_ids)
                        append_unique_effect_reference(pipeline.terrain_impact_effects, id);
                    for (const uint32_t id : shot->target_impact_effect_ids)
                        append_unique_effect_reference(pipeline.target_class_impact_effects, id);
                }
                for (auto& [shot_id, pipeline] : weapon_pipelines_by_shot_definition) {
                    (void)shot_id;
                    const auto by_effect_id = [](const EffectPipelineReferenceInfo& left,
                                                  const EffectPipelineReferenceInfo& right) {
                        return left.effect_definition_id < right.effect_definition_id;
                    };
                    std::sort(pipeline.terrain_impact_effects.begin(),
                              pipeline.terrain_impact_effects.end(), by_effect_id);
                    std::sort(pipeline.target_class_impact_effects.begin(),
                              pipeline.target_class_impact_effects.end(), by_effect_id);
                    variant.weapon_pipelines.push_back(std::move(pipeline));
                }
                std::sort(variant.weapon_pipelines.begin(), variant.weapon_pipelines.end(),
                    [](const WeaponEffectPipelineInfo& left,
                       const WeaponEffectPipelineInfo& right) {
                        return left.shot_definition_id < right.shot_definition_id;
                    });
            }
        }

        std::vector<std::string> attachment_loadout_names;
        for (const auto& group : preset.model_variant_groups) {
            if (group.depth == 0U) continue;
            std::string attachment_branch_title;
            bool branch_has_weapon_pipeline = false;
            uint32_t basic_weapon_shot_definition_id = 0U;
            for (const auto& variant : group.variants) {
                branch_has_weapon_pipeline = branch_has_weapon_pipeline || !variant.weapon_pipelines.empty();
                if (basic_weapon_shot_definition_id == 0U && !variant.weapon_pipelines.empty())
                    basic_weapon_shot_definition_id = variant.weapon_pipelines.front().shot_definition_id;
                if (!variant.technology_title.empty() &&
                    lower_ascii(variant.technology_title) != "basic technology") {
                    attachment_branch_title = technology_family_title(variant.technology_title);
                    if (!attachment_branch_title.empty()) break;
                }
            }
            if (attachment_branch_title.empty() && branch_has_weapon_pipeline) {
                attachment_branch_title = "Basic weapon";
                if (basic_weapon_shot_definition_id != 0U)
                    attachment_branch_title += " (SHT " + std::to_string(basic_weapon_shot_definition_id) + ")";
            }
            if (!attachment_branch_title.empty() &&
                std::find(attachment_loadout_names.begin(), attachment_loadout_names.end(), attachment_branch_title) ==
                    attachment_loadout_names.end()) {
                attachment_loadout_names.push_back(std::move(attachment_branch_title));
            }
        }
        if (!attachment_loadout_names.empty()) {
            for (std::size_t index = 0U; index < attachment_loadout_names.size(); ++index) {
                if (index != 0U) preset.loadout_name += " + ";
                preset.loadout_name += attachment_loadout_names[index];
            }
        } else if (preset.browser_kind == PlacementBrowserKind::Unit &&
                   preset.visual_kind != PlacementVisualKind::PlayerStart) {
            preset.loadout_name = "Unarmed";
        }

        {
            std::array<bool, 7> populated_branches{};
            std::array<bool, 4> populated_rows{};
            if (!preset.linked_technology_title.empty()) {
                preset.technology_branch_names[0] =
                    technology_family_title(preset.linked_technology_title);
                populated_branches[0] = !preset.technology_branch_names[0].empty();
            }
            for (const auto& entry : preset.technology_panel_entries) {
                if (entry.direction_index < populated_branches.size()) {
                    populated_branches[entry.direction_index] = true;
                    if (preset.technology_branch_names[entry.direction_index].empty()) {
                        preset.technology_branch_names[entry.direction_index] =
                            technology_family_title(entry.title);
                    }
                }
                if (entry.row_index < populated_rows.size()) populated_rows[entry.row_index] = true;
            }
            preset.technology_branch_count = static_cast<uint32_t>(
                std::count(populated_branches.begin(), populated_branches.end(), true));
            const auto row_count = static_cast<uint32_t>(
                std::count(populated_rows.begin(), populated_rows.end(), true));
            if (!preset.technology_panel_entries.empty() || preset.linked_technology_id != 0U) {
                preset.technology_summary =
                    std::to_string(preset.technology_panel_entries.size()) +
                    " panel entries · " + std::to_string(preset.technology_branch_count) +
                    " branches · " + std::to_string(row_count) + " populated rows";
                if (preset.linked_technology_id != 0U) {
                    preset.technology_summary += " · linked TEC " +
                        std::to_string(preset.linked_technology_id);
                    if (!preset.linked_technology_title.empty())
                        preset.technology_summary += " " + preset.linked_technology_title;
                }
            }
        }

        for (const auto& group : preset.model_variant_groups) {
            for (const auto& variant : group.variants) {
                if (!variant.technology_title.empty())
                    preset.asset_search_text += " " + variant.technology_title;
                if (!variant.linked_technology_title.empty())
                    preset.asset_search_text += " " + variant.linked_technology_title;
                if (variant.linked_technology_id != 0U)
                    preset.asset_search_text += " linked tech " +
                        std::to_string(variant.linked_technology_id);
                preset.asset_search_text += " mdl " + std::to_string(variant.model_definition_id);
                for (const auto& pipeline : variant.weapon_pipelines) {
                    preset.asset_search_text += " sht " +
                        std::to_string(pipeline.shot_definition_id) + " " +
                        shot_trajectory_mode_name(pipeline.trajectory_kind) + " " +
                        pipeline.shot_sprite_path;
                    const auto append_effect_search = [&](const EffectPipelineReferenceInfo& effect) {
                        if (effect.effect_definition_id == 0U) return;
                        preset.asset_search_text += " eff " +
                            std::to_string(effect.effect_definition_id) + " " +
                            effect_lifecycle_transition_kind_name(effect.transition_kind) + " " +
                            effect.sprite_path;
                    };
                    append_effect_search(pipeline.primary_effect);
                    append_effect_search(pipeline.launch_effect);
                    append_effect_search(pipeline.secondary_effect);
                    for (const auto& effect : pipeline.terrain_impact_effects)
                        append_effect_search(effect);
                    for (const auto& effect : pipeline.target_class_impact_effects)
                        append_effect_search(effect);
                }
            }
        }
        if (!preset.linked_technology_title.empty())
            preset.asset_search_text += " " + preset.linked_technology_title;
        if (preset.linked_technology_id != 0U)
            preset.asset_search_text += " linked base tech " +
                std::to_string(preset.linked_technology_id);
        for (uint32_t branch = 0U; branch < preset.technology_branch_names.size(); ++branch) {
            if (!preset.technology_branch_names[branch].empty())
                preset.asset_search_text += " branch " + std::to_string(branch + 1U) + " " +
                    preset.technology_branch_names[branch];
        }
        for (const auto& entry : preset.technology_panel_entries)
            preset.asset_search_text += " " + entry.title + " tech " +
                std::to_string(entry.technology_id);

        if (const auto found = effect_definition_ids_by_sprite.find(preset.sprite_path);
            found != effect_definition_ids_by_sprite.end()) {
            preset.shared_sprite_effect_ids = found->second;
        }
        assign_pck_placement_name(preset);
    }

    std::set<std::size_t> map_owned;
    for (std::size_t i = 0; i < catalog.assets_.size(); ++i) {
        const auto& level_record = catalog.assets_[i];
        if (level_record.kind != AssetKind::Level) continue;
        try {
            const auto level = LevelAsset::parse(catalog.load(level_record));
            MapAssetGroup group;
            group.display_name = stem_of(level_record.normalized_path);
            const auto parent = parent_of(level_record.normalized_path);
            if (parent != "level" && parent.starts_with("level/")) {
                group.display_name = parent.substr(6) + " / " + group.display_name;
            }
            group.level_path = level_record.normalized_path;
            group.field_path = level_record.normalized_path.substr(0, level_record.normalized_path.size() - 3) + "fld";
            std::set<std::size_t> members{i};
            if (const auto field = catalog.find_asset_index(group.field_path)) members.insert(*field);
            const auto& paths = level.paths();
            const std::array<std::string, 7> texture_bases{
                paths.ground_textures, paths.surface_textures, paths.sky_textures,
                paths.army_textures, paths.shot_textures, paths.effect_textures, paths.sounds
            };
            for (std::size_t candidate = 0; candidate < catalog.assets_.size(); ++candidate) {
                const auto& asset = catalog.assets_[candidate];
                bool matched = parent_of(asset.normalized_path) == parent;
                if (!matched) for (const auto& base : texture_bases) {
                    if (!base.empty() && starts_with_path(asset.normalized_path, normalize_asset_path(base))) { matched = true; break; }
                }
                if (!matched) for (const auto& resource : level.resource_paths()) {
                    if (starts_with_path(asset.normalized_path, normalize_asset_path(resource))) { matched = true; break; }
                }
                if (matched) members.insert(candidate);
            }
            group.asset_indices.assign(members.begin(), members.end());
            map_owned.insert(members.begin(), members.end());
            catalog.map_groups_.push_back(std::move(group));
        } catch (const std::exception&) {
            // Keep a malformed level visible in General; do not let one bad asset block the editor.
        }
    }
    std::sort(catalog.map_groups_.begin(), catalog.map_groups_.end(), [](const auto& a, const auto& b) { return a.display_name < b.display_name; });
    for (const auto& preset : catalog.army_presets_) {
        bool assigned = false;
        const auto source_index = catalog.find_asset_index(preset.source_path);
        if (source_index) {
            for (auto& group : catalog.map_groups_) {
                if (std::binary_search(group.asset_indices.begin(), group.asset_indices.end(), *source_index)) {
                    group.placement_registry_ids.push_back(preset.registry_id);
                    assigned = true;
                }
            }
        }
        if (!assigned) catalog.general_placement_ids_.push_back(preset.registry_id);
    }
    for (auto& group : catalog.map_groups_) {
        std::sort(group.placement_registry_ids.begin(), group.placement_registry_ids.end());
        group.placement_registry_ids.erase(std::unique(group.placement_registry_ids.begin(), group.placement_registry_ids.end()), group.placement_registry_ids.end());
    }
    std::sort(catalog.general_placement_ids_.begin(), catalog.general_placement_ids_.end());
    catalog.general_placement_ids_.erase(std::unique(catalog.general_placement_ids_.begin(), catalog.general_placement_ids_.end()), catalog.general_placement_ids_.end());
    for (std::size_t i = 0; i < catalog.assets_.size(); ++i) if (!map_owned.contains(i)) catalog.general_assets_.push_back(i);
    return catalog;
}

const AssetRecord* AssetCatalog::find(std::string_view normalized_path) const {
    const auto it = lookup_.find(normalize_asset_path(normalized_path));
    return it == lookup_.end() ? nullptr : &assets_[it->second];
}

std::optional<std::size_t> AssetCatalog::find_asset_index(std::string_view normalized_path) const {
    const auto it = lookup_.find(normalize_asset_path(normalized_path));
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

std::vector<std::byte> AssetCatalog::load(const AssetRecord& record) const {
    if (record.archive_index >= archives_.size()) throw FormatError("catalog archive index is invalid");
    const auto& archive = *archives_[record.archive_index];
    if (record.entry_index >= archive.entries().size()) throw FormatError("catalog entry index is invalid");
    return archive.extract(archive.entries()[record.entry_index]);
}

std::vector<std::byte> AssetCatalog::load(std::string_view normalized_path) const {
    const auto* record = find(normalized_path);
    if (!record) throw FormatError("asset not found: " + std::string(normalized_path));
    return load(*record);
}

const ArmyAssetPreset* AssetCatalog::army_preset(uint32_t registry_id) const noexcept {
    const auto found = std::lower_bound(army_presets_.begin(), army_presets_.end(), registry_id,
        [](const ArmyAssetPreset& preset, uint32_t value) { return preset.registry_id < value; });
    return found != army_presets_.end() && found->registry_id == registry_id ? &*found : nullptr;
}

const EffectDefinitionRecord* AssetCatalog::effect_definition(uint32_t definition_id) const noexcept {
    const auto found = effect_definitions_.find(definition_id);
    return found == effect_definitions_.end() ? nullptr : &found->second;
}

const ShotDefinitionRecord* AssetCatalog::shot_definition(uint32_t definition_id) const noexcept {
    const auto found = shot_definitions_.find(definition_id);
    return found == shot_definitions_.end() ? nullptr : &found->second;
}

const TechnologyRecord* AssetCatalog::technology(uint32_t technology_id) const noexcept {
    return technology_id < technologies_.size() ? &technologies_[technology_id] : nullptr;
}

bool AssetCatalog::placement_available_in_level(const ArmyAssetPreset& preset,
                                                const LevelAsset& level) const noexcept {
    if (!preset.game_placeable || !preset.visual_chain_complete) return false;
    const auto contains_any_base = [&](const std::vector<std::string>& alternatives,
                                       std::string_view fallback) {
        const auto matches = [&](std::string_view candidate) {
            const std::string required = asset_base_path(candidate);
            if (required.empty()) return false;
            return std::any_of(level.resource_paths().begin(), level.resource_paths().end(),
                [&](const std::string& resource) { return asset_base_path(resource) == required; });
        };
        if (alternatives.empty()) return matches(fallback);
        return std::any_of(alternatives.begin(), alternatives.end(), matches);
    };
    return contains_any_base(preset.compatible_arm_paths, preset.source_path) &&
           contains_any_base(preset.compatible_model_paths, preset.model_source_path);
}

std::string AssetCatalog::placement_compatibility_diagnostic(const ArmyAssetPreset& preset,
                                                             const LevelAsset& level) const {
    if (!preset.visual_chain_complete)
        return "ARM -> MDL -> SPR dependency chain is incomplete";
    if (!preset.game_placeable)
        return "raw/internal record is not proven as a direct game LEV placement";
    const auto contains_any_base = [&](const std::vector<std::string>& alternatives,
                                       std::string_view fallback) {
        const auto matches = [&](std::string_view candidate) {
            const std::string required = asset_base_path(candidate);
            return std::any_of(level.resource_paths().begin(), level.resource_paths().end(),
                [&](const std::string& resource) { return asset_base_path(resource) == required; });
        };
        if (alternatives.empty()) return matches(fallback);
        return std::any_of(alternatives.begin(), alternatives.end(), matches);
    };
    if (!contains_any_base(preset.compatible_arm_paths, preset.source_path))
        return "current LEV does not load any ARM family that defines this registry ID";
    if (!contains_any_base(preset.compatible_model_paths, preset.model_source_path))
        return "current LEV does not load any MDL family required by this registry ID";
    return {};
}

std::vector<PlacementCompatibilityIssue> AssetCatalog::validate_level_placements(
    const LevelAsset& level) const {
    std::vector<PlacementCompatibilityIssue> issues;
    const auto placements = level.placements();
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto& placement = placements[index];
        const auto* preset = army_preset(placement.army_asset_id);
        if (preset == nullptr) {
            issues.push_back({index, placement.army_asset_id,
                "ARM registry ID is absent from the mounted catalog"});
            continue;
        }
        const std::string diagnostic = placement_compatibility_diagnostic(*preset, level);
        if (!diagnostic.empty()) issues.push_back({index, placement.army_asset_id, diagnostic});
    }
    return issues;
}


AssetValidationReport AssetCatalog::validate_supported_assets() const {
    AssetValidationReport report;
    report.catalog_entries = assets_.size();
    for (const auto& asset : assets_) {
        try {
            const auto bytes = load(asset);
            ++report.decoded_entries;
            const auto extension = extension_of(asset.normalized_path);
            switch (asset.kind) {
                case AssetKind::Level:
                    (void)LevelAsset::parse(bytes); ++report.level_files; break;
                case AssetKind::Field:
                    (void)FieldGrid::parse(bytes); ++report.field_files; break;
                case AssetKind::Texture:
                    (void)GfxAsset::parse(bytes); ++report.texture_files; break;
                case AssetKind::Palette:
                    (void)PalAsset::parse(bytes); ++report.palette_files; break;
                case AssetKind::Army: {
                    if (bytes.size() < 0x200 || read_u32(bytes, 0) != fourcc("arm")) throw FormatError("ARM magic mismatch");
                    const auto count = read_u32(bytes, 0xB0);
                    std::size_t offset = 0x200;
                    for (uint32_t record = 0; record < count; ++record) {
                        if (offset + 0x10 > bytes.size()) throw FormatError("ARM record header exceeds payload");
                        const auto size = read_u32(bytes, offset);
                        if (size < 0x10 || offset + size > bytes.size()) throw FormatError("ARM record exceeds payload");
                        const auto root = static_cast<std::size_t>(read_u32(bytes, offset + 0x0C));
                        if (root != 0 && root + 0x24 > bytes.size()) throw FormatError("ARM root node exceeds payload");
                        offset += size;
                    }
                    ++report.army_files;
                    break;
                }
                case AssetKind::Model:
                    if (extension == "mdl") (void)parse_model_visual_records(bytes);
                    else if (extension == "spr") (void)SpriteAsset::parse(bytes);
                    ++report.model_files;
                    break;
                case AssetKind::Effect: {
                    const auto effect = EffectAsset::parse(bytes);
                    ++report.effect_files;
                    report.effect_definitions += effect.definitions().size();
                    break;
                }
                default:
                    break;
            }
        } catch (const std::exception& error) {
            report.issues.push_back({asset.normalized_path, error.what()});
        }
    }
    return report;
}

} // namespace thandor
