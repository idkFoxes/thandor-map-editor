#pragma once

#include "thandor/binary.hpp"
#include <array>
#include <span>
#include <string>
#include <vector>

namespace thandor {

// Disk CGN records, independent of the relocated runtime views in the original game.
// Uninterpreted bytes and imported path spelling are preserved on a no-op round trip.
class CampaignLevelRecord {
public:
    static constexpr std::size_t kSize=0x180;
    static constexpr std::size_t kOutcomeCount=8;
    static constexpr std::size_t kFactionCount=8;
    static CampaignLevelRecord parse(std::span<const std::byte> bytes);
    static CampaignLevelRecord minimal(int32_t id,std::string_view relative_level_path);
    int32_t level_id() const;
    void set_level_id(int32_t id);
    std::string level_path() const;
    void set_level_path(std::string_view relative_level_path);
    int32_t successor(std::size_t outcome) const;
    void set_successor(std::size_t outcome,int32_t id);
    int32_t end_movie(std::size_t outcome,bool alternate_variant) const;
    void set_end_movie(std::size_t outcome,bool alternate_variant,int32_t number);
    bool carry_over(std::size_t outcome) const;
    bool retain_previous_carry_over(std::size_t outcome) const;
    void set_carry_over(std::size_t outcome,bool enabled);
    void set_retain_previous_carry_over(std::size_t outcome,bool enabled);
    struct ExitZone {
        int32_t center_x_q12{},center_y_q12{},radius_q12{};
        int32_t destination_x_q12{},destination_y_q12{};
    };
    ExitZone exit_zone(std::size_t faction) const;
    void set_exit_zone(std::size_t faction,const ExitZone& zone);
    std::span<const std::byte,kSize> bytes() const noexcept {return bytes_;}
private:
    std::array<std::byte,kSize> bytes_{};
};

enum class CampaignContinuationKind { NextLevel, CampaignFinished, NoTransition };
struct CampaignContinuation {
    CampaignContinuationKind kind{CampaignContinuationKind::NoTransition};
    int32_t successor_id{};
    std::string level_path;
};

class CampaignAsset {
public:
    static constexpr std::size_t kHeaderSize=0x200;
    static CampaignAsset parse(std::span<const std::byte> bytes);
    // Preserve the common asset header and embedded catalog metadata from a valid template.
    // Creating a selectable campaign also needs its campaign catalog and localized text entries.
    static CampaignAsset from_template(const CampaignAsset& source,int32_t first_level_id,
                                       std::span<const CampaignLevelRecord> records);
    int32_t first_level_id() const;
    void set_first_level_id(int32_t id);
    int32_t saved_current_level_id() const;
    std::span<const CampaignLevelRecord> records() const noexcept {return records_;}
    CampaignLevelRecord& record(std::size_t index);
    const CampaignLevelRecord* find_level(int32_t id) const noexcept;
    CampaignContinuation continuation(int32_t current_level_id,std::size_t outcome) const;
    std::vector<int32_t> unreachable_level_ids() const;
    std::vector<std::string> authoring_errors() const;
    std::vector<std::byte> serialize() const;
private:
    std::array<std::byte,kHeaderSize> header_{};
    std::vector<CampaignLevelRecord> records_;
    std::vector<std::byte> trailing_bytes_;
    std::size_t original_size_{};
};
} // namespace thandor
