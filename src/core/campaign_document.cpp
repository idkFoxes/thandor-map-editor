#include "thandor/campaign_document.hpp"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace thandor {
namespace {
std::string numbered(std::string_view prefix,unsigned slot,std::string_view extension) {
    std::ostringstream s;s<<prefix<<std::setfill('0')<<std::setw(2)<<slot<<extension;return s.str();
}
std::map<std::string,std::filesystem::path> archive_files(const std::filesystem::path& root) {
    std::map<std::string,std::filesystem::path> files;
    for(const auto& file:std::filesystem::directory_iterator(root))if(file.is_regular_file()) {
        const auto name=lower_ascii(file.path().filename().string());
        if(name.ends_with(".pck")&&!files.emplace(name,file.path()).second)
            throw FormatError("ambiguous archive filename: "+name);
    }
    return files;
}
// Supplied native bootstrap mounts PATCH00, descending numbered level add-ons, then stock.
// Do not inherit the asset browser's alphabetical order when exporting native packages.
class MountedGame {
public:
    explicit MountedGame(const std::filesystem::path& root):files(archive_files(root)) {
        if(files.contains("patch00.pck"))order.push_back("patch00.pck");
        for(int i=99;i>=0;--i) {
            const auto name=numbered("level",static_cast<unsigned>(i),".pck");
            if(files.contains(name))order.push_back(name);
        }
        for(const auto* name:{"daten.pck","modelle.pck","graphik.pck","sound.pck","filme.pck","level.pck","engine.pck"})
            if(files.contains(name))order.push_back(name);
    }
    const PckArchive& archive(const std::string& name) {
        auto& cached=archives[name];if(!cached)cached=std::make_unique<PckArchive>(PckArchive::open(files.at(name)));return *cached;
    }
    bool contains(std::string_view path) {
        const auto exact=canonical_pck_entry_path(path);
        for(const auto& name:order) {
            if(name=="filme.pck"&&!normalize_asset_path(path).ends_with(".flm"))continue;
            if(archive(name).find_exact(exact))return true;
        }
        return false;
    }
    std::optional<std::vector<std::byte>> optional(std::string_view path) {
        const auto exact=canonical_pck_entry_path(path);
        for(const auto& name:order) {
            // Movies are the only assets sourced from the large movie archive.
            if(name=="filme.pck"&&!normalize_asset_path(path).ends_with(".flm"))continue;
            const auto& p=archive(name);if(const auto* e=p.find_exact(exact))return p.extract(*e);
        }
        return std::nullopt;
    }
    std::vector<std::byte> load(std::string_view path) {
        auto bytes=optional(path);if(!bytes)throw FormatError("missing game asset: "+std::string(path));return std::move(*bytes);
    }
    std::map<std::string,std::filesystem::path> files;
    std::vector<std::string> order;
private:
    std::map<std::string,std::unique_ptr<PckArchive>> archives;
};
std::string safe_identifier(std::string_view value) {
    if(value.empty()||value.size()>30)throw FormatError("campaign identifier must contain 1..30 ASCII letters, digits or underscores");
    std::string s=lower_ascii(std::string(value));
    for(const char c:s)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))
        throw FormatError("campaign identifier must contain only ASCII letters, digits or underscores");
    return s;
}
std::string level_base(const CampaignLevelRecord& record) {
    return "level/"+normalize_asset_path(record.level_path());
}
void append_u32(std::vector<std::byte>& b,uint32_t value) {const auto n=b.size();b.resize(n+4);write_u32(b,n,value);}
void append_string(std::vector<std::byte>& b,std::string_view text) {
    if(text.size()>1024U*1024U)throw FormatError("campaign project text is too long");
    append_u32(b,static_cast<uint32_t>(text.size()));
    for(const unsigned char c:text)b.push_back(static_cast<std::byte>(c));
}
struct Reader {
    std::span<const std::byte> b;std::size_t at{};
    uint32_t number(){const auto v=read_u32(b,at);at+=4;return v;}
    std::string string(){const auto n=number();if(n>b.size()-at)throw FormatError("truncated campaign project text");
        std::string s;for(uint32_t i=0;i<n;++i)s.push_back(static_cast<char>(b[at++]));
        if(s.find('\0')!=std::string::npos)throw FormatError("campaign text contains NUL");
        return s;}
};
void merge_entry(std::map<std::string,PckWriteEntry>& entries,PckWriteEntry entry) {
    const auto key=normalize_asset_path(canonical_pck_entry_path(entry.name));
    const auto it=entries.find(key);
    if(it!=entries.end()) {
        if(it->second.data!=entry.data)throw FormatError("missions define different bytes for shared asset: "+key);
        return;
    }
    entry.name=key;entries.emplace(key,std::move(entry));
}
struct MissionData {LevelAsset level;std::vector<PckWriteEntry> entries;};
MissionData load_mission(const CampaignDocument& doc,const CampaignLevelRecord& record,
                         const AssetCatalog& catalog,MountedGame& mounts) {
    const auto& source=doc.source(record.level_id());
    if(!source.map_project.empty()) {
        const auto map=MapDocument::open_project(catalog,source.map_project);
        auto entries=map.campaign_package_entries();
        const auto expected=level_base(record)+".lev";
        const auto actual="level/"+map.map_name()+".lev";
        if(expected!=actual)throw FormatError("mission project map name differs from its campaign path: "+expected);
        return {map.level(),std::move(entries)};
    }
    auto bytes=mounts.load(level_base(record)+".lev");
    auto level=LevelAsset::parse(bytes);
    const auto terrain=normalize_asset_path(level.paths().level)+".fld";
    if(!terrain.starts_with("level/"))throw FormatError("mission terrain path is outside level/: "+terrain);
    auto field=mounts.load(terrain);(void)FieldGrid::parse(field);
    std::vector<PckWriteEntry> entries{{level_base(record)+".lev",std::move(bytes),0},{terrain,std::move(field),0}};
    if(auto text=mounts.optional(level_base(record)+".str"))entries.push_back({level_base(record)+".str",std::move(*text),0});
    return {std::move(level),std::move(entries)};
}
void append_messages(CampaignValidation& result,const CampaignValidation& child,std::string_view prefix) {
    for(const auto& s:child.errors)result.errors.push_back(std::string(prefix)+s);
    for(const auto& s:child.warnings)result.warnings.push_back(std::string(prefix)+s);
}
void check_dependencies(const MissionData& mission,std::map<std::string,PckWriteEntry>& entries,
                        MountedGame& mounts,CampaignValidation& result,const std::string& prefix) {
    for(const auto& e:mission.entries)merge_entry(entries,e);
    const auto exists=[&](const std::string& path){return entries.contains(normalize_asset_path(path))||mounts.contains(path);};
    const auto image=mission.level.fixed_image();
    constexpr std::array<const char*,4> suffixes{".arm",".mdl",".eff",".sht"};
    for(std::size_t table=0;table<4;++table) {
        const auto count=read_u32(image,0xE0+table*8),offset=read_u32(image,0xE4+table*8);
        for(uint32_t i=0;i<count;++i) {
            auto path=normalize_asset_path(read_utf16le_z(image,offset+i*0x40,0x40));
            if(path.empty())continue;
            const auto slash=path.find_last_of('/'),dot=path.find_last_of('.');
            if(dot!=std::string::npos&&(slash==std::string::npos||dot>slash))path.resize(dot);
            path+=suffixes[table];
            if(!exists(path))result.errors.push_back(prefix+"missing referenced resource: "+path);
        }
    }
    const auto environment=normalize_asset_path(mission.level.paths().ending_movie);
    if(environment.empty()||!exists(environment+".flm"))result.errors.push_back(prefix+"missing required environment movie: "+environment+".flm");
    const auto ground=normalize_asset_path(mission.level.paths().ground_textures);
    if(!ground.empty()) {
        if(!exists(ground+".dat"))result.errors.push_back(prefix+"missing ground texture table: "+ground+".dat");
        for(unsigned i=0;i<26;++i) {
            const auto path=ground+static_cast<char>('a'+i)+".gfx";
            if(!exists(path))result.errors.push_back(prefix+"missing terrain texture: "+path);
        }
    }
}
bool blank(const TextResourceAsset& page,uint32_t index) {
    for(const auto country:page.country_codes())if(index<page.string_count(country)&&!page.string(index,country).empty())return false;
    return true;
}
std::vector<CampaignCatalogRecord> campaign_catalog(MountedGame& mounts) {
    std::vector<CampaignCatalogRecord> result;
    auto merge=[&](std::string_view path) {
        const auto bytes=mounts.optional(path);if(!bytes)return;
        if(bytes->size()%0x100)throw FormatError("campaign catalog is not an array of 0x100-byte records");
        for(std::size_t i=0;i<bytes->size();i+=0x100) {
            auto r=CampaignCatalogRecord::parse(std::span(*bytes).subspan(i,0x100));
            auto it=std::find_if(result.begin(),result.end(),[&](const auto& old){return old.identifier()==r.identifier();});
            if(it==result.end())result.push_back(std::move(r));else *it=std::move(r);
        }
    };
    merge("level/campagne.dat");for(unsigned i=0;i<100;++i)merge(numbered("level/campagne",i,".dat"));
    return result;
}
struct PreparedCampaign {CampaignValidation validation;std::map<std::string,PckWriteEntry> entries;};
PreparedCampaign prepare(const CampaignDocument& doc,const AssetCatalog& catalog,MountedGame& mounts) {
    PreparedCampaign result;auto& report=result.validation;
    report.errors=doc.campaign().authoring_errors();
    try{(void)safe_identifier(doc.identifier());}catch(const std::exception& e){report.errors.push_back(e.what());}
    if(doc.title.empty())report.errors.push_back("campaign title is empty");
    if(doc.title.size()>2047||doc.description.size()>16383)report.errors.push_back("campaign title or description is too long");
    if(doc.missions.size()!=doc.campaign().records().size())report.errors.push_back("mission source count does not match campaign records");
    std::set<int32_t> sources;
    for(const auto& s:doc.missions) {
        if(!sources.insert(s.level_id).second)report.errors.push_back("duplicate mission source ID "+std::to_string(s.level_id));
        if(!doc.campaign().find_level(s.level_id))report.errors.push_back("mission source has no campaign record");
    }
    for(const auto id:doc.campaign().unreachable_level_ids())report.warnings.push_back("Map "+std::to_string(id)+" is unreachable from the starting map.");
    for(const auto& r:doc.campaign().records()) {
        const auto prefix="Map "+std::to_string(r.level_id())+": ";
        try {
            (void)canonical_pck_entry_path(level_base(r));
            auto mission=load_mission(doc,r,catalog,mounts);
            append_messages(report,validate_campaign_mission_script(mission.level),prefix);
            check_dependencies(mission,result.entries,mounts,report,prefix);
            std::set<uint8_t> outcomes;
            for(uint32_t i=0;i<16;++i){const auto t=mission.level.end_condition_trigger(i);if(t.state_flags==1)outcomes.insert(t.end_movie_selection_index);}
            if(outcomes.empty())report.warnings.push_back(prefix+"no active end trigger; this map cannot advance through mission conditions.");
            for(const auto o:outcomes)if(o<8) {
                std::set<int32_t> movies{r.end_movie(o,false),r.end_movie(o,true)};
                for(const auto movie:movies) {
                    std::ostringstream path;path<<"flm/ende"<<std::setfill('0')<<std::setw(4)<<movie<<".flm";
                    if(!mounts.contains(path.str()))report.warnings.push_back(prefix+"outcome "+std::to_string(o)+" ending movie is absent: "+path.str()+" (game falls back to results).");
                }
                const auto next=doc.campaign().continuation(r.level_id(),o);
                if(next.kind==CampaignContinuationKind::NoTransition)report.warnings.push_back(prefix+"outcome "+std::to_string(o)+" keeps the current campaign without loading a map.");
                else if(o!=0U&&next.successor_id==r.level_id()&&next.kind==CampaignContinuationKind::NextLevel)
                    report.warnings.push_back(prefix+"outcome "+std::to_string(o)+" replays this map.");
            }
            for(unsigned f=0;f<8;++f)if(r.exit_zone(f).radius_q12<0)report.errors.push_back(prefix+"exit zone radius is negative.");
        }catch(const std::exception& e){report.errors.push_back(prefix+e.what());}
    }
    return result;
}
} // namespace

