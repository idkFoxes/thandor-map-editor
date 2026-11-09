#pragma once

#include "thandor/terrain_shadow.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor {
// Cache the retail generated-shadow result for an authored LEV placement. Shadow
// atlas placement and receiver-vertex UV remapping remain scene-local; only the
// immutable mask and contact-solved receiver geometry are reused. Verify every
// input structurally rather than relying on a hash (or a mutable scene tick).
// The map is edited on the editor thread, never concurrently with this cache.
class GeneratedShadowReuse {
public:
    void begin(const thandor::FieldGrid& field) {
        const auto cells=field.cells();
        const auto equal_cell=[](const auto& a,const auto& b){
            return a.world_x_q12==b.world_x_q12&&a.world_y_q12==b.world_y_q12&&
                a.terrain_height_q12==b.terrain_height_q12&&
                a.water_delta_q12==b.water_delta_q12&&
                a.flags_and_material==b.flags_and_material;
        };
        if(field.width()!=field_width_||field.height()!=field_height_||
           cells.size()!=field_cells_.size()||
           !std::equal(cells.begin(),cells.end(),field_cells_.begin(),equal_cell)){
            entries_.clear();
            field_cells_.assign(cells.begin(),cells.end());
            field_width_=field.width();field_height_=field.height();
        }
    }

    template<class Builder>
    const std::optional<thandor::GeneratedObjectShadow>& obtain(
        std::size_t placement_index,std::span<const thandor::GeneratedShadowTriangle> triangles,
        thandor::Vec3f direction,thandor::GeneratedShadowSettings settings,Builder&& builder){
        auto found=entries_.find(placement_index);
        if(found!=entries_.end()&&equal_input(found->second,triangles,direction,settings)){
            ++hits_;
            return found->second.shadow;
        }
        // Compute first. Failed preparations cannot destroy an earlier valid entry.
        auto result=std::forward<Builder>(builder)();
        ++misses_;
        // Do not cache unlimited scene-authored placements on huge custom maps.
        if(found==entries_.end()&&entries_.size()>=kMaximumEntries){
            uncached_=std::move(result);
            return uncached_;
        }
        Entry entry;
        entry.triangles.assign(triangles.begin(),triangles.end());
        entry.direction=direction;entry.settings=settings;entry.shadow=std::move(result);
        auto [it,inserted]=entries_.insert_or_assign(placement_index,std::move(entry));
        (void)inserted;
        return it->second.shadow;
    }
    void clear(){entries_.clear();field_cells_.clear();field_width_=0;field_height_=0;uncached_.reset();}
    std::uint64_t hits()const noexcept{return hits_;}
    std::uint64_t misses()const noexcept{return misses_;}
    std::size_t entries()const noexcept{return entries_.size();}

private:
    static constexpr std::size_t kMaximumEntries=1024U;
    struct Entry {
        std::vector<thandor::GeneratedShadowTriangle> triangles;
        thandor::Vec3f direction{};
        thandor::GeneratedShadowSettings settings{};
        std::optional<thandor::GeneratedObjectShadow> shadow;
    };
    static bool equal_direction(thandor::Vec3f a,thandor::Vec3f b){
        return a.x==b.x&&a.y==b.y&&a.z==b.z;
    }
    static bool equal_input(const Entry& entry,
        std::span<const thandor::GeneratedShadowTriangle> triangles,
        thandor::Vec3f direction,thandor::GeneratedShadowSettings settings){
        if(!equal_direction(entry.direction,direction)||
           entry.settings.texture_dimension!=settings.texture_dimension||
           entry.settings.grid_half_size!=settings.grid_half_size||
           entry.settings.subresource_count!=settings.subresource_count||
           entry.settings.surface_bias!=settings.surface_bias||
           entry.triangles.size()!=triangles.size())return false;
        for(std::size_t i=0;i<triangles.size();++i){
            if(entry.triangles[i].blurred_group!=triangles[i].blurred_group)return false;
            for(std::size_t j=0;j<3U;++j)
                if(!equal_direction(entry.triangles[i].world[j],triangles[i].world[j]))return false;
        }
        return true;
    }
    std::unordered_map<std::size_t,Entry> entries_;
    std::vector<thandor::FieldCell> field_cells_;
    std::uint32_t field_width_{},field_height_{};
    std::optional<thandor::GeneratedObjectShadow> uncached_;
    std::uint64_t hits_{},misses_{};
};
} // namespace editor
