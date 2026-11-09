#pragma once

#include "thandor/campaign.hpp"
#include "thandor/map_document.hpp"

namespace thandor {

// Campaign catalog +0x50 is a title index, unlike a single-map catalog record.
class CampaignCatalogRecord {
public:
    static CampaignCatalogRecord parse(std::span<const std::byte> bytes);
    std::string identifier() const;
    void set_identifier(std::string_view name);
    uint32_t title_index() const;
    void set_title_index(uint32_t index);
    std::vector<std::byte> serialize() const;
private:
    std::array<std::byte,0x100> bytes_{};
};

struct CampaignMissionSource {
    int32_t level_id{};
    // Empty means load the native LEV and its actual referenced FLD from the game.
    // Project paths are absolute in memory and saved relative to the campaign project.
    std::filesystem::path map_project;
    std::array<std::string,8> outcome_names;
};
struct CampaignValidation {
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    bool ok() const noexcept { return errors.empty(); }
};
struct CampaignPackageResult {
    std::filesystem::path level_package;
    std::filesystem::path text_patch;
    uint32_t title_index{};
    std::size_t mission_count{};
    std::vector<std::string> warnings;
};

class CampaignDocument {
public:
    static CampaignDocument import_native(const std::filesystem::path& game_root,
                                           std::string_view cgn_path);
    static CampaignDocument create(const CampaignDocument& header_template,
                                    std::string_view identifier);
    static CampaignDocument open_project(const std::filesystem::path& path);
    void save_project(const std::filesystem::path& path) const;
    CampaignAsset& campaign() noexcept { return campaign_; }
    const CampaignAsset& campaign() const noexcept { return campaign_; }
    const std::string& identifier() const noexcept { return identifier_; }
    void set_identifier(std::string_view value);
    std::string title;
    std::string description;
    std::vector<CampaignMissionSource> missions;
    CampaignMissionSource& source(int32_t level_id);
    const CampaignMissionSource& source(int32_t level_id) const;
    void add_map_project(const AssetCatalog& catalog,const std::filesystem::path& path);
    void remove_mission(int32_t level_id);
    void set_mission_id(int32_t previous,int32_t next);
    std::string outcome_label(int32_t level_id,std::size_t outcome) const;
    std::string continuation_label(int32_t level_id,std::size_t outcome) const;
    CampaignValidation validate(const AssetCatalog& catalog) const;
    // Stage complete replacements outside the installation. Never overwrite the game here.
    // Output must be a new directory; PATCH00 contains the existing PATCH00 entries plus text updates.
    CampaignPackageResult export_package(const AssetCatalog& catalog,
        const std::filesystem::path& output_directory,unsigned slot) const;
    static unsigned first_free_package_slot(const std::filesystem::path& game_root);
private:
    CampaignAsset campaign_;
    CampaignCatalogRecord catalog_record_;
    std::string identifier_;
};

// Diagnostics keep serialized order. No sorting or rewriting of imported conditions.
CampaignValidation validate_campaign_mission_script(const LevelAsset& level);
LevelScheduledCondition campaign_expression_from_text(std::string_view text);
std::string campaign_expression_text(const LevelScheduledCondition& condition);
// Uses the same native world-Y -> render-X mapping as LEV placements.
std::array<float,5> campaign_exit_zone_render(const CampaignLevelRecord::ExitZone& zone);
CampaignLevelRecord::ExitZone campaign_exit_zone_from_render(const std::array<float,5>& zone);

} // namespace thandor