CampaignCatalogRecord CampaignCatalogRecord::parse(std::span<const std::byte> bytes) {
    if(bytes.size()!=0x100)throw FormatError("campaign catalog record must contain 0x100 bytes");
    CampaignCatalogRecord r;std::copy(bytes.begin(),bytes.end(),r.bytes_.begin());return r;
}
std::string CampaignCatalogRecord::identifier() const{return read_utf16le_z(bytes_,0,0x40);}
void CampaignCatalogRecord::set_identifier(std::string_view name){auto updated=bytes_;write_utf16le_fixed(updated,0,0x40,safe_identifier(name)+".");bytes_=updated;}
uint32_t CampaignCatalogRecord::title_index() const{return read_u32(bytes_,0x50);}
void CampaignCatalogRecord::set_title_index(uint32_t index){if(index>0xDF)throw FormatError("campaign title exceeds compact page 0x22");write_u32(bytes_,0x50,index);}
std::vector<std::byte> CampaignCatalogRecord::serialize() const{return {bytes_.begin(),bytes_.end()};}

CampaignDocument CampaignDocument::import_native(const std::filesystem::path& root,std::string_view path) {
    MountedGame mounts(root);CampaignDocument d;d.campaign_=CampaignAsset::parse(mounts.load(path));
    d.identifier_=lower_ascii(std::filesystem::path(normalize_asset_path(path)).stem().string());
    auto bytes=d.campaign_.serialize();d.catalog_record_=CampaignCatalogRecord::parse(std::span(bytes).subspan(0x100,0x100));
    for(const auto& c:campaign_catalog(mounts))if(c.identifier()==d.identifier_+".")d.catalog_record_=c;
    const auto index=d.catalog_record_.title_index();
    const auto names=TextResourceAsset::parse(mounts.load("texte/level.str"));
    const auto descriptions=TextResourceAsset::parse(mounts.load("texte/inhalt.str"));
    d.title=(index<=0xDF&&index+0x20<names.string_count())?names.string(index+0x20):d.identifier_;
    d.description=index<descriptions.string_count()?descriptions.string(index):"";
    for(const auto& r:d.campaign_.records())d.missions.push_back({r.level_id(),{}, {}});
    return d;
}
CampaignDocument CampaignDocument::create(const CampaignDocument& base,std::string_view identifier) {
    auto d=base;d.set_identifier(identifier);d.title="New campaign";d.description.clear();
    auto first=*base.campaign_.find_level(base.campaign_.first_level_id());first.set_level_id(1);
    for(unsigned o=0;o<8;++o){first.set_successor(o,0);first.set_carry_over(o,false);first.set_retain_previous_carry_over(o,false);}
    d.campaign_=CampaignAsset::from_template(base.campaign_,1,std::span(&first,1));
    d.missions={{1,{},{}}};return d;
}
void CampaignDocument::set_identifier(std::string_view value){const auto id=safe_identifier(value);auto r=catalog_record_;r.set_identifier(id);catalog_record_=r;identifier_=id;}
CampaignMissionSource& CampaignDocument::source(int32_t id) {
    auto it=std::find_if(missions.begin(),missions.end(),[&](const auto& m){return m.level_id==id;});
    if(it==missions.end())throw FormatError("campaign map has no source binding");
    return *it;
}
const CampaignMissionSource& CampaignDocument::source(int32_t id) const {
    const auto it=std::find_if(missions.begin(),missions.end(),[&](const auto& m){return m.level_id==id;});
    if(it==missions.end())throw FormatError("campaign map has no source binding");
    return *it;
}
void CampaignDocument::add_map_project(const AssetCatalog& catalog,const std::filesystem::path& path) {
    const auto absolute=std::filesystem::absolute(path).lexically_normal();const auto map=MapDocument::open_project(catalog,absolute);
    std::set<int32_t> occupied;
    for(const auto& record:campaign_.records()) {occupied.insert(record.level_id());for(unsigned o=0;o<8;++o)occupied.insert(record.successor(o));}
    int32_t id=1;while(occupied.contains(id)){if(id==std::numeric_limits<int32_t>::max())throw FormatError("no campaign IDs remain");++id;}
    auto records=std::vector<CampaignLevelRecord>(campaign_.records().begin(),campaign_.records().end());
    auto r=CampaignLevelRecord::minimal(id,map.map_name());records.push_back(r);
    auto next=CampaignAsset::from_template(campaign_,campaign_.first_level_id(),records);
    missions.push_back({id,absolute,{}});campaign_=std::move(next);
}
void CampaignDocument::remove_mission(int32_t id) {
    if(!campaign_.find_level(id))throw FormatError("campaign map ID does not exist");
    if(campaign_.records().size()<=1)throw FormatError("keep at least one campaign map");
    if(id==campaign_.first_level_id())throw FormatError("choose a different starting map before removing this map");
    // Removing a referenced map would reinterpret its ID as campaign completion.
    for(const auto& r:campaign_.records())if(r.level_id()!=id)for(unsigned o=0;o<8;++o)
        if(r.successor(o)==id)throw FormatError("change incoming outcome links before removing this map");
    auto records=std::vector<CampaignLevelRecord>(campaign_.records().begin(),campaign_.records().end());
    std::erase_if(records,[&](const auto& r){return r.level_id()==id;});
    auto next=CampaignAsset::from_template(campaign_,campaign_.first_level_id(),records);
    std::erase_if(missions,[&](const auto& s){return s.level_id==id;});campaign_=std::move(next);
}
void CampaignDocument::set_mission_id(int32_t previous,int32_t next) {
    if(previous==next)return;
    if(next<0||campaign_.find_level(next))throw FormatError("campaign map ID must be nonnegative and unique");
    if(!campaign_.find_level(previous))throw FormatError("campaign map ID does not exist");
    // Do not turn an existing terminal sentinel into an unintended next-map link.
    for(const auto& r:campaign_.records())for(unsigned o=0;o<8;++o)if(r.successor(o)==next)
        throw FormatError("new ID is already used as a terminal outcome; change those links first");
    auto& binding=source(previous);
    for(std::size_t i=0;i<campaign_.records().size();++i) {
        auto& r=campaign_.record(i);if(r.level_id()==previous)r.set_level_id(next);
        for(unsigned o=0;o<8;++o)if(r.successor(o)==previous)r.set_successor(o,next);
    }
    if(campaign_.first_level_id()==previous)campaign_.set_first_level_id(next);
    binding.level_id=next;
}
std::string CampaignDocument::outcome_label(int32_t id,std::size_t outcome) const {
    if(outcome>=8)throw FormatError("campaign outcome must be 0..7");
    const auto& name=source(id).outcome_names[outcome];return name.empty()?"Outcome "+std::to_string(outcome):name;
}
std::string CampaignDocument::continuation_label(int32_t id,std::size_t outcome) const {
    const auto next=campaign_.continuation(id,outcome);
    if(next.kind==CampaignContinuationKind::CampaignFinished)return "Finish campaign";
    if(next.kind==CampaignContinuationKind::NoTransition)return "Keep current campaign";
    if(next.successor_id==id)return "Replay this map";
    return std::to_string(next.successor_id)+" - "+next.level_path;
}
void CampaignDocument::save_project(const std::filesystem::path& path) const {
    const auto errors=campaign_.authoring_errors();if(!errors.empty())throw FormatError(errors.front());
    if(missions.size()!=campaign_.records().size())throw FormatError("mission source count does not match campaign records");
    std::set<int32_t> source_ids;
    for(const auto& m:missions)if(!campaign_.find_level(m.level_id)||!source_ids.insert(m.level_id).second)
        throw FormatError("campaign source ID is missing or duplicated");
    std::vector<std::byte> metadata;append_u32(metadata,fourcc("tcmp"));append_u32(metadata,1);
    append_string(metadata,identifier_);append_string(metadata,title);append_string(metadata,description);
    append_u32(metadata,static_cast<uint32_t>(missions.size()));
    const auto parent=std::filesystem::absolute(path).parent_path();
    for(const auto& m:missions) {
        append_u32(metadata,static_cast<uint32_t>(m.level_id));
        auto relative=m.map_project.empty()?std::filesystem::path{}:std::filesystem::absolute(m.map_project).lexically_relative(parent);
        if(relative.empty()&&!m.map_project.empty())relative=std::filesystem::absolute(m.map_project);
        append_string(metadata,relative.generic_string());
        for(const auto& name:m.outcome_names)append_string(metadata,name);
    }
    while(metadata.size()%4)metadata.push_back(std::byte{0});
    const std::vector<PckWriteEntry> entries{{"editor/campaign.bin",std::move(metadata),0},
        {"editor/campaign.cgn",campaign_.serialize(),0},{"editor/campaign-catalog.dat",catalog_record_.serialize(),0}};
    std::filesystem::create_directories(parent);write_stored_pck(path,entries);
}
CampaignDocument CampaignDocument::open_project(const std::filesystem::path& path) {
    const auto archive=PckArchive::open(path);const auto bytes=archive.extract("editor/campaign.bin");Reader reader{bytes};
    if(reader.number()!=fourcc("tcmp")||reader.number()!=1)throw FormatError("unsupported campaign project");
    CampaignDocument d;d.campaign_=CampaignAsset::parse(archive.extract("editor/campaign.cgn"));
    d.catalog_record_=CampaignCatalogRecord::parse(archive.extract("editor/campaign-catalog.dat"));
    d.set_identifier(reader.string());d.title=reader.string();d.description=reader.string();const auto count=reader.number();
    if(count!=d.campaign_.records().size())throw FormatError("campaign project source count mismatch");
    std::set<int32_t> ids;
    for(uint32_t i=0;i<count;++i) {
        CampaignMissionSource m;m.level_id=static_cast<int32_t>(reader.number());const auto source=reader.string();
        if(!source.empty())m.map_project=(std::filesystem::absolute(path).parent_path()/std::filesystem::path(source)).lexically_normal();
        for(auto& name:m.outcome_names)name=reader.string();
        if(!d.campaign_.find_level(m.level_id)||!ids.insert(m.level_id).second)throw FormatError("campaign project source ID mismatch");
        d.missions.push_back(std::move(m));
    }
    if(bytes.size()-reader.at>3)throw FormatError("campaign project has trailing metadata");
    while(reader.at<bytes.size())if(bytes[reader.at++]!=std::byte{0})throw FormatError("campaign project padding is not zero");
    return d;
}
CampaignValidation CampaignDocument::validate(const AssetCatalog& catalog) const {
    MountedGame mounts(catalog.game_root());return prepare(*this,catalog,mounts).validation;
}
unsigned CampaignDocument::first_free_package_slot(const std::filesystem::path& root) {
    MountedGame mounts(root);
    for(unsigned i=0;i<100;++i)if(!mounts.files.contains(numbered("level",i,".pck"))&&
        !mounts.contains(numbered("level/campagne",i,".dat"))&&!mounts.contains(numbered("level/level",i,".dat")))return i;
    throw FormatError("all 100 level package slots are occupied");
}
CampaignPackageResult CampaignDocument::export_package(const AssetCatalog& catalog,
    const std::filesystem::path& output,unsigned slot) const {
    if(slot>99)throw FormatError("campaign package slot must be 00..99");
    if(std::filesystem::exists(output))throw FormatError("choose a new export directory");
    const auto destination=std::filesystem::weakly_canonical(output);
    const auto in_game=destination.lexically_relative(std::filesystem::weakly_canonical(catalog.game_root()));
    if(!in_game.empty()&&*in_game.begin()!="..")
        throw FormatError("export outside the game installation");
    MountedGame mounts(catalog.game_root());const auto package_name=numbered("level",slot,".pck");
    if(mounts.files.contains(package_name))throw FormatError("level package slot is occupied; choose a free slot");
    auto prepared=prepare(*this,catalog,mounts);if(!prepared.validation.ok())throw FormatError(prepared.validation.errors.front());
    auto& entries=prepared.entries;auto warnings=prepared.validation.warnings;
    const auto identifier=identifier_+".";
    const auto catalog_entries=campaign_catalog(mounts);
    for(const auto& existing:catalog_entries)if(existing.identifier()==identifier)
        throw FormatError("campaign identifier is already registered; choose a new identifier");
    const auto cgn_path="level/"+identifier_+".cgn";
    if(mounts.optional(cgn_path))throw FormatError("campaign path already exists in the game");
    const auto catalog_path=numbered("level/campagne",slot,".dat");
    if(mounts.optional(catalog_path))throw FormatError("campaign catalog slot is occupied; choose another package slot");
    auto page22=TextResourceAsset::parse(mounts.load("texte/level.str"));
    auto page23=TextResourceAsset::parse(mounts.load("texte/inhalt.str"));
    std::set<uint32_t> used,used_descriptions;
    for(const auto& r:catalog_entries)used.insert(r.title_index());
    // Both campaign and single-map titles share compact page 0x22. The fixed UI occupies its prefix.
    auto protect_map_catalog=[&](std::string_view path) {
        if(const auto bytes=mounts.optional(path))for(const auto& r:parse_scenario_catalog_records(*bytes)) {
            const auto alias=r.alias_base();
            if(alias<=0xCF) {
                used.insert(alias+0x10);
                for(uint32_t line=0;line<15;++line)used_descriptions.insert(alias*16+16+line);
            }
        }
    };
    protect_map_catalog("level/level.dat");for(unsigned i=0;i<100;++i)protect_map_catalog(numbered("level/level",i,".dat"));
    const auto mount_alias=game_custom_alias_for_slot(slot);
    used.insert(mount_alias+0x10);
    uint32_t title_index=0xE0;
    for(uint32_t i=0x70;i<=0xDF;++i)if(!used.contains(i)&&!used_descriptions.contains(i)&&blank(page22,i+0x20)&&blank(page23,i)){title_index=i;break;}
    if(title_index>0xDF)throw FormatError("no unused campaign title and description pair remains in compact text page 0x22");
    page22.ensure_string_count_all_locales(title_index+0x21);page23.ensure_string_count_all_locales(title_index+1);
    page22.replace_plain_string_all_locales(title_index+0x20,title);page23.replace_plain_string_all_locales(title_index,description);
    auto record=catalog_record_;record.set_identifier(identifier_);record.set_title_index(title_index);
    auto cgn=campaign_.serialize();const auto record_bytes=record.serialize();
    std::copy(record_bytes.begin(),record_bytes.end(),cgn.begin()+0x100);
    merge_entry(entries,{cgn_path,std::move(cgn),0});merge_entry(entries,{catalog_path,record_bytes,0});
    // Detect collisions before staging; byte-identical stock dependencies may be shared.
    for(const auto& [path,e]:entries)if(const auto old=mounts.optional(path);old&&*old!=e.data)
        throw FormatError("campaign would replace an existing game asset: "+path+". Rename the authored map or custom asset first.");
    const std::string patch_name="patch00.pck";
    std::map<std::string,PckWriteEntry> patch_entries;std::vector<std::byte> patch_header;
    if(mounts.files.contains(patch_name)) {
        const auto& patch=mounts.archive(patch_name);patch_header.assign(patch.header_bytes().begin(),patch.header_bytes().end());
        for(const auto& e:patch.entries())patch_entries.emplace(e.normalized_name,PckWriteEntry{e.name,patch.extract(e),e.type_tag});
    }
    patch_entries["texte/level.str"]={"texte/level.str",page22.serialize(),0};
    patch_entries["texte/inhalt.str"]={"texte/inhalt.str",page23.serialize(),0};
    // LevelPackage_ValidateAndMount checks the first level\*.lev and level\*.str before retaining a package.
    // Keep that compatibility pair independent of campaign mission aliases and optional mission STRs.
    const auto start=level_base(*campaign_.find_level(campaign_.first_level_id()))+".lev";
    auto mount_level=LevelAsset::parse(entries.at(start).data);
    if(read_u32(mount_level.fixed_image(),0x0C)!=0x00070001)throw FormatError("package mount LEV has an unsupported converter version");
    mount_level.set_compatibility_alias_base(mount_alias);
    const auto mount_base="level/"+identifier_+"_mount";
    if(entries.contains(mount_base+".lev")||entries.contains(mount_base+".str")||mounts.optional(mount_base+".lev")||mounts.optional(mount_base+".str"))
        throw FormatError("package compatibility path is already occupied");
    std::vector<PckWriteEntry> package_vector{
        {mount_base+".lev",mount_level.serialize(),0},
        {mount_base+".str",TextResourceAsset::minimal_level_compatibility(title).serialize(),0}};
    std::vector<PckWriteEntry> patch_vector;
    for(auto& [key,e]:entries){(void)key;package_vector.push_back(std::move(e));}
    for(auto& [key,e]:patch_entries){(void)key;patch_vector.push_back(std::move(e));}
    const auto package_bytes=build_stored_pck(package_vector);const auto patch_bytes=build_stored_pck(patch_vector,patch_header);
    // Build and parse both complete archives before publishing any output.
    const auto verified=PckArchive::parse(package_name,package_bytes);
    const auto linked=CampaignAsset::parse(verified.extract(cgn_path));
    if(linked.first_level_id()!=campaign_.first_level_id())throw FormatError("exported campaign start ID mismatch");
    (void)PckArchive::parse(patch_name,patch_bytes);
    std::ostringstream readme;
    readme<<"Campaign: "<<title<<"\nIdentifier: "<<identifier_<<"\nStart map ID: "<<campaign_.first_level_id()
          <<"\nMissions: "<<campaign_.records().size()<<"\nTitle resource: 0x"<<std::hex<<(0x2220+title_index)
          <<"\nDescription resource: 0x"<<(0x230000+title_index)<<std::dec
          <<"\n\nBuilt for game installation: "<<catalog.game_root().string()
          <<"\nClose the game. Back up the existing "<<patch_name<<". Copy both PCK files next to game.exe, then restart the game."
          <<"\nThe patch is a complete merged replacement for this installation, not a portable standalone text patch."
          <<" Re-export if installed packages change. Mission and stock resource dependencies require this game installation."
          <<"\nOriginal retail executable playtesting has not been performed by the exporter.\n";
    for(const auto& w:warnings)readme<<"\nWarning: "<<w;
    const auto notes=readme.str();std::vector<std::byte> note_bytes;for(unsigned char c:notes)note_bytes.push_back(static_cast<std::byte>(c));
    std::filesystem::create_directories(destination.parent_path());
    // New-directory contract prevents partial writes from replacing an earlier export.
    if(!std::filesystem::create_directory(destination))throw FormatError("export directory already exists");
    try {
        write_file_atomic(destination/package_name,package_bytes);write_file_atomic(destination/patch_name,patch_bytes);
        write_file_atomic(destination/"INSTALL.txt",note_bytes);
    }catch(...){std::error_code error;std::filesystem::remove_all(destination,error);throw;}
    return {destination/package_name,destination/patch_name,title_index,campaign_.records().size(),std::move(warnings)};
}

