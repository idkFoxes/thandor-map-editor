#include "thandor/map_document.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace thandor {
namespace {
constexpr std::string_view kProjectManifest = "editor/project.txt";
constexpr std::string_view kProjectMagic = "THANDOR_MAP_EDITOR_PROJECT_V2";
constexpr std::string_view kProjectMagicV1 = "THANDOR_MAP_EDITOR_PROJECT_V1";
constexpr std::string_view kScenarioEntry = "editor/scenario_record.dat";

std::string stem_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    const auto dot = path.find_last_of('.');
    const auto begin = slash == std::string_view::npos ? 0 : slash + 1;
    const auto end = dot == std::string_view::npos || dot < begin ? path.size() : dot;
    return std::string(path.substr(begin, end - begin));
}

std::string percent_encode(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '/' || c == ' ') {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hex[c >> 4U]);
            result.push_back(hex[c & 15U]);
        }
    }
    return result;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

std::string percent_decode(std::string_view value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hex_value(value[i + 1]);
            const int low = hex_value(value[i + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i]);
    }
    return result;
}

std::vector<std::byte> text_bytes_padded(std::string text) {
    while ((text.size() & 3U) != 0U) text.push_back('\0');
    std::vector<std::byte> result(text.size());
    std::transform(text.begin(), text.end(), result.begin(), [](char c) {
        return static_cast<std::byte>(static_cast<unsigned char>(c));
    });
    return result;
}

std::string bytes_text(std::span<const std::byte> bytes) {
    std::string result;
    result.reserve(bytes.size());
    for (const auto value : bytes) {
        const char c = static_cast<char>(std::to_integer<unsigned char>(value));
        if (c == '\0') break;
        result.push_back(c);
    }
    return result;
}

uint32_t parse_manifest_u32(std::string_view value, std::string_view field_name) {
    uint32_t parsed = 0U;
    const char* begin = value.data();
    const char* end = value.data() + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end)
        throw FormatError(std::string("invalid unsigned project-manifest value for ") +
                          std::string(field_name));
    return parsed;
}

std::unordered_map<std::string, std::string> parse_manifest(std::string_view text) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream stream{std::string(text)};
    std::string line;
    if (!std::getline(stream, line) || (line != kProjectMagic && line != kProjectMagicV1)) {
        throw FormatError("unsupported map-editor project manifest");
    }
    while (std::getline(stream, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        result.emplace(line.substr(0, equals), percent_decode(line.substr(equals + 1)));
    }
    return result;
}

PlacementRole role_for(const AssetCatalog& catalog, const LevelPlacement& placement) {
    PlacementRole role = PlacementRole::Generic;
    if (const auto* preset = catalog.army_preset(placement.army_asset_id)) {
        switch (preset->visual_kind) {
            case PlacementVisualKind::PlayerStart: role = PlacementRole::PlayerStart; break;
            case PlacementVisualKind::PowerResource:
            case PlacementVisualKind::BuildingResource: role = PlacementRole::Resource; break;
            case PlacementVisualKind::Tree:
            case PlacementVisualKind::Rock:
            case PlacementVisualKind::ResourceScenery:
                role = PlacementRole::EffectOrScenery;
                break;
            case PlacementVisualKind::Standard:
                if (preset->browser_kind == PlacementBrowserKind::Element) {
                    role = PlacementRole::EffectOrScenery;
                }
                break;
        }
    }
    return role;
}

std::string placement_name_for(const AssetCatalog& catalog, const LevelPlacement& placement) {
    if (const auto* preset = catalog.army_preset(placement.army_asset_id)) return preset->display_name;
    return "ARM " + std::to_string(placement.army_asset_id);
}

PlayableFactionSummary analyze_playable_factions(
    std::span<const LevelPlacement> placements, std::span<const PlacementTag> tags = {}) {
    PlayableFactionSummary summary;
    bool unsupported_index = false;
    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto faction = placements[index].faction_index;
        if (faction == 0U) continue;
        ++summary.nonneutral_placement_count;
        if (faction > 7U) {
            unsupported_index = true;
            summary.highest_faction_index = std::max(summary.highest_faction_index, faction);
            continue;
        }
        summary.faction_mask |= 1U << faction;
        summary.highest_faction_index = std::max(summary.highest_faction_index, faction);
        if (index < tags.size() && tags[index].role == PlacementRole::PlayerStart) {
            ++summary.player_start_count;
        }
    }
    summary.player_count = summary.highest_faction_index;
    if (unsupported_index) {
        summary.diagnostic = "faction indices above 7 are not representable by the game's seven-row task-assignment UI";
        return summary;
    }
    if (summary.player_count == 0U) {
        summary.diagnostic = "no faction-owned placements exist; add at least factions 1 and 2 before game export";
        return summary;
    }
    uint32_t expected_mask = 0U;
    for (uint32_t faction = 1U; faction <= summary.player_count; ++faction) {
        expected_mask |= 1U << faction;
    }
    summary.contiguous = summary.faction_mask == expected_mask;
    if (!summary.contiguous) {
        std::ostringstream message;
        message << "faction indices must be contiguous from 1 through " << summary.player_count
                << "; the game initializes rows by count rather than by sparse faction ID";
        summary.diagnostic = message.str();
        return summary;
    }
    if (summary.player_count < 2U) {
        summary.diagnostic = "custom skirmish export requires at least two active factions";
        return summary;
    }
    summary.exportable = true;
    summary.diagnostic = "player/faction metadata can be derived from contiguous faction-owned placements";
    return summary;
}

std::string level_string_path(std::string_view map_name) {
    return "level/" + std::string(map_name) + ".str";
}

std::string scenario_fragment_path(unsigned slot) {
    std::ostringstream path;
    path << "level/level" << std::setfill('0') << std::setw(2) << slot << ".dat";
    return path.str();
}

std::filesystem::path find_case_insensitive_leaf(const std::filesystem::path& root,
                                                 std::string_view wanted) {
    if (!std::filesystem::is_directory(root)) return {};
    const std::string key = lower_ascii(std::string(wanted));
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        if (item.is_regular_file() && lower_ascii(item.path().filename().string()) == key) {
            return item.path();
        }
    }
    return {};
}

std::vector<std::filesystem::path> existing_numbered_archives(
    const std::filesystem::path& game_root, std::string_view prefix) {
    std::vector<std::filesystem::path> result;
    for (int slot = 99; slot >= 0; --slot) {
        std::ostringstream leaf;
        leaf << prefix << std::setfill('0') << std::setw(2) << slot << ".pck";
        const auto path = find_case_insensitive_leaf(game_root, leaf.str());
        if (!path.empty()) result.push_back(path);
    }
    return result;
}

std::vector<std::filesystem::path> resource_mount_order(const std::filesystem::path& game_root) {
    auto mounts = existing_numbered_archives(game_root, "patch");
    const auto levels = existing_numbered_archives(game_root, "level");
    mounts.insert(mounts.end(), levels.begin(), levels.end());
    for (const auto* leaf : {"daten.pck", "modelle.pck", "graphik.pck", "sound.pck",
                             "filme.pck", "level.pck", "engine.pck"}) {
        const auto path = find_case_insensitive_leaf(game_root, leaf);
        if (!path.empty()) mounts.push_back(path);
    }
    return mounts;
}

bool effective_resource_exists(const std::filesystem::path& game_root,
                               std::string_view normalized_path) {
    for (const auto& mount : resource_mount_order(game_root)) {
        try {
            const auto archive = PckArchive::open(mount);
            if (archive.find_exact(canonical_pck_entry_path(normalized_path)) != nullptr) return true;
        } catch (const FormatError&) {
            // The game skips an archive that it cannot mount. Continue with the remaining order.
        }
    }
    return false;
}

