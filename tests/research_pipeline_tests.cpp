#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/pck.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace thandor;
namespace {
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
SimulationLabSeedEntity source(uint32_t arm,uint32_t cls,std::vector<uint32_t> panel={}){
 SimulationLabSeedEntity s;s.army_asset_id=arm;s.faction_index=1;s.building=true;
 s.runtime_model_class_id=cls;s.root_model_maximum_integrity=1000;s.actor.position={10,10,0};s.research_technology_ids=std::move(panel);return s;
}
SimulationLabTechnologyAsset tech(uint32_t id,int32_t score=256){SimulationLabTechnologyAsset t;
 t.technology_id=id;t.dependency_technology_id=id;t.ai_base_candidate_score=score;t.research_duration_ticks=3;return t;}
SimulationLabExperiment fixture(SimulationLabTechnologyAsset t,uint32_t cls=13,uint32_t funds=60000,uint32_t supply=1000){
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=256;cfg.parameter_index=100;cfg.sweep_mode=SimulationLabSweepMode::Fixed;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};ki[0x0D]=4096;
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].xenite_current_q4=funds;economy[1].baseline_xenite_storage_limit_q4=funds;economy[1].baseline_energy_supply_q4=supply;
 std::array<FactionTechnologyMask256,8> masks{};set_faction_technology_unlocked(masks[1],0,true);
 const std::array seeds{source(330,11),source(301,cls,{t.technology_id})};
 SimulationLabExperiment lab;lab.reset(cfg,ki,seeds,economy,{},std::span(&t,1),masks,{},{});return lab;
}
const auto& candidate(const SimulationLabExperiment& lab){const auto& trace=lab.runs()[0].ai_research_trace[1];check(trace.candidate_count==1,"target not admitted exactly once");return trace.candidates[0];}
void model_table_test(){
 const auto dir=std::filesystem::temp_directory_path()/"thandor_v636_research_registry";
 std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::filesystem::remove_all(p);}} cleanup{dir};
 std::ofstream(dir/"thandor.exe",std::ios::binary).put('\0');
 auto mdl=[](std::initializer_list<uint32_t> ids){std::vector<std::byte> b(0x200+ids.size()*0x300);
  write_u32(b,0,fourcc("mdl"));write_u32(b,4,b.size());write_u32(b,0xB0,ids.size());size_t off=0x200;
  for(auto id:ids){write_u32(b,off,0x300);write_u32(b,off+8,id);write_u32(b,off+0x4C,2);write_u32(b,off+0x1C0,18);size_t sp=off+0x280;for(char c:std::string("spr/test.spr")){b[sp]=std::byte(c);sp+=2;}off+=0x300;}return b;};
 std::vector<std::byte> tec(0x200+256*0x40);write_u32(tec,0,fourcc("tec"));write_u32(tec,4,tec.size());
 write_u32(tec,8,0x10000); // fixed TEC parser version
 std::vector<PckWriteEntry> entries{{"mdl/z.mdl",mdl({900,100}),fourcc("mdl")},{"mdl/a.mdl",mdl({1}),fourcc("mdl")},{"engine/tech.tec",tec,fourcc("tec")}};
 write_stored_pck(dir/"MODELLE.PCK",entries);const auto catalog=AssetCatalog::scan_game_root(dir);
 std::vector<std::byte> b(0x500);write_u32(b,0,fourcc("lev"));write_u32(b,4,b.size());write_u32(b,0xDC,b.size());
 for(size_t i=0;i<10;++i)write_u32(b,0xB0+i*4,0x480+i*4);
 write_u32(b,0xE8,2);write_u32(b,0xEC,0x3C0);
 const auto path=[&](size_t off,std::string_view s){for(char c:s){b[off]=std::byte(c);off+=2;}};
 path(0x3C0,"mdl/z.mdl");path(0x400,"mdl/a.mdl");
 auto level=LevelAsset::parse(b);check(level.resource_paths()[0]=="mdl/a.mdl","legacy sorted paths changed");
 check(level.model_resource_paths_in_load_order()[0]=="mdl/z.mdl","LEV serialized MDL order lost");
 const auto prepared=simulation_lab_prepare_technologies(catalog,&level);
 check(prepared.size()==256,"fixed TEC table unavailable");
 check(prepared[18].compatible_model_lookup_complete&&prepared[18].compatible_model_resolved&&prepared[18].compatible_model_definition_id==900,
  "lookup sorted records/files by ID or omitted a non-ARM definition");
 check(prepared[19].compatible_model_lookup_complete&&!prepared[19].compatible_model_resolved,"complete registry miss became approximate fallback");
 write_u32(b,0xE0,1);write_u32(b,0xE4,0x440);path(0x440,"arm/building08");
 auto army_level=LevelAsset::parse(b);
 check(army_level.army_resource_paths_in_load_order().size()==1&&army_level.army_resource_paths_in_load_order()[0]=="arm/building08","LEV army registry path missing");
 check(LevelAsset::parse(army_level.serialize()).army_resource_paths_in_load_order()[0]=="arm/building08","LEV save changed army registry");
 auto roundtrip=LevelAsset::parse(level.serialize());check(roundtrip.model_resource_paths_in_load_order().size()==2&&roundtrip.model_resource_paths_in_load_order()[0]=="mdl/z.mdl","LEV save reordered model load list");
}
}
int main(int argc,char** argv)try{
 const auto field=FieldGrid::create(59,59,0,0);
 model_table_test();
 if(argc>1){
  const auto global=AssetCatalog::scan_archive_root(argv[1]);
  const auto campaign=LevelAsset::parse(global.load("level/hansolo/s08_instand.lev"));
  const auto scoped=AssetCatalog::scan_archive_root(argv[1],campaign.model_resource_paths_in_load_order(),campaign.army_resource_paths_in_load_order());
  check(global.army_preset(304)&&scoped.army_preset(304),"stock factory registry absent");
  check((global.army_preset(304)->army_runtime_flags14_raw&1U)&&!(scoped.army_preset(304)->army_runtime_flags14_raw&1U),"campaign disabled factory inherited general enabled flag");
  check(scoped.army_preset(303)->browser_kind==PlacementBrowserKind::Building,"campaign factory was omitted from simulation seeds");
  check(scoped.army_preset(304)->source_path=="arm/building08.arm","campaign did not use its ARM registry");
 }

 for(uint32_t cls:{1U,2U,3U,17U,19U,21U}){auto t=tech(18);t.compatible_model_resolved=true;t.compatible_model_definition_id=100;t.compatible_model_runtime_class_id=cls;
  auto lab=fixture(t);lab.step(field,74);check(candidate(lab).score==(cls==1?20000U:40000U),"native class relation score differs");check(candidate(lab).score_kind==3,"factory kind precedence changed");}
 for(uint32_t cls:{4U,7U,9U,11U}){auto t=tech(18);t.compatible_model_resolved=true;t.compatible_model_runtime_class_id=cls;
  auto lab=fixture(t);lab.step(field,74);check(candidate(lab).reason==5&&candidate(lab).score==0,"native address branch silently reported evaluated or invented a score");}
 for(bool complete:{false,true}){auto t=tech(18);t.compatible_model_lookup_complete=complete;auto lab=fixture(t);lab.step(field,74);
  check(candidate(lab).score==(complete?0U:256U)&&candidate(lab).reason==(complete?4U:6U),"authoritative missing-model path used base fallback");}
 for(auto [score,chosen]:{std::pair<int32_t,bool>{-1,false},{0,false},{256,true}}){auto lab=fixture(tech(18,score),11);lab.step(field,74);
  check(lab.runs()[0].ai_research_trace[1].best_technology_id==(chosen?18U:0U),"signed base selection differs");}
 // Pump ratio reaches a dword-multiply overflow with positive signed pressure:
 // 0x00FFFFFF * 0x200 wraps to 0xFFFFFE00, >>8 = 0x00FFFFFE.
 {auto t=tech(213,512);auto lab=fixture(t,11,60000,256);auto& run=lab.runs()[0];
  lab.step(field,73);run.faction_supplied_energy_demand_q4[1]=0x00FFFFFF;lab.step(field,1);
  check(candidate(lab).score==0x00FFFFFE,"pump multiplied at 64 bits instead of wrapping before shift");}
 for(auto [demand,expected_reason]:{std::pair<uint32_t,uint32_t>{239,10},{240,0},{0x80000000U,10}}){
  auto lab=fixture(tech(213),11,60000,256);lab.step(field,73);lab.runs()[0].faction_supplied_energy_demand_q4[1]=demand;lab.step(field,1);
  check(candidate(lab).reason==expected_reason&&candidate(lab).score==(demand==240?240U:0U),"pump threshold or signed pressure changed");
 }
 for(uint32_t supply:{0U,1U}){auto lab=fixture(tech(213),11,60000,supply);lab.step(field,73);
  lab.runs()[0].faction_supplied_energy_demand_q4[1]=UINT32_MAX;lab.step(field,1);
  check(candidate(lab).reason==(supply?13U:9U)&&candidate(lab).score==0,"unsafe DIV input escaped containment");}
 for(uint32_t arm:{302U,305U}){auto t=tech(18);t.compatible_model_resolved=true;t.compatible_model_runtime_class_id=1;
  auto lab=fixture(t);auto extra=lab.runs()[0].entities[1];extra.army_asset_id=arm;extra.research_technology_ids.clear();lab.runs()[0].entities.push_back(extra);lab.step(field,74);
  check(candidate(lab).score==(arm==302?0U:15000U),"class1 native factory exception/reduction changed");}
 for(uint32_t id:{216U,217U}){auto lab=fixture(tech(id),11);lab.step(field,74);check(candidate(lab).score_kind==0&&candidate(lab).score==0,"wall was assigned nonzero research score");}
 // An assigned research waits for payment, charges Xenite exactly once, pauses
 // without power, resumes, and unlocks a recursive dependency without extra payment.
 {auto t=tech(18);t.dependency_technology_id=6;t.xenite_cost_q4=48;t.energy_demand_q4=32;t.research_duration_ticks=4;
  auto lab=fixture(t,11,100);lab.step(field,74);auto& run=lab.runs()[0];auto& owner=run.entities[1];
  check(owner.research_pending_payment&&!owner.research_active&&!faction_technology_unlocked(run.faction_technology_masks[1],18),"assignment completed early");
  run.faction_economy[1].xenite_current_q4=47;lab.step(field,1);check(owner.research_pending_payment,"unaffordable pending research started");
  run.faction_economy[1].xenite_current_q4=100;lab.step(field,1);check(owner.research_active&&run.faction_economy[1].xenite_current_q4==52,"research charged wrong Xenite");
  owner.runtime_flags|=1;lab.step(field,8);check(owner.research_elapsed_ticks==0,"unpowered research advanced");
  owner.runtime_flags&=~1U;lab.step(field,4);check(!owner.research_active&&run.metrics.research_jobs_completed==1&&run.faction_economy[1].xenite_current_q4==52,"research completion/one-time payment failed");
  check(faction_technology_unlocked(run.faction_technology_masks[1],18)&&faction_technology_unlocked(run.faction_technology_masks[1],6)&&run.faction_active_research_energy_q4[1]==0,"completion failed unlock/energy release");
 }
 std::cout<<"V636_RESEARCH_PIPELINE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