CampaignValidation validate_campaign_mission_script(const LevelAsset& level) {
    CampaignValidation r;using K=LevelScheduledConditionKind;
    for(uint32_t i=0;i<64;++i) {
        const auto c=level.scheduled_condition(i);const auto prefix="Condition "+std::to_string(i)+": ";
        const auto kind=static_cast<uint8_t>(c.kind());
        if(kind>0x1A){r.errors.push_back(prefix+"unknown predicate kind");continue;}
        if(c.kind()==K::BooleanPostfixExpression) {
            int depth=0;bool end=false;
            for(const auto token:c.payload_bytes) {
                if(token==0xFC){end=true;break;}
                if(token<64) {++depth;if(token>=i)r.warnings.push_back(prefix+"reference to C"+std::to_string(token)+" reads the previous pass when evaluated before it.");}
                else if(token==0xFD){if(depth<1){r.errors.push_back(prefix+"NOT needs one operand");break;}}
                else if(token==0xFE||token==0xFF){if(depth<2){r.errors.push_back(prefix+"AND/OR needs two operands");break;}--depth;}
                else {r.errors.push_back(prefix+"invalid expression token");break;}
            }
            if(!end||depth!=1)r.errors.push_back(prefix+"expression needs one result and an END token");
        } else if(kind!=0&&c.kind()!=K::CountdownElapsed&&c.kind()!=K::NoActiveEntityWithClassIdOutsideClassCommandGroupA) {
            if(c.operand(0)>7)r.errors.push_back(prefix+"faction operand is outside 0..7");
            if(c.kind()==K::FactionInactiveOrRelationAtLeast8&&c.operand(1)>7)r.errors.push_back(prefix+"second faction is outside 0..7");
        }
        if(c.kind()==K::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast&&c.operand(1)==0)
            r.warnings.push_back(prefix+"zero count threshold follows the native decrement quirk and is false.");
    }
    for(uint32_t i=0;i<16;++i) {
        const auto t=level.end_condition_trigger(i);if(t.state_flags!=1)continue;
        const auto prefix="Trigger "+std::to_string(i)+": ";
        if(t.faction_runtime_index>7||t.end_movie_selection_index>7||t.condition_index>63)r.errors.push_back(prefix+"faction, outcome or condition is out of range");
        else if(level.scheduled_condition(t.condition_index).kind()==K::NoneOrUnused)r.warnings.push_back(prefix+"references an unused condition");
    }
    return r;
}
LevelScheduledCondition campaign_expression_from_text(std::string_view text) {
    LevelScheduledCondition result;result.status_and_kind=0x1A;result.payload_bytes.fill(0xFC);
    std::istringstream input{std::string(text)};std::string token;std::size_t at=0;int depth=0;bool ended=false;
    while(input>>token) {
        if(ended)throw FormatError("END must be the final expression token");
        token=lower_ascii(std::move(token));
        if(token=="end"){ended=true;continue;}
        if(at>=14)throw FormatError("expression has room for 14 tokens plus END");
        uint8_t value=0;
        if(token=="not"){value=0xFD;if(depth<1)throw FormatError("NOT needs one preceding operand");}
        else if(token=="and"||token=="or") {
            value=token=="and"?0xFE:0xFF;if(depth<2)throw FormatError("AND/OR needs two preceding operands");--depth;
        } else {
            unsigned slot=64;
            if(token.size()<2||token.front()!='c')throw FormatError("use C0..C63, NOT, AND and OR in postfix order");
            const auto parsed=std::from_chars(token.data()+1,token.data()+token.size(),slot);
            if(parsed.ec!=std::errc{}||parsed.ptr!=token.data()+token.size()||slot>63)throw FormatError("condition reference must be C0..C63");
            value=static_cast<uint8_t>(slot);++depth;
        }
        result.payload_bytes[at++]=value;
    }
    if(depth!=1)throw FormatError("expression must leave exactly one result");
    return result;
}
std::string campaign_expression_text(const LevelScheduledCondition& condition) {
    std::string result;
    for(const auto token:condition.payload_bytes) {
        if(token==0xFC)break;
        if(!result.empty())result+=' ';
        if(token<64)result+="C"+std::to_string(token);
        else if(token==0xFD)result+="NOT";
        else if(token==0xFE)result+="AND";
        else if(token==0xFF)result+="OR";
        else result+="INVALID("+std::to_string(token)+")";
    }
    return result;
}
std::array<float,5> campaign_exit_zone_render(const CampaignLevelRecord::ExitZone& z) {
    return {q12_to_float(z.center_y_q12),q12_to_float(z.center_x_q12),q12_to_float(z.radius_q12),
            q12_to_float(z.destination_y_q12),q12_to_float(z.destination_x_q12)};
}
CampaignLevelRecord::ExitZone campaign_exit_zone_from_render(const std::array<float,5>& z) {
    for(float v:z)if(!std::isfinite(v)||v<-524287.0F||v>524287.0F)throw FormatError("exit zone coordinates exceed Q12 range");
    if(z[2]<0)throw FormatError("exit zone radius must be nonnegative");
    return {float_to_q12(z[1]),float_to_q12(z[0]),float_to_q12(z[2]),float_to_q12(z[4]),float_to_q12(z[3])};
}
} // namespace thandor