std::vector<std::byte> select_header_template(const std::filesystem::path& game_root,
                                              const std::filesystem::path& preferred) {
    if (!preferred.empty() && std::filesystem::exists(preferred)) {
        const auto archive = PckArchive::open(preferred);
        return {archive.header_bytes().begin(), archive.header_bytes().end()};
    }
    for (const auto candidate : {"level.pck", "level00.pck", "patch00.pck"}) {
        const auto path = find_case_insensitive_leaf(game_root, candidate);
        if (!path.empty()) {
            const auto archive = PckArchive::open(path);
            return {archive.header_bytes().begin(), archive.header_bytes().end()};
        }
    }
    return {};
}

std::vector<PckWriteEntry> merge_pck_entries(const PckArchive& existing,
                                             std::span<const PckWriteEntry> replacements) {
    std::unordered_map<std::string, std::size_t> replacement_by_path;
    replacement_by_path.reserve(replacements.size());
    for (std::size_t index = 0; index < replacements.size(); ++index) {
        const auto key = normalize_asset_path(replacements[index].name);
        if (!replacement_by_path.emplace(key, index).second) {
            throw FormatError("duplicate PATCH00 replacement path: " + key);
        }
    }
    std::unordered_set<std::size_t> consumed;
    std::vector<PckWriteEntry> merged;
    merged.reserve(existing.entries().size() + replacements.size());
    for (const auto& entry : existing.entries()) {
        const auto found = replacement_by_path.find(entry.normalized_name);
        if (found == replacement_by_path.end()) {
            merged.push_back({entry.name, existing.extract(entry), entry.type_tag});
        } else {
            merged.push_back(replacements[found->second]);
            consumed.insert(found->second);
        }
    }
    for (std::size_t index = 0; index < replacements.size(); ++index) {
        if (!consumed.contains(index)) merged.push_back(replacements[index]);
    }
    return merged;
}

void backup_existing_file(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    auto backup = path;
    backup += ".map-editor-backup";
    unsigned suffix = 1;
    while (std::filesystem::exists(backup)) {
        backup = path;
        backup += ".map-editor-backup." + std::to_string(suffix++);
    }
    std::filesystem::copy_file(path, backup);
}

std::optional<std::vector<std::byte>> read_optional_file(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return std::nullopt;
    return read_file(path);
}

