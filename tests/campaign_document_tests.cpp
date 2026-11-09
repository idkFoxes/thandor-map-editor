#include "thandor/campaign_document.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace fs=std::filesystem;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid campaign operation accepted");}
std::vector<std::byte> cgn_fixture() {
    auto r=CampaignLevelRecord::minimal(1,"test/first");
    std::vector<std::byte> b(0x380);write_u32(b,0,fourcc("cgn"));write_u32(b,4,0x380);write_i32(b,0xB4,1);write_i32(b,0xB8,1);
    write_u32(b,0x50,0xAABBCCDD); // unrelated common header must survive creation/export
    auto catalog=ScenarioCatalogRecord::minimal("testcamp",0).serialize();write_u32(catalog,0x50,0);
    std::copy(catalog.begin(),catalog.end(),b.begin()+0x100);std::copy(r.bytes().begin(),r.bytes().end(),b.begin()+0x200);return b;
}
std::vector<std::byte> lev_fixture(std::string_view name="level/test/first") {
    std::vector<std::byte> b(0x1000);write_u32(b,0,fourcc("lev"));write_u32(b,4,0x1000);write_u32(b,0x0C,0x70001);write_u32(b,0xDC,0x1000);
    constexpr unsigned fields[]{0xB0,0xB4,0xB8,0xBC,0xC0,0xC4,0xC8,0xCC,0xD0,0xD4};
    for(unsigned i=0;i<10;++i)write_u32(b,fields[i],0x800+i*0x60);
    write_utf16le_fixed(b,0x800,0x60,name);write_utf16le_fixed(b,0x800+7*0x60,0x60,"flm/test");write_u32(b,0x310,1);write_u32(b,0x314,2);
    auto l=LevelAsset::parse(b);LevelScheduledCondition c;c.status_and_kind=0x14;c.set_operand(1,1);l.set_scheduled_condition(0,c);
    LevelEndConditionTrigger t;t.state_flags=1;t.faction_runtime_index=1;t.condition_index=0;t.end_movie_selection_index=1;l.set_end_condition_trigger(0,t);t.end_movie_selection_index=0;l.set_end_condition_trigger(1,t);return l.serialize();
}
int main(int argc,char** argv)try {
    const auto root=fs::temp_directory_path()/("thandor-campaign-v761-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"game");struct Cleanup{fs::path p;~Cleanup(){std::error_code e;fs::remove_all(p,e);}} cleanup{root};
    const auto input=cgn_fixture();auto page22=TextResourceAsset::minimal_level_compatibility("ui");page22.ensure_string_count_all_locales(256);
    page22.replace_plain_string_all_locales(0x20,"Original campaign");page22.replace_plain_string_all_locales(0x90,"Occupied title");
    auto page23=TextResourceAsset::minimal_level_compatibility("content");page23.ensure_string_count_all_locales(0x100);page23.replace_plain_string_all_locales(0,"Original description");
    page23.replace_plain_string_all_locales(0x71,"Occupied description");
    auto base_record=ScenarioCatalogRecord::minimal("testcamp",0).serialize();write_u32(base_record,0x50,0);
    std::vector<PckWriteEntry> stock{{"level/testcamp.cgn",input,0},{"level/campagne.dat",base_record,0},
        {"texte/level.str",page22.serialize(),0},{"texte/inhalt.str",page23.serialize(),0},
        {"level/test/first.lev",lev_fixture(),0},{"level/test/first.fld",FieldGrid::create(59,59,0,0).serialize(),0},{"flm/test.flm",std::vector<std::byte>(4),0}};
    write_stored_pck(root/"game/LEVEL.PCK",stock);
    const auto catalog=AssetCatalog::scan_archive_root(root/"game");
    auto imported=CampaignDocument::import_native(root/"game","level/testcamp.cgn");
    check(imported.title=="Original campaign"&&imported.description=="Original description","campaign title index confused with planet metadata");
    check(imported.campaign().serialize()==input,"native import altered common header/records");
    auto doc=CampaignDocument::create(imported,"my_branch");doc.title="Branch and replay";doc.description="Two native mission outcomes";
    doc.source(1).outcome_names[1]="Victory";
    auto second=CampaignLevelRecord::minimal(2,"test/second");auto first=doc.campaign().record(0);first.set_successor(1,2);first.set_successor(0,1);
    std::array records{first,second};doc.campaign()=CampaignAsset::from_template(doc.campaign(),1,records);doc.missions.push_back({2,{},{}});
    stock.push_back({"level/test/second.lev",lev_fixture("level/test/first"),0}); // shared field identity, no second.fld
    write_stored_pck(root/"game/LEVEL.PCK",stock);
    const auto report=doc.validate(catalog);
    for(const auto& error:report.errors)std::cerr<<error<<'\n';
    check(report.ok(),"two mission validation failed");
    for(const auto& warning:report.warnings)check(warning.find("outcome 0 replays this map")==std::string::npos,
        "normal defeat replay produced a campaign warning");
    doc.save_project(root/"project/branch.thcampaign");auto reopened=CampaignDocument::open_project(root/"project/branch.thcampaign");
    check(reopened.campaign().serialize()==doc.campaign().serialize(),"project changed native CGN bytes");
    check(reopened.outcome_label(1,1)=="Victory","project lost named outcomes");
    check(reopened.continuation_label(1,0)=="Replay this map"&&reopened.continuation_label(2,1)=="Finish campaign","friendly outcome semantics differ from native IDs");
    auto result=reopened.export_package(catalog,root/"export",0);
    check(result.title_index==0x72,"allocator overwrote occupied title/description or bootstrap alias");
    auto package=PckArchive::open(result.level_package);auto patch=PckArchive::open(result.text_patch);
    check(package.find_exact("level\\my_branch.cgn")&&package.find_exact("level\\campagne00.dat"),"missing exact native campaign paths");
    check(package.find("level/test/first.fld")&&!package.find("level/test/second.fld"),"shared terrain link was guessed from CGN map name");
    check(package.extract("level/test/first.lev")==lev_fixture(),"export modified authored LEV resources/factions/script");
    check(package.entries()[0].normalized_name=="level/my_branch_mount.lev"&&package.entries()[1].normalized_name=="level/my_branch_mount.str","numbered-package mount compatibility pair is not first");
    auto mount=LevelAsset::parse(package.extract(package.entries()[0]));check(mount.compatibility_alias_base()==game_custom_alias_for_slot(0),"unsafe mount alias");
    auto text=TextResourceAsset::parse(patch.extract("texte/level.str"));auto content=TextResourceAsset::parse(patch.extract("texte/inhalt.str"));
    check(text.string(0x20)=="Original campaign"&&text.string(0x90)=="Occupied title","existing UI/campaign text overwritten");
    for(auto country:text.country_codes())check(text.string(result.title_index+0x20,country)==doc.title,"title missing from a locale");
    check(content.string(result.title_index)==doc.description&&content.string(0x71)=="Occupied description","description collision");
    const auto emitted=CampaignAsset::parse(package.extract("level/my_branch.cgn"));
    check(read_u32(emitted.serialize(),0x50)==0xAABBCCDD,"native common header was not preserved");
    check(read_u32(emitted.serialize(),0x150)==result.title_index,"embedded catalog title index was not synchronized");
    check(fs::exists(root/"export/INSTALL.txt")&&!fs::exists(root/"game/level00.pck"),"export modified the installation");
    // Merge the native PATCH00 without discarding unrelated resources; PATCH01 is ignored.
    write_stored_pck(root/"game/PATCH00.PCK",std::vector<PckWriteEntry>{{"probe/preserve.dat",std::vector<std::byte>(4,std::byte{0x7B}),0}});
    const auto merged=doc.export_package(catalog,root/"merged-export",1);
    check(PckArchive::open(merged.text_patch).extract("probe/preserve.dat")==std::vector<std::byte>(4,std::byte{0x7B}),"text patch dropped unrelated entries");
    rejects([&]{doc.export_package(catalog,root/"game/nested/new-export",0);});
    check(!fs::exists(root/"game/nested"),"export wrote staging data inside the installation");
    rejects([&]{doc.export_package(catalog,root/"export",0);});rejects([&]{doc.set_identifier("../unsafe");});
    rejects([&]{doc.remove_mission(1);});rejects([&]{doc.remove_mission(2);});
    doc.set_mission_id(2,42);check(doc.campaign().record(0).successor(1)==42&&doc.source(42).level_id==42,"ID rename left incoming links/source stale");
    rejects([&]{doc.set_mission_id(42,0);}); // zero is already the campaign completion sentinel
    const auto before=read_file(root/"game/LEVEL.PCK");
    doc.campaign().record(0).set_level_path("missing/map");rejects([&]{doc.export_package(catalog,root/"bad-export",1);});
    check(!fs::exists(root/"bad-export")&&read_file(root/"game/LEVEL.PCK")==before,"failed export changed files");
    auto zone=campaign_exit_zone_from_render({2,3,4,5,6});
    check(zone.center_x_q12==3*4096&&zone.center_y_q12==2*4096&&zone.destination_x_q12==6*4096,"exit coordinates swapped incorrectly");
    check(campaign_exit_zone_render(zone)==std::array<float,5>{2,3,4,5,6},"exit coordinate conversion not reversible");
    rejects([&]{campaign_exit_zone_from_render({0,0,-1,0,0});});
    auto parsed_expression=campaign_expression_from_text("C0 C1 AND C2 NOT OR END");
    check(campaign_expression_text(parsed_expression)=="C0 C1 AND C2 NOT OR","friendly expression builder changed postfix order");
    rejects([&]{campaign_expression_from_text("C64");});rejects([&]{campaign_expression_from_text("C0 AND");});
    rejects([&]{campaign_expression_from_text("C0 END C1");});rejects([&]{campaign_expression_from_text("C0 C1");});
    auto l=LevelAsset::parse(lev_fixture());LevelScheduledCondition expression;expression.status_and_kind=0x1A;expression.payload_bytes={0xFF,0xFC};l.set_scheduled_condition(1,expression);
    check(!validate_campaign_mission_script(l).ok(),"expression stack underflow accepted");
    expression.payload_bytes={0,2,0xFE,0xFC};l.set_scheduled_condition(1,expression);
    check(validate_campaign_mission_script(l).ok()&&!validate_campaign_mission_script(l).warnings.empty(),"forward reference must warn without reordering");
    // Highest numbered level add-on takes precedence; PATCH01 is not mounted by supplied native bootstrap.
    auto override=imported.campaign().serialize();override[0x80]=std::byte{0x5A};
    write_stored_pck(root/"game/level99.pck",std::vector<PckWriteEntry>{{"level/testcamp.cgn",override,0}});
    auto wrong=override;wrong[0x80]=std::byte{0xA5};write_stored_pck(root/"game/PATCH01.PCK",std::vector<PckWriteEntry>{{"level/testcamp.cgn",wrong,0}});
    check(CampaignDocument::import_native(root/"game","level/testcamp.cgn").campaign().serialize()==override,"campaign mounted assets in editor alphabetical order");
    std::cout<<"V761_CAMPAIGN_DOCUMENT PASS project, branch/replay/finish, catalog/text allocation, mount gate, shared FLD, atomic staging, exact paths, native precedence, scripts and coordinates\n";
    if(argc>1) {
        const auto actual_catalog=AssetCatalog::scan_archive_root(argv[1]);auto tutorial=CampaignDocument::import_native(argv[1],"level/tutorial.cgn");
        auto custom=CampaignDocument::create(tutorial,"v761_probe");
        const auto group=std::find_if(actual_catalog.map_groups().begin(),actual_catalog.map_groups().end(),[](const auto& g){return g.level_path=="level/asgard.lev";});
        check(group!=actual_catalog.map_groups().end(),"Asgard fixture absent");
        auto map=MapDocument::create_from_template(actual_catalog,*group,"v761_first",67,59);map.level().set_faction_starting_resources_q4(1,0,0);
        LevelPlacement resource;resource.army_asset_id=811;const auto& cell=map.field().at(20,20);
        resource.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);map.level().add_placement(resource);
        map.save_project(root/"first.thmap",true);auto reloaded=MapDocument::open_project(actual_catalog,root/"first.thmap");
        reloaded.save_project(root/"resaved.thmap");
        check(MapDocument::open_project(actual_catalog,root/"resaved.thmap").level().faction_xenite_q4(1)==0,"reopening a mission project lost resource-preservation mode");
        check(reloaded.level().faction_xenite_q4(1)==0&&reloaded.level().faction_tritium_q4(1)==0,"campaign project repaired intentional zero resources");
        auto record=CampaignLevelRecord::minimal(1,"v761_first");record.set_successor(1,2);custom.campaign()=CampaignAsset::from_template(custom.campaign(),1,std::span(&record,1));custom.source(1).map_project=root/"first.thmap";
        map.set_map_name("v761_second");map.save_project(root/"second.thmap",true);custom.add_map_project(actual_catalog,root/"second.thmap");
        // add_map_project reserves all existing terminal IDs, so use the assigned ID explicitly.
        const auto next=custom.campaign().records()[1].level_id();custom.campaign().record(0).set_successor(1,next);
        custom.source(next).outcome_names[0]="Defeat";custom.save_project(root/"stock-project/campaign.thcampaign");
        auto restored=CampaignDocument::open_project(root/"stock-project/campaign.thcampaign");
        check(restored.source(1).map_project==root/"first.thmap","relative source path did not resolve after save/open");
        const auto validation=restored.validate(actual_catalog);for(const auto& error:validation.errors)std::cerr<<error<<'\n';check(validation.ok(),"authored stock-based campaign validation failed");
        const auto exported=restored.export_package(actual_catalog,root/"stock-export",0);auto actual=PckArchive::open(exported.level_package);
        check(FieldGrid::parse(actual.extract("level/v761_first.fld")).placement_occupancy_class_present(20,20,1),"campaign export omitted the resource support field mask");
        check(LevelAsset::parse(actual.extract("level/v761_first.lev")).faction_xenite_q4(1)==0,"package repaired authored zero starting resources");
        auto tutorial_validation=tutorial.validate(actual_catalog);for(const auto& error:tutorial_validation.errors)std::cerr<<error<<'\n';check(tutorial_validation.ok(),"stock tutorial validation failed");
        std::cout<<"V761_CAMPAIGN_STOCK PASS two authored map projects, reference roundtrip, native CGN/catalog/locales/package, zero resources, shared tutorial terrain\n";
    }
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
