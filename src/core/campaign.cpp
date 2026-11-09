#include "thandor/campaign.hpp"
#include "thandor/pck.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace thandor {
namespace {
void outcome_check(std::size_t value){if(value>=8)throw FormatError("campaign outcome must be 0..7");}
void faction_check(std::size_t value){if(value>=8)throw FormatError("campaign faction must be 0..7");}
bool mask_bit(std::span<const std::byte> bytes,std::size_t offset,std::size_t outcome){
    outcome_check(outcome);return (read_u32(bytes,offset)&(1U<<outcome))!=0;
}
void set_mask_bit(std::span<std::byte> bytes,std::size_t offset,std::size_t outcome,bool enabled){
    outcome_check(outcome);auto mask=read_u32(bytes,offset);
    write_u32(bytes,offset,enabled?mask|(1U<<outcome):mask&~(1U<<outcome));
}
}
CampaignLevelRecord CampaignLevelRecord::parse(std::span<const std::byte> bytes){
    if(bytes.size()!=kSize)throw FormatError("campaign level record must contain 0x180 bytes");
    CampaignLevelRecord result;std::copy(bytes.begin(),bytes.end(),result.bytes_.begin());return result;
}
CampaignLevelRecord CampaignLevelRecord::minimal(int32_t id,std::string_view path){
    CampaignLevelRecord result;result.set_level_id(id);result.set_level_path(path);return result;
}
int32_t CampaignLevelRecord::level_id() const{return read_i32(bytes_,0x100);}
void CampaignLevelRecord::set_level_id(int32_t id){write_i32(bytes_,0x100,id);}
std::string CampaignLevelRecord::level_path() const{return read_utf16le_z(bytes_,0x10C,0x74);}
void CampaignLevelRecord::set_level_path(std::string_view path){
    if(path.empty()||path.front()=='/'||path.front()=='\\'||path.find('\0')!=std::string_view::npos)
        throw FormatError("campaign map path must be relative and terminated");
    auto normalized=normalize_asset_path(path);
    if(normalized.starts_with("level/"))normalized.erase(0,6);
    if(normalized.ends_with(".lev"))normalized.resize(normalized.size()-4);
    if(normalized.empty())throw FormatError("campaign map path is empty");
    // Reuse the package writer's rejection of absolute paths, parent traversal and unsafe components.
    const auto canonical=canonical_pck_entry_path(normalized);
    auto updated=bytes_;
    write_utf16le_fixed(updated,0x10C,0x74,canonical);
    bytes_=updated;
}
int32_t CampaignLevelRecord::successor(std::size_t outcome) const{
    outcome_check(outcome);return read_i32(bytes_,4*outcome);
}
void CampaignLevelRecord::set_successor(std::size_t outcome,int32_t id){
    outcome_check(outcome);write_i32(bytes_,4*outcome,id);
}
int32_t CampaignLevelRecord::end_movie(std::size_t outcome,bool alternate) const{
    outcome_check(outcome);return read_i32(bytes_,(alternate?0x20:0x40)+4*outcome);
}
void CampaignLevelRecord::set_end_movie(std::size_t outcome,bool alternate,int32_t number){
    outcome_check(outcome);write_i32(bytes_,(alternate?0x20:0x40)+4*outcome,number);
}
bool CampaignLevelRecord::carry_over(std::size_t outcome) const{return mask_bit(bytes_,0x104,outcome);}
bool CampaignLevelRecord::retain_previous_carry_over(std::size_t outcome) const{return mask_bit(bytes_,0x108,outcome);}
void CampaignLevelRecord::set_carry_over(std::size_t outcome,bool enabled){set_mask_bit(bytes_,0x104,outcome,enabled);}
void CampaignLevelRecord::set_retain_previous_carry_over(std::size_t outcome,bool enabled){set_mask_bit(bytes_,0x108,outcome,enabled);}
CampaignLevelRecord::ExitZone CampaignLevelRecord::exit_zone(std::size_t faction) const{
    faction_check(faction);const auto at=faction*4;
    return {read_i32(bytes_,0x60+at),read_i32(bytes_,0x80+at),read_i32(bytes_,0xA0+at),
            read_i32(bytes_,0xC0+at),read_i32(bytes_,0xE0+at)};
}
void CampaignLevelRecord::set_exit_zone(std::size_t faction,const ExitZone& zone){
    faction_check(faction);const auto at=faction*4;
    write_i32(bytes_,0x60+at,zone.center_x_q12);write_i32(bytes_,0x80+at,zone.center_y_q12);
    write_i32(bytes_,0xA0+at,zone.radius_q12);write_i32(bytes_,0xC0+at,zone.destination_x_q12);
    write_i32(bytes_,0xE0+at,zone.destination_y_q12);
}
CampaignAsset CampaignAsset::parse(std::span<const std::byte> bytes){
    if(bytes.size()<kHeaderSize||read_u32(bytes,0)!=fourcc("cgn"))throw FormatError("invalid CGN header");
    const auto count=read_i32(bytes,0xB8);
    if(count<1||static_cast<uint64_t>(count)*CampaignLevelRecord::kSize>bytes.size()-kHeaderSize)
        throw FormatError("CGN record count exceeds image");
    if(read_u32(bytes,4)>bytes.size())throw FormatError("CGN decoded size exceeds image");
    CampaignAsset result;std::copy_n(bytes.begin(),kHeaderSize,result.header_.begin());
    result.original_size_=bytes.size();result.records_.reserve(static_cast<std::size_t>(count));
    std::size_t offset=kHeaderSize;
    for(int32_t i=0;i<count;++i,offset+=CampaignLevelRecord::kSize)
        result.records_.push_back(CampaignLevelRecord::parse(bytes.subspan(offset,CampaignLevelRecord::kSize)));
    result.trailing_bytes_.assign(bytes.begin()+static_cast<std::ptrdiff_t>(offset),bytes.end());
    if(!result.find_level(result.first_level_id()))throw FormatError("CGN first level has no record");
    return result;
}
CampaignAsset CampaignAsset::from_template(const CampaignAsset& source,int32_t first,
                                           std::span<const CampaignLevelRecord> records){
    auto result=source;result.records_.assign(records.begin(),records.end());result.trailing_bytes_.clear();
    result.set_first_level_id(first);
    if(!result.authoring_errors().empty())throw FormatError(result.authoring_errors().front());
    return result;
}
int32_t CampaignAsset::first_level_id() const{return read_i32(header_,0xB4);}
void CampaignAsset::set_first_level_id(int32_t id){write_i32(header_,0xB4,id);}
int32_t CampaignAsset::saved_current_level_id() const{return read_i32(header_,0xC4);}
CampaignLevelRecord& CampaignAsset::record(std::size_t index){
    if(index>=records_.size())throw FormatError("campaign record index exceeds record count");
    return records_[index];
}
const CampaignLevelRecord* CampaignAsset::find_level(int32_t id) const noexcept{
    const auto found=std::find_if(records_.begin(),records_.end(),[&](const auto& r){return r.level_id()==id;});
    return found==records_.end()?nullptr:&*found;
}
CampaignContinuation CampaignAsset::continuation(int32_t current,std::size_t outcome) const{
    outcome_check(outcome);const auto* record=find_level(current);if(!record)return {};
    const auto id=record->successor(outcome);
    if(id<0)return {CampaignContinuationKind::NoTransition,id,{}};
    const auto* next=find_level(id);
    if(!next)return {CampaignContinuationKind::CampaignFinished,id,{}};
    return {CampaignContinuationKind::NextLevel,id,next->level_path()};
}
std::vector<int32_t> CampaignAsset::unreachable_level_ids() const{
    std::set<int32_t> visited;std::vector<int32_t> pending{first_level_id()};
    while(!pending.empty()){
        const auto id=pending.back();pending.pop_back();if(!visited.insert(id).second)continue;
        if(const auto* record=find_level(id))for(std::size_t o=0;o<8;++o){
            const auto target=record->successor(o);
            if(target>=0&&find_level(target)&&!visited.contains(target))pending.push_back(target);
        }
    }
    std::vector<int32_t> result;
    for(const auto& r:records_)if(!visited.contains(r.level_id()))result.push_back(r.level_id());
    return result;
}
std::vector<std::string> CampaignAsset::authoring_errors() const{
    std::vector<std::string> result;std::set<int32_t> ids;
    if(records_.empty())result.push_back("campaign must contain at least one map");
    if(!find_level(first_level_id()))result.push_back("campaign first level has no record");
    for(const auto& record:records_){
        if(!ids.insert(record.level_id()).second)result.push_back("duplicate campaign level id "+std::to_string(record.level_id()));
        if(record.level_id()<0)result.push_back("negative campaign level id is unreachable as a successor");
        if(record.level_path().empty())result.push_back("campaign level path is empty");
    }
    return result;
}
std::vector<std::byte> CampaignAsset::serialize() const{
    if(records_.empty()||!find_level(first_level_id()))throw FormatError("CGN first level has no record");
    const auto size=kHeaderSize+records_.size()*CampaignLevelRecord::kSize+trailing_bytes_.size();
    if(size>std::numeric_limits<uint32_t>::max())throw FormatError("CGN image is too large");
    std::vector<std::byte> result(header_.begin(),header_.end());result.reserve(size);
    for(const auto& record:records_)result.insert(result.end(),record.bytes().begin(),record.bytes().end());
    result.insert(result.end(),trailing_bytes_.begin(),trailing_bytes_.end());
    write_u32(result,0xB8,static_cast<uint32_t>(records_.size()));
    if(size!=original_size_)write_u32(result,4,static_cast<uint32_t>(size));
    return result;
}
} // namespace thandor