void restore_optional_file(const std::filesystem::path& path,
                           const std::optional<std::vector<std::byte>>& original) {
    if (original.has_value()) write_file_atomic(path, *original);
    else {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
}

std::unordered_set<std::string> owned_game_package_paths(
    unsigned slot, std::string_view map_name,
    const std::map<std::string, std::vector<std::byte>>& custom_assets) {
    std::unordered_set<std::string> result;
    result.emplace(normalize_asset_path(scenario_fragment_path(slot)));
    const std::string base = "level/" + std::string(map_name);
    result.emplace(normalize_asset_path(base + ".lev"));
    result.emplace(normalize_asset_path(base + ".fld"));
    result.emplace(normalize_asset_path(base + ".str"));
    for (const auto& [path, data] : custom_assets) {
        (void)data;
        result.emplace(normalize_asset_path(path));
    }
    return result;
}

std::vector<PckWriteEntry> remove_pck_paths(
    const PckArchive& existing, const std::unordered_set<std::string>& removed,
    std::size_t& removed_count) {
    std::vector<PckWriteEntry> kept;
    kept.reserve(existing.entries().size());
    removed_count = 0;
    for (const auto& entry : existing.entries()) {
        if (removed.contains(entry.normalized_name)) {
            ++removed_count;
            continue;
        }
        kept.push_back({entry.name, existing.extract(entry), entry.type_tag});
    }
    return kept;
}

bool ends_with_ascii(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() &&
           value.substr(value.size() - suffix.size()) == suffix;
}

bool is_root_level_map_chain_entry(std::string_view name) {
    const std::string normalized = normalize_asset_path(name);
    if (!normalized.starts_with("level/")) return false;
    if (normalized.find('/', 6U) != std::string::npos) return false;
    return ends_with_ascii(normalized, ".lev") || ends_with_ascii(normalized, ".fld") ||
           ends_with_ascii(normalized, ".str") || ends_with_ascii(normalized, ".dat");
}

struct ValidatedNumberedLevelPackage {
    std::filesystem::path path;
    std::string level_entry;
    std::string text_entry;
    uint32_t alias_base{};
    TextResourceAsset text;
};

std::optional<ValidatedNumberedLevelPackage> validate_numbered_package_shape(
    const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return std::nullopt;
    const auto archive = PckArchive::open(path);
    std::vector<std::string> levels;
    std::vector<std::string> texts;
    for (const auto& entry : archive.entries()) {
        if (entry.name.find('/') != std::string::npos) return std::nullopt;
        if (entry.name.starts_with("level\\") && ends_with_ascii(entry.name, ".lev")) {
            levels.push_back(entry.name);
        }
        if (entry.name.starts_with("level\\") && ends_with_ascii(entry.name, ".str")) {
            texts.push_back(entry.name);
        }
    }
    if (levels.empty() || texts.empty()) return std::nullopt;
    std::sort(levels.begin(), levels.end());
    std::sort(texts.begin(), texts.end());
    try {
        const auto level_bytes = archive.extract(*archive.find_exact(levels.front()));
        const auto level = LevelAsset::parse(level_bytes);
        auto text = TextResourceAsset::parse(archive.extract(*archive.find_exact(texts.front())));
        for (uint32_t index = 0; index < 16U; ++index) (void)text.string(index);
        return ValidatedNumberedLevelPackage{path, levels.front(), texts.front(),
                                             level.compatibility_alias_base(), std::move(text)};
    } catch (const FormatError&) {
        return std::nullopt;
    }
}

const PckEntry* exact_entry_or_null(const PckArchive& archive, std::string_view normalized) {
    return archive.find_exact(canonical_pck_entry_path(normalized));
}

TextResourceAsset load_effective_text_page(const std::filesystem::path& game_root,
                                               std::string_view normalized_path) {
    const auto mounts = resource_mount_order(game_root);
    for (const auto& mount : mounts) {
        const auto archive = PckArchive::open(mount);
        if (const auto* entry = exact_entry_or_null(archive, normalized_path)) {
            return TextResourceAsset::parse(archive.extract(*entry));
        }
    }
    throw FormatError("game root has no effective text page: " + std::string(normalized_path));
}

TextResourceAsset load_effective_level_ui_text_page(const std::filesystem::path& game_root) {
    return load_effective_text_page(game_root, "texte/level.str");
}

TextResourceAsset load_effective_level_content_text_page(const std::filesystem::path& game_root) {
    return load_effective_text_page(game_root, "texte/inhalt.str");
}

void synchronize_custom_level_alias_pages(TextResourceAsset& page22, TextResourceAsset& page23,
                                          const TextResourceAsset& package_text, uint32_t alias) {
    if (alias < kCustomScenarioAliasFirst || alias > kCustomScenarioAliasLast) {
        throw FormatError("custom scenario alias is outside the game-safe 0x5A..0xCF domain");
    }
    const uint32_t title_index = alias + 0x30U;
    const uint32_t content_base = alias * 0x10U + 0x10U;
    page22.ensure_string_count_all_locales(title_index + 1U);
    page22.copy_string_all_locales(title_index, package_text, 0U);
    page23.ensure_string_count_all_locales(content_base + 0x10U);
    for (uint32_t index = 1U; index < 16U; ++index) {
        page23.copy_string_all_locales(content_base + index - 1U, package_text, index);
    }
}

void validate_frontend_catalog_runtime_contract(
    const std::filesystem::path& game_root, const std::filesystem::path& target_package,
    unsigned slot, std::string_view map_name, std::string_view display_name,
    const std::unordered_set<std::string>& owned_paths, bool allow_patch_shadow = false) {
    const auto expected_alias = game_custom_alias_for_slot(slot);
    const auto target = PckArchive::open(target_package);
    std::ostringstream fragment;
    fragment << "level/level" << std::setfill('0') << std::setw(2) << slot << ".dat";
    const std::string base = "level/" + std::string(map_name);
    const auto* record_entry = exact_entry_or_null(target, fragment.str());
    const auto* level_entry = exact_entry_or_null(target, base + ".lev");
    const auto* field_entry = exact_entry_or_null(target, base + ".fld");
    const auto* text_entry = exact_entry_or_null(target, base + ".str");
    if (record_entry == nullptr || level_entry == nullptr || field_entry == nullptr || text_entry == nullptr) {
        throw FormatError("installed levelNN.pck lacks its exact DAT/LEV/FLD/STR chain");
    }
    std::vector<PckArchive> patches;
    for (const auto& patch_path : existing_numbered_archives(game_root, "patch")) {
        patches.push_back(PckArchive::open(patch_path));
    }
    const auto effective_bytes = [&](std::string_view normalized, const PckEntry& fallback) {
        for (const auto& patch : patches) {
            if (const auto* shadow = exact_entry_or_null(patch, normalized)) return patch.extract(*shadow);
        }
        return target.extract(fallback);
    };
    for (const auto& entry : target.entries()) {
        if (entry.name.find('/') != std::string::npos) {
            throw FormatError("installed package contains a forward-slash path: " + entry.name);
        }
        const auto data = target.extract(entry);
        if (data.size() >= 4U && entry.type_tag != read_u32(data, 0U)) {
            throw FormatError("installed package type tag differs from payload dword zero: " + entry.name);
        }
    }

    const auto record = ScenarioCatalogRecord::parse(effective_bytes(fragment.str(), *record_entry));
    const auto level = LevelAsset::parse(effective_bytes(base + ".lev", *level_entry));
    const auto text = TextResourceAsset::parse(effective_bytes(base + ".str", *text_entry));
    (void)FieldGrid::parse(effective_bytes(base + ".fld", *field_entry));
    const auto faction_summary = analyze_playable_factions(level.placements());
    if (!faction_summary.exportable) {
        throw FormatError("installed LEV has an invalid playable-faction layout: " +
                          faction_summary.diagnostic);
    }
    const uint32_t human_count = level.frontend_assignable_faction_count();
    const uint32_t active_count = level.frontend_active_faction_count();
    if (human_count == 0U || human_count > active_count || active_count > 7U ||
        active_count < faction_summary.highest_faction_index ||
        level.embedded_minimum_player_count() != human_count ||
        level.embedded_maximum_player_count() != active_count) {
        throw FormatError("installed LEV human/active faction metadata is inconsistent");
    }
    if (record.minimum_player_count() != human_count ||
        record.maximum_player_count() != active_count) {
        throw FormatError("installed scenario range does not match LEV human/active faction counts");
    }
    if (record.map_name() != map_name) throw FormatError("installed scenario identifier changed");
    if (record.alias_base() != expected_alias) {
        throw FormatError("installed scenario +0x70 is not the slot-scoped game-safe alias");
    }
    if (level.compatibility_alias_base() != expected_alias) {
        throw FormatError("installed LEV +0x170 is not the slot-scoped game-safe alias");
    }
    if (!text.has_level_list_mutation_prefix()) {
        throw FormatError("STR resource 0 lacks the stock mutable leading-word prefix");
    }
    if (text.compatibility_display_name() != display_name) {
        throw FormatError("STR resource 0 does not resolve to the requested display name");
    }
    for (uint32_t index = 0; index < 16U; ++index) (void)text.string(index);

    for (const auto& patch : patches) {
        for (const auto& entry : patch.entries()) {
            if (!owned_paths.contains(entry.normalized_name)) continue;
            if (!allow_patch_shadow) {
                throw FormatError(std::string("PATCH archive") +
                                  " still shadows installed map entry: " + entry.name);
            }
            const auto data = patch.extract(entry);
            if (data.size() >= 4U && entry.type_tag != read_u32(data, 0U)) {
                throw FormatError("PATCH map override type tag differs from payload dword zero: " + entry.name);
            }
        }
    }

    for (int higher = 99; higher > static_cast<int>(slot); --higher) {
        std::ostringstream name;
        name << "level" << std::setfill('0') << std::setw(2) << higher << ".pck";
        const auto path = find_case_insensitive_leaf(game_root, name.str());
        if (path.empty()) continue;
        const auto info = validate_numbered_package_shape(path);
        if (!info.has_value()) continue;
        const auto archive = PckArchive::open(path);
        for (const auto& entry : archive.entries()) {
            if (owned_paths.contains(entry.normalized_name)) {
                throw FormatError(path.filename().string() + " shadows installed map entry " + entry.name);
            }
        }
        // A deterministic slot-scoped alias makes a duplicate alias proof equivalent to duplicate
        // numbered-slot ownership. Old G326-G328 packages can still carry arbitrary aliases, so
        // reject those only when they collide with this slot's corrected alias.
        if (info->alias_base == expected_alias) {
            throw FormatError(path.filename().string() + " registers this slot's custom text alias first");
        }
    }

    // The stock binary initializes the 4096 override resource IDs to 0xFFFFFFFF at 0x41B0EE but
    // TextResourceOverride_Register scans for zero at 0x41CD04. Package-local STR aliases are
    // therefore not sufficient. The installer mirrors the intended mappings into the first-mounted
    // global page 0x22/0x23 resources in PATCH00 and validates the exact fallback IDs used by the
    // frontend.
    const auto page22 = load_effective_level_ui_text_page(game_root);
    const auto page23 = load_effective_level_content_text_page(game_root);
    static constexpr std::array<std::pair<std::size_t, uint32_t>, 3> kFrontendPage22Resolves{{
        {0x50U, 0x220AU}, {0x60U, 0x2200U}, {0x80U, 0x2205U},
    }};
    for (const auto& [offset, base_id] : kFrontendPage22Resolves) {
        const uint32_t resource_id = read_u32(record.bytes(), offset) + base_id;
        if ((resource_id >> 8U) != 0x22U) {
            throw FormatError("scenario frontend field resolves outside text page 0x22");
        }
        (void)page22.string(resource_id & 0xFFU);
    }

    const uint32_t compact_id = expected_alias + 0x2230U;
    if ((compact_id >> 8U) != 0x22U) {
        throw FormatError("custom level title alias crosses out of compact page 0x22");
    }
    const uint32_t title_index = compact_id & 0xFFU;
    if (page22.string(title_index) != text.string(0U)) {
        throw FormatError("PATCH00 page 0x22 title alias does not match package STR resource 0x3000");
    }
    const uint32_t content_base = expected_alias * 0x10U + 0x10U;
    for (uint32_t source_index = 1U; source_index < 16U; ++source_index) {
        const uint32_t extended_id = 0x230000U + content_base + source_index - 1U;
        if ((extended_id >> 16U) != 0x23U) {
            throw FormatError("custom level detail alias crosses out of extended page 0x23");
        }
        const uint32_t target_index = extended_id & 0xFFFFU;
        if (page23.string(target_index) != text.string(source_index)) {
            throw FormatError("PATCH00 page 0x23 detail alias does not match package STR");
        }
    }
}
} // namespace

std::string MapDocument::terrain_base_path(std::string_view source) {
    std::string normalized = normalize_asset_path(source);
    const auto slash = normalized.find_last_of('/');
    const auto dot = normalized.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) normalized.resize(dot);
    return normalized;
}

std::string MapDocument::terrain_material_path(std::string_view base, uint32_t material) {
    if (material >= 26U) throw std::out_of_range("terrain material index");
    return std::string(base) + static_cast<char>('a' + material) + ".gfx";
}

void MapDocument::initialize_placement_tags(MapDocument& document, const AssetCatalog& catalog) {
    document.placement_tags_.clear();
    document.placement_tags_.reserve(document.level_.placements().size());
    for (std::size_t index = 0; index < document.level_.placements().size(); ++index) {
        const auto& placement = document.level_.placements()[index];
        document.placement_tags_.push_back({index, role_for(catalog, placement),
                                            placement_name_for(catalog, placement)});
    }
}

std::vector<ScenarioCatalogRecord> MapDocument::load_merged_level_catalog(const AssetCatalog& catalog) {
    std::vector<ScenarioCatalogRecord> records;
    const auto merge_path = [&](std::string_view path) {
        const auto* entry = catalog.find(path);
        if (entry == nullptr) return;
        const auto source = parse_scenario_catalog_records(catalog.load(*entry));
        merge_scenario_catalog_records(records, source);
    };
    merge_path("level/level.dat");
    for (unsigned slot = 0; slot <= 99; ++slot) merge_path(scenario_fragment_path(slot));
    return records;
}

TextResourceAsset MapDocument::load_level_text_or_minimal(const AssetCatalog& catalog,
                                                          const MapAssetGroup* group,
                                                          std::string_view display_name) {
    if (group != nullptr) {
        const auto* level_record = catalog.find(group->level_path);
        if (level_record != nullptr) {
            for (const auto& asset : catalog.assets()) {
                if (asset.archive_name == level_record->archive_name && asset.kind == AssetKind::Text &&
                    asset.normalized_path.starts_with("level/")) {
                    try {
                        auto text = TextResourceAsset::parse(catalog.load(asset));
                        text.replace_string_zero_all_locales(display_name);
                        return text;
                    } catch (const FormatError&) {
                        // Continue to the deterministic compatibility asset below.
                    }
                }
            }
        }
    }
    // A LEVEL.PCK map has no package-local STR. Reuse the first valid numbered level STR as the
    // compatibility shell before falling back to a generated asset. This preserves the stock
    // rich-text stream shape and locale records while still replacing resource 0 with the editor
    // display name.
    for (const auto& asset : catalog.assets()) {
        if (asset.kind != AssetKind::Text || !asset.normalized_path.starts_with("level/")) continue;
        try {
            auto text = TextResourceAsset::parse(catalog.load(asset));
            for (uint32_t index = 0; index < 16U; ++index) (void)text.string(index);
            text.replace_string_zero_all_locales(display_name);
            return text;
        } catch (const FormatError&) {
        }
    }
    return TextResourceAsset::minimal_level_compatibility(display_name);
}

ScenarioFrontendMetadata MapDocument::scenario_frontend_metadata() const noexcept {
    return {scenario_record_.planet_theme_index(), scenario_record_.difficulty_index(),
            scenario_record_.map_size_category_index()};
}

void MapDocument::set_scenario_frontend_metadata(const ScenarioFrontendMetadata& metadata) {
    scenario_record_.set_frontend_catalog_indices(metadata.planet_theme_index,
                                                   metadata.difficulty_index,
                                                   metadata.map_size_category_index);
    dirty_ = true;
}

std::array<std::string, 15> MapDocument::scenario_detail_strings(uint32_t preferred_country) const {
    std::array<std::string, 15> result{};
    for (uint32_t index = 0U; index < result.size(); ++index) {
        result[index] = level_text_.string(index + 1U, preferred_country);
    }
    return result;
}

void MapDocument::set_scenario_detail_string(uint32_t detail_index, std::string value) {
    if (detail_index >= 15U) throw FormatError("scenario detail index must be 0..14");
    if (value.size() > 2047U) throw FormatError("scenario detail text exceeds 2047 UTF-8 bytes");
    level_text_.replace_plain_string_all_locales(detail_index + 1U, value);
    dirty_ = true;
}

void MapDocument::restore_editor_metadata(std::span<const std::byte> scenario_record,
                                          std::span<const std::byte> level_text) {
    scenario_record_ = ScenarioCatalogRecord::parse(scenario_record);
    level_text_ = TextResourceAsset::parse(level_text);
    dirty_ = true;
}

PlayableFactionSummary MapDocument::playable_faction_summary() const {
    return analyze_playable_factions(level_.placements(), placement_tags_);
}


PlayerAiConfiguration MapDocument::player_ai_configuration() const {
    return {level_.frontend_assignable_faction_count(),
            level_.frontend_active_faction_count()};
}

void MapDocument::set_player_ai_configuration(const PlayerAiConfiguration& configuration) {
    level_.set_frontend_faction_counts(configuration.human_assignable_factions,
                                       configuration.active_factions);
    level_.set_embedded_player_count_range(configuration.human_assignable_factions,
                                           configuration.active_factions);
    scenario_record_.set_player_count_range(configuration.human_assignable_factions,
                                             configuration.active_factions);
    dirty_ = true;
}

void MapDocument::synchronize_gameplay_metadata(LevelAsset& level,
                                                ScenarioCatalogRecord& scenario) const {
    const auto summary = playable_faction_summary();
    if (!summary.exportable) {
        throw FormatError("map is not game-exportable: " + summary.diagnostic);
    }
    uint32_t human_count = level.frontend_assignable_faction_count();
    uint32_t active_count = level.frontend_active_faction_count();
    if (human_count == 0U || human_count > 7U) human_count = 1U;
    active_count = std::max(active_count, summary.highest_faction_index);
    active_count = std::clamp(active_count, 2U, 7U);
    human_count = std::min(human_count, active_count);
    level.set_frontend_faction_counts(human_count, active_count);
    level.set_embedded_player_count_range(human_count, active_count);
    scenario.set_player_count_range(human_count, active_count);
    // Campaign compatibility shells can legally carry zero starting resources, but a new
    // skirmish map inheriting those zeros starts unable to construct. Repair only the
    // unambiguous zero/zero case for active factions; authored asymmetric values stay intact.
    for (uint32_t faction = 1U; faction <= active_count; ++faction) {
        if (level.faction_building_resource_q4(faction) == 0U &&
            level.faction_energy_resource_q4(faction) == 0U) {
            level.set_faction_starting_resources_q4(
                faction, LevelAsset::kDefaultBuildingResourceQ4,
                LevelAsset::kDefaultEnergyResourceQ4);
        }
    }
}

void MapDocument::validate_game_package_contract() const {
    if (scenario_record_.map_name() != map_name_) {
        throw FormatError("scenario catalog identifier does not match the LEV/FLD map name");
    }
    const uint32_t field_version = field_.converter_version();
    if (field_version != FieldGrid::kGameConverterVersion &&
        field_version != FieldGrid::kLegacyEditorConverterVersion) {
        throw FormatError("FLD converter version is not supported by game.exe; expected 0x00060006");
    }
    if (!field_.has_game_compatible_dimensions()) {
        throw FormatError(
            "FLD dimensions are outside the stock-proven compatibility envelope; each axis must "
            "be 8*n+3 in 59..139 and width*height must not exceed 131*131");
    }
    const auto movie_base = normalize_asset_path(level_.paths().ending_movie);
    if (movie_base.empty()) {
        throw FormatError(
            "LEV ending-movie/environment base is empty; game.exe opens this FLM unconditionally "
            "before the in-game resource loader");
    }
    if (movie_base.ends_with(".flm")) {
        throw FormatError("LEV ending-movie path must be a base path without the .flm extension");
    }
    const auto factions = playable_faction_summary();
    if (!factions.exportable) {
        throw FormatError("map is not game-exportable: " + factions.diagnostic);
    }
    // Scenario record +0x70 and LEV +0x170 are synchronized for the emitted custom package, but
    // equality alone is not sufficient: submitted alias 0x101 matched in both fields and still
    // escaped the runtime fallback text domain. The global text pages must be synchronized too.
    const auto serialized_text = level_text_.serialize();
    const auto validated_text = TextResourceAsset::parse(serialized_text);
    if (validated_text.locale_count() == 0U) throw FormatError("game level package STR has no locale blocks");
    if (!validated_text.has_level_list_mutation_prefix()) {
        throw FormatError("game level package STR resource 0x3000 lacks the stock mutable prefix");
    }
    if (validated_text.compatibility_display_name() != display_name_) {
        throw FormatError("game level package STR resource 0x3000 does not match the display name");
    }
    for (uint32_t index = 0; index < 16U; ++index) (void)validated_text.string(index);
    if (scenario_record_.planet_theme_index() > 9U ||
        scenario_record_.difficulty_index() > 4U ||
        scenario_record_.map_size_category_index() > 4U) {
        throw FormatError("scenario frontend selector is outside the stock text-resource domain");
    }
}

MapDocument MapDocument::open(const AssetCatalog& catalog, const MapAssetGroup& group) {
    MapDocument document;
    document.level_ = LevelAsset::parse(catalog.load(group.level_path));
    document.field_ = FieldGrid::parse(catalog.load(group.field_path));
    document.map_name_ = stem_of(group.level_path);
    document.display_name_ = sanitize_display_name(group.display_name);
    const auto records = load_merged_level_catalog(catalog);
    if (const auto* record = find_scenario_catalog_record(records, document.map_name_)) {
        document.scenario_record_ = *record;
    } else {
        document.scenario_record_ = ScenarioCatalogRecord::minimal(
            document.map_name_, document.level_.compatibility_alias_base());
    }
    document.level_text_ = load_level_text_or_minimal(catalog, &group, document.display_name_);
    const std::string base = terrain_base_path(document.level_.paths().ground_textures);
    for (uint32_t material = 0; material < 26U; ++material) {
        document.terrain_material_sources_[material] = terrain_material_path(base, material);
    }
    initialize_placement_tags(document, catalog);
    document.dirty_ = false;
    return document;
}

MapDocument MapDocument::open_loose(const AssetCatalog& catalog,
                                    const std::filesystem::path& level_path,
                                    const std::filesystem::path& field_path) {
    MapDocument document;
    const auto resolved_field = field_path.empty()
        ? level_path.parent_path() / (level_path.stem().string() + ".fld") : field_path;
    document.level_ = LevelAsset::parse(read_file(level_path));
    document.field_ = FieldGrid::parse(read_file(resolved_field));
    document.map_name_ = sanitize_map_name(level_path.stem().string());
    document.display_name_ = document.map_name_;
    const auto text_path = level_path.parent_path() / (level_path.stem().string() + ".str");
    if (std::filesystem::is_regular_file(text_path)) {
        document.level_text_ = TextResourceAsset::parse(read_file(text_path));
        document.display_name_ = sanitize_display_name(document.level_text_.compatibility_display_name());
    } else {
        document.level_text_ = TextResourceAsset::minimal_level_compatibility(document.display_name_);
    }
    const auto record_path = level_path.parent_path() / (level_path.stem().string() + ".level.dat");
    if (std::filesystem::is_regular_file(record_path)) {
        document.scenario_record_ = ScenarioCatalogRecord::parse(read_file(record_path));
    } else {
        document.scenario_record_ = ScenarioCatalogRecord::minimal(
            document.map_name_, document.level_.compatibility_alias_base());
    }
    const std::string base = terrain_base_path(document.level_.paths().ground_textures);
    const auto loose_root = level_path.parent_path();
    const auto import_loose_asset = [&](const std::string& normalized) {
        const auto candidate = loose_root / std::filesystem::path(normalized);
        if (std::filesystem::is_regular_file(candidate)) {
            document.custom_assets_[normalize_asset_path(normalized)] = read_file(candidate);
        }
    };
    import_loose_asset(base + ".dat");
    for (uint32_t material = 0; material < 26U; ++material) {
        const std::string path = terrain_material_path(base, material);
        document.terrain_material_sources_[material] = path;
        import_loose_asset(path);
    }
    initialize_placement_tags(document, catalog);
    document.dirty_ = false;
    return document;
}

MapDocument MapDocument::open_project(const AssetCatalog& catalog,
                                      const std::filesystem::path& project_path) {
    const auto archive = PckArchive::open(project_path);
    const auto* manifest_entry = archive.find(kProjectManifest);
    if (manifest_entry == nullptr) throw FormatError("map project lacks editor/project.txt");
    const auto manifest = parse_manifest(bytes_text(archive.extract(*manifest_entry)));
    const auto level_it = manifest.find("level_entry");
    const auto field_it = manifest.find("field_entry");
    if (level_it == manifest.end() || field_it == manifest.end()) {
        throw FormatError("map project manifest lacks LEV/FLD entries");
    }
    MapDocument document;
    document.level_ = LevelAsset::parse(archive.extract(level_it->second));
    document.field_ = FieldGrid::parse(archive.extract(field_it->second));
    document.map_name_ = sanitize_map_name(manifest.at("map_name"));
    const auto display = manifest.find("display_name");
    document.display_name_ = display == manifest.end() ? document.map_name_ : sanitize_display_name(display->second);

    const auto scenario_it = manifest.find("scenario_entry");
    const auto string_it = manifest.find("string_entry");
    if (scenario_it != manifest.end() && archive.find(scenario_it->second) != nullptr) {
        document.scenario_record_ = ScenarioCatalogRecord::parse(archive.extract(scenario_it->second));
    } else {
        document.scenario_record_ = ScenarioCatalogRecord::minimal(
            document.map_name_, document.level_.compatibility_alias_base());
    }
    if (string_it != manifest.end() && archive.find(string_it->second) != nullptr) {
        document.level_text_ = TextResourceAsset::parse(archive.extract(string_it->second));
        document.level_text_.replace_string_zero_all_locales(document.display_name_);
    } else {
        document.level_text_ = TextResourceAsset::minimal_level_compatibility(document.display_name_);
    }

    for (const auto& entry : archive.entries()) {
        if (entry.normalized_name == normalize_asset_path(kProjectManifest) ||
            entry.normalized_name == normalize_asset_path(level_it->second) ||
            entry.normalized_name == normalize_asset_path(field_it->second) ||
            (scenario_it != manifest.end() && entry.normalized_name == normalize_asset_path(scenario_it->second)) ||
            (string_it != manifest.end() && entry.normalized_name == normalize_asset_path(string_it->second))) continue;
        document.custom_assets_.emplace(entry.normalized_name, archive.extract(entry));
    }
    const std::string base = terrain_base_path(document.level_.paths().ground_textures);
    for (uint32_t material = 0; material < 26U; ++material) {
        const std::string key = "material." + std::to_string(material);
        const auto found = manifest.find(key);
        document.terrain_material_sources_[material] = found == manifest.end()
            ? terrain_material_path(base, material) : found->second;
    }
    initialize_placement_tags(document, catalog);
    for (std::size_t index = 0; index < document.placement_tags_.size(); ++index) {
        const auto found = manifest.find("placement_name." + std::to_string(index));
        if (found != manifest.end()) document.placement_tags_[index].display_name = found->second;
        if (const auto level = manifest.find("placement_technology_level." + std::to_string(index));
            level != manifest.end())
            document.placement_tags_[index].technology_level = parse_manifest_u32(
                level->second, "placement technology level");
        if (const auto effect = manifest.find("placement_technology_effect." + std::to_string(index));
            effect != manifest.end())
            document.placement_tags_[index].technology_effect_id = parse_manifest_u32(
                effect->second, "placement technology effect");
    }
    document.dirty_ = false;
    return document;
}

MapDocument MapDocument::create_from_template(const AssetCatalog& catalog, const MapAssetGroup& group,
                                              std::string map_name, uint32_t width, uint32_t height) {
    if (!FieldGrid::is_game_compatible_shape(width, height)) {
        throw FormatError(
            "new map dimensions must be game-compatible: "
            "59, 67, 75, 83, 91, 99, 107, 115, 123, 131, or 139 per axis");
    }
    const auto source = open(catalog, group);
    const auto records = load_merged_level_catalog(catalog);
    MapDocument document;
    document.map_name_ = sanitize_map_name(std::move(map_name));
    document.display_name_ = document.map_name_;
    const std::string level_path = "level/" + document.map_name_;
    document.level_ = LevelAsset::from_template(source.level_, level_path, false);
    document.field_ = source.field_;
    document.field_.resize(width, height, true);
    document.field_.set_source_path(level_path + ".fld");
    document.terrain_material_sources_ = source.terrain_material_sources_;
    document.scenario_record_ = source.scenario_record_;
    document.scenario_record_.set_map_name(document.map_name_);
    const auto alias_base = choose_unused_scenario_alias_base(records);
    document.scenario_record_.set_alias_base(alias_base);
    document.level_.set_compatibility_alias_base(alias_base);
    document.level_text_ = source.level_text_;
    document.level_text_.replace_string_zero_all_locales(document.display_name_);
    document.set_player_ai_configuration({1U, 2U});
    for (uint32_t faction = 1U; faction <= 2U; ++faction) {
        document.level_.set_faction_starting_resources_q4(
            faction, LevelAsset::kDefaultBuildingResourceQ4,
            LevelAsset::kDefaultEnergyResourceQ4);
    }
    document.dirty_ = true;
    return document;
}

MapDocument MapDocument::create_empty(const AssetCatalog& catalog, const MapAssetGroup& compatibility_shell,
                                      std::string map_name, std::string display_name,
                                      uint32_t width, uint32_t height,
                                      int32_t initial_height_q12, uint8_t initial_material) {
    if (!FieldGrid::is_game_compatible_shape(width, height)) {
        throw FormatError(
            "new map dimensions must be game-compatible: "
            "59, 67, 75, 83, 91, 99, 107, 115, 123, 131, or 139 per axis");
    }
    const auto shell = open(catalog, compatibility_shell);
    const auto records = load_merged_level_catalog(catalog);
    MapDocument document;
    document.map_name_ = sanitize_map_name(std::move(map_name));
    document.display_name_ = sanitize_display_name(std::move(display_name));
    const std::string level_path = "level/" + document.map_name_;
    document.level_ = LevelAsset::from_template(shell.level_, level_path, false);
    document.field_ = FieldGrid::create(std::max(width, 2U), std::max(height, 2U),
                                        initial_height_q12, initial_material);
    document.field_.set_source_path(level_path + ".fld");
    document.terrain_material_sources_ = shell.terrain_material_sources_;
    document.scenario_record_ = shell.scenario_record_;
    document.scenario_record_.set_map_name(document.map_name_);
    const auto alias_base = choose_unused_scenario_alias_base(records);
    document.scenario_record_.set_alias_base(alias_base);
    document.level_.set_compatibility_alias_base(alias_base);
    document.level_text_ = shell.level_text_;
    document.level_text_.replace_string_zero_all_locales(document.display_name_);
    document.set_player_ai_configuration({1U, 2U});
    for (uint32_t faction = 1U; faction <= 2U; ++faction) {
        document.level_.set_faction_starting_resources_q4(
            faction, LevelAsset::kDefaultBuildingResourceQ4,
            LevelAsset::kDefaultEnergyResourceQ4);
    }
    document.dirty_ = true;
    return document;
}

std::string MapDocument::sanitize_map_name(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
               c == '"' || c == '<' || c == '>' || c == '|';
    }), value.end());
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    if (value.empty()) throw FormatError("map name is empty after filename sanitization");
    if (value.size() > 28) throw FormatError("map name must fit the stock 0x40-byte UTF-16 level-path field");
    return lower_ascii(std::move(value));
}

std::string MapDocument::sanitize_display_name(std::string value) {
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return c < 0x20 && c != '\t';
    }), value.end());
    if (value.empty()) value = "Untitled map";
    if (value.size() > 96U) value.resize(96U);
    return value;
}

void MapDocument::resize_field(uint32_t width, uint32_t height, bool center_existing) {
    if (!FieldGrid::is_game_compatible_shape(width, height)) {
        throw FormatError(
            "map dimensions must be inside the stock-proven compatibility envelope: "
            "8*n+3 axes in 59..139 and area no larger than 131*131");
    }
    const int32_t dx_cells = center_existing
        ? (static_cast<int32_t>(width) - static_cast<int32_t>(field_.width())) / 2
        : 0;
    const int32_t dy_cells = center_existing
        ? (static_cast<int32_t>(height) - static_cast<int32_t>(field_.height())) / 2
        : 0;
    const int64_t delta_render_x = static_cast<int64_t>(dx_cells) * 0x901LL +
                                   static_cast<int64_t>(dy_cells) * 0x480LL;
    const int64_t delta_render_y = -static_cast<int64_t>(dy_cells) * 1999LL;

    auto shifted = std::vector<LevelPlacement>(level_.placements().begin(), level_.placements().end());
    for (auto& placement : shifted) {
        const int64_t next_render_x = static_cast<int64_t>(placement.world_y_q12) + delta_render_x;
        const int64_t next_render_y = static_cast<int64_t>(placement.world_x_q12) + delta_render_y;
        const double grid_y = -static_cast<double>(next_render_y) / 1999.0;
        const double grid_x = (static_cast<double>(next_render_x) - 1152.0 * grid_y) / 2305.0;
        if (grid_x < 0.0 || grid_y < 0.0 ||
            grid_x > static_cast<double>(width - 1U) ||
            grid_y > static_cast<double>(height - 1U)) {
            throw FormatError("centered resize would move a placement outside the resized field");
        }
        if (next_render_x < std::numeric_limits<int32_t>::min() ||
            next_render_x > std::numeric_limits<int32_t>::max() ||
            next_render_y < std::numeric_limits<int32_t>::min() ||
            next_render_y > std::numeric_limits<int32_t>::max()) {
            throw FormatError("centered resize overflows a serialized placement coordinate");
        }
        placement.world_y_q12 = static_cast<int32_t>(next_render_x);
        placement.world_x_q12 = static_cast<int32_t>(next_render_y);
    }

    field_.resize(width, height, center_existing);
    auto placements = level_.placements();
    std::copy(shifted.begin(), shifted.end(), placements.begin());
    dirty_ = true;
}

void MapDocument::set_map_name(std::string value) {
    map_name_ = sanitize_map_name(std::move(value));
    level_.paths().level = "level/" + map_name_;
    field_.set_source_path("level/" + map_name_ + ".fld");
    scenario_record_.set_map_name(map_name_);
    dirty_ = true;
}

void MapDocument::set_display_name(std::string value) {
    display_name_ = sanitize_display_name(std::move(value));
    level_text_.replace_string_zero_all_locales(display_name_);
    dirty_ = true;
}

std::vector<std::byte> MapDocument::load_asset(const AssetCatalog& catalog, std::string_view path) const {
    const auto normalized = normalize_asset_path(path);
    const auto found = custom_assets_.find(normalized);
    if (found != custom_assets_.end()) return found->second;
    return catalog.load(normalized);
}

bool MapDocument::has_custom_asset(std::string_view path) const {
    return custom_assets_.contains(normalize_asset_path(path));
}

std::string MapDocument::ensure_custom_terrain_family(const AssetCatalog& catalog) {
    const std::string current_base = terrain_base_path(level_.paths().ground_textures);
    if (current_base.starts_with("gfx/boden/edit_")) return current_base;
    const std::string custom_base = "gfx/boden/edit_" + map_name_ + "_";
    custom_assets_[custom_base + ".dat"] = load_asset(catalog, current_base + ".dat");
    for (uint32_t material = 0; material < 26U; ++material) {
        const std::string source = terrain_material_path(current_base, material);
        const std::string destination = terrain_material_path(custom_base, material);
        custom_assets_[destination] = load_asset(catalog, source);
        if (terrain_material_sources_[material].empty()) terrain_material_sources_[material] = source;
    }
    level_.paths().ground_textures = custom_base;
    return custom_base;
}

void MapDocument::import_terrain_material(const AssetCatalog& catalog, uint8_t destination_material,
                                          std::string_view source_gfx_path) {
    if (destination_material >= 26U) throw std::out_of_range("terrain material index");
    const std::string source = normalize_asset_path(source_gfx_path);
    const auto* record = catalog.find(source);
    if (record == nullptr || record->kind != AssetKind::Texture) {
        throw FormatError("terrain material source is not a GFX texture: " + source);
    }
    import_terrain_material_bytes(catalog, destination_material, source, catalog.load(*record));
}

void MapDocument::import_terrain_material_bytes(const AssetCatalog& catalog,
                                                uint8_t destination_material,
                                                std::string provenance,
                                                std::vector<std::byte> gfx_bytes) {
    if (destination_material >= 26U) throw std::out_of_range("terrain material index");
    // Validate before publishing the replacement. A malformed imported GFX must not poison the
    // complete custom terrain family persisted by project, loose, and patch saves.
    const auto parsed = GfxAsset::parse(gfx_bytes);
    if (parsed.subresources().empty()) throw FormatError("imported terrain GFX has no subresources");
    const std::string custom_base = ensure_custom_terrain_family(catalog);
    custom_assets_[terrain_material_path(custom_base, destination_material)] = std::move(gfx_bytes);
    terrain_material_sources_[destination_material] = std::move(provenance);
    dirty_ = true;
}

std::filesystem::path MapDocument::export_game_level_package(
    const std::filesystem::path& game_root, unsigned slot, bool create_backup) const {
    if (slot > 99U) throw FormatError("game level package slot must be in the range 00..99");
    if (!has_supported_game_executable(game_root)) {
        throw FormatError(
            "game level package root must be the directory containing game.exe or thandor.exe");
    }
    validate_game_package_contract();
    const auto movie_asset = normalize_asset_path(level_.paths().ending_movie) + ".flm";
    if (!effective_resource_exists(game_root, movie_asset)) {
        throw FormatError("LEV environment movie is not present in the mounted game archives: " +
                          movie_asset);
    }
    const uint32_t emitted_alias = game_custom_alias_for_slot(slot);
    auto emitted_record = scenario_record_;
    auto emitted_level = level_;
    synchronize_gameplay_metadata(emitted_level, emitted_record);
    emitted_record.set_alias_base(emitted_alias);
    emitted_level.set_compatibility_alias_base(emitted_alias);
    auto emitted_text = level_text_;
    emitted_text.replace_string_zero_all_locales(display_name_);
    auto emitted_field = field_;
    // Visible resource ARM records and buildable FLD support masks are separate stock-map
    // authoring data. Repair legacy/editor maps by stamping the stock-proven 5x5 support patch
    // around every active resource placement before serialization.
    stamp_resource_field_masks(emitted_field, emitted_level.placements());
    emitted_field.normalize_for_game_export();

    std::ostringstream filename;
    filename << "level" << std::setfill('0') << std::setw(2) << slot << ".pck";
    auto output = find_case_insensitive_leaf(game_root, filename.str());
    if (output.empty()) output = game_root / filename.str();

    const auto header_template = select_header_template(game_root, output);
    const std::string internal_base = "level/" + map_name_;
    std::vector<PckWriteEntry> entries;
    entries.push_back({scenario_fragment_path(slot), emitted_record.serialize(), 0});
    entries.push_back({internal_base + ".lev", emitted_level.serialize(), 0});
    entries.push_back({internal_base + ".str", emitted_text.serialize(), 0});
    entries.push_back({internal_base + ".fld", emitted_field.serialize(), 0});
    for (const auto& [path, data] : custom_assets_) entries.push_back({path, data, 0});
    const auto package_bytes = build_stored_pck(entries, header_template);
    const auto owned_paths = owned_game_package_paths(slot, map_name_, custom_assets_);

    // The archived executable's package-local alias table is initialized to 0xFFFFFFFF but its
    // registration routine searches for zero. Mirror the intended 0x3000..0x300F mappings into
    // PATCH00's first-mounted global page 0x22 and 0x23 resources. Slot 00 starts at alias 0x5A,
    // directly after the stock title range, and each numbered slot receives a deterministic alias.
    auto page22 = load_effective_level_ui_text_page(game_root);
    auto page23 = load_effective_level_content_text_page(game_root);
    synchronize_custom_level_alias_pages(page22, page23, emitted_text, emitted_alias);
    const std::vector<PckWriteEntry> text_page_replacements{
        {"texte/level.str", page22.serialize(), 0U},
        {"texte/inhalt.str", page23.serialize(), 0U},
    };

    auto patch_paths = existing_numbered_archives(game_root, "patch");
    if (patch_paths.empty()) patch_paths.push_back(game_root / "PATCH00.PCK");
    const auto text_patch_path = patch_paths.front();
    std::map<std::filesystem::path, std::vector<std::byte>> repaired_patch_bytes;
    for (const auto& patch_path : patch_paths) {
        std::vector<std::byte> patch_header;
        std::vector<PckWriteEntry> patch_entries;
        if (std::filesystem::is_regular_file(patch_path)) {
            const auto patch = PckArchive::open(patch_path);
            patch_header.assign(patch.header_bytes().begin(), patch.header_bytes().end());
            std::size_t removed_count = 0;
            patch_entries = remove_pck_paths(patch, owned_paths, removed_count);
            patch_entries.erase(std::remove_if(patch_entries.begin(), patch_entries.end(),
                [&](const PckWriteEntry& entry) {
                    if (!is_root_level_map_chain_entry(normalize_asset_path(entry.name))) return false;
                    ++removed_count;
                    return true;
                }), patch_entries.end());
            if (patch_path == text_patch_path) {
                for (const auto& replacement : text_page_replacements) {
                    const auto normalized = normalize_asset_path(replacement.name);
                    const auto found = std::find_if(patch_entries.begin(), patch_entries.end(),
                        [&](const PckWriteEntry& entry) {
                            return normalize_asset_path(entry.name) == normalized;
                        });
                    if (found == patch_entries.end()) patch_entries.push_back(replacement);
                    else *found = replacement;
                }
            }
        } else {
            patch_header = select_header_template(game_root, {});
            patch_entries = text_page_replacements;
        }
        repaired_patch_bytes.emplace(patch_path, build_stored_pck(patch_entries, patch_header));
    }

    const auto original_level = read_optional_file(output);
    std::map<std::filesystem::path, std::optional<std::vector<std::byte>>> original_patches;
    for (const auto& [patch_path, bytes] : repaired_patch_bytes) {
        (void)bytes;
        original_patches.emplace(patch_path, read_optional_file(patch_path));
    }
    if (create_backup) {
        backup_existing_file(output);
        for (const auto& [patch_path, original] : original_patches) {
            (void)original;
            backup_existing_file(patch_path);
        }
    }

    try {
        write_file_atomic(output, package_bytes);
        for (const auto& [patch_path, bytes] : repaired_patch_bytes) {
            write_file_atomic(patch_path, bytes);
        }
        validate_frontend_catalog_runtime_contract(game_root, output, slot, map_name_,
                                                   display_name_, owned_paths);
    } catch (...) {
        restore_optional_file(output, original_level);
        for (const auto& [patch_path, original] : original_patches) {
            restore_optional_file(patch_path, original);
        }
        throw;
    }
    return output;
}

std::filesystem::path MapDocument::update_patch00_overrides(
    const std::filesystem::path& game_root, unsigned slot,
    const Patch00ExportOptions& options, bool create_backup) const {
    if (slot > 99U) throw FormatError("PATCH00 scenario slot must be in the range 00..99");
    if (!options.any()) throw FormatError("no PATCH00 override groups were selected");
    const bool map_chain_selected = options.include_scenario_catalog || options.include_level ||
                                    options.include_field || options.include_text;
    if (map_chain_selected) {
        throw FormatError(
            "PATCH00 map-chain overrides are disabled: game.exe mounts PATCH archives before "
            "levelNN.pck, so stale DAT/LEV/FLD/STR shadows can override the repaired numbered "
            "package. Use Install/repair game level package; PATCH00 append is limited to custom "
            "assets and synchronized global text pages.");
    }
    validate_game_package_contract();
    const auto movie_asset = normalize_asset_path(level_.paths().ending_movie) + ".flm";
    if (!effective_resource_exists(game_root, movie_asset)) {
        throw FormatError("LEV environment movie is not present in the mounted game archives: " +
                          movie_asset);
    }
    const uint32_t emitted_alias = game_custom_alias_for_slot(slot);
    auto emitted_text = level_text_;
    emitted_text.replace_string_zero_all_locales(display_name_);

    const auto patch_mounts = existing_numbered_archives(game_root, "patch");
    auto output = patch_mounts.empty() ? game_root / "PATCH00.PCK" : patch_mounts.front();

    std::vector<PckWriteEntry> replacements;
    if (options.include_custom_assets) {
        for (const auto& [path, data] : custom_assets_) replacements.push_back({path, data, 0});
    }
    auto page22 = load_effective_level_ui_text_page(game_root);
    auto page23 = load_effective_level_content_text_page(game_root);
    synchronize_custom_level_alias_pages(page22, page23, emitted_text, emitted_alias);
    replacements.push_back({"texte/level.str", page22.serialize(), 0U});
    replacements.push_back({"texte/inhalt.str", page23.serialize(), 0U});
    if (replacements.empty()) throw FormatError("selected PATCH00 groups contain no entries for this map");

    std::vector<std::byte> header_template;
    std::vector<PckWriteEntry> merged;
    if (std::filesystem::exists(output)) {
        const auto existing = PckArchive::open(output);
        header_template.assign(existing.header_bytes().begin(), existing.header_bytes().end());
        merged = merge_pck_entries(existing, replacements);
    } else {
        header_template = select_header_template(game_root, {});
        merged = replacements;
    }
    const auto updated_bytes = build_stored_pck(merged, header_template);
    const auto original = read_optional_file(output);
    if (create_backup) backup_existing_file(output);

    try {
        write_file_atomic(output, updated_bytes);
        const auto verification = PckArchive::open(output);
        for (const auto& replacement : replacements) {
            const auto exact = canonical_pck_entry_path(replacement.name);
            const auto* found = verification.find_exact(exact);
            if (found == nullptr || verification.extract(*found) != replacement.data) {
                throw FormatError("PATCH00 exact-path update verification failed: " + exact);
            }
        }
        for (const auto& entry : verification.entries()) {
            if (entry.name.find('/') != std::string::npos) {
                throw FormatError("PATCH00 contains a forward-slash path rejected by game.exe: " +
                                  entry.name);
            }
        }
    } catch (...) {
        restore_optional_file(output, original);
        throw;
    }
    return output;
}

void MapDocument::export_loose(const std::filesystem::path& directory) const {
    validate_game_package_contract();
    auto emitted_level = level_;
    auto emitted_record = scenario_record_;
    synchronize_gameplay_metadata(emitted_level, emitted_record);
    auto emitted_field = field_;
    // Visible resource ARM records and buildable FLD support masks are separate stock-map
    // authoring data. Repair legacy/editor maps by stamping the stock-proven 5x5 support patch
    // around every active resource placement before serialization.
    stamp_resource_field_masks(emitted_field, emitted_level.placements());
    emitted_field.normalize_for_game_export();
    std::filesystem::create_directories(directory);
    write_file_atomic(directory / (map_name_ + ".lev"), emitted_level.serialize());
    write_file_atomic(directory / (map_name_ + ".fld"), emitted_field.serialize());
    write_file_atomic(directory / (map_name_ + ".str"), level_text_.serialize());
    write_file_atomic(directory / (map_name_ + ".level.dat"), emitted_record.serialize());
    for (const auto& [path, data] : custom_assets_) {
        const auto output = directory / std::filesystem::path(path);
        std::filesystem::create_directories(output.parent_path());
        write_file_atomic(output, data);
    }
}

void MapDocument::save_project(const std::filesystem::path& project_path) const {
    auto emitted_level = level_;
    auto emitted_record = scenario_record_;
    const auto factions = playable_faction_summary();
    if (factions.exportable) synchronize_gameplay_metadata(emitted_level, emitted_record);
    const std::string level_entry = "level/" + map_name_ + ".lev";
    const std::string field_entry = "level/" + map_name_ + ".fld";
    const std::string string_entry = level_string_path(map_name_);
    std::ostringstream manifest;
    manifest << kProjectMagic << '\n';
    manifest << "map_name=" << percent_encode(map_name_) << '\n';
    manifest << "display_name=" << percent_encode(display_name_) << '\n';
    manifest << "level_entry=" << percent_encode(level_entry) << '\n';
    manifest << "field_entry=" << percent_encode(field_entry) << '\n';
    manifest << "scenario_entry=" << percent_encode(kScenarioEntry) << '\n';
    manifest << "string_entry=" << percent_encode(string_entry) << '\n';
    manifest << "alias_base=" << scenario_record_.alias_base() << '\n';
    for (uint32_t material = 0; material < 26U; ++material) {
        manifest << "material." << material << '=' << percent_encode(terrain_material_sources_[material]) << '\n';
    }
    for (std::size_t index = 0; index < placement_tags_.size(); ++index) {
        manifest << "placement_name." << index << '=' << percent_encode(placement_tags_[index].display_name) << '\n';
        manifest << "placement_technology_level." << index << '='
                 << placement_tags_[index].technology_level << '\n';
        manifest << "placement_technology_effect." << index << '='
                 << placement_tags_[index].technology_effect_id << '\n';
    }
    std::vector<PckWriteEntry> entries;
    entries.push_back({std::string(kProjectManifest), text_bytes_padded(manifest.str()), fourcc("txt")});
    entries.push_back({level_entry, emitted_level.serialize(), fourcc("lev")});
    entries.push_back({field_entry, field_.serialize(), fourcc("fld")});
    entries.push_back({std::string(kScenarioEntry), emitted_record.serialize(), fourcc("dat")});
    entries.push_back({string_entry, level_text_.serialize(), fourcc("str")});
    for (const auto& [path, data] : custom_assets_) entries.push_back({path, data, 0});
    write_stored_pck(project_path, entries);
}

} // namespace thandor
