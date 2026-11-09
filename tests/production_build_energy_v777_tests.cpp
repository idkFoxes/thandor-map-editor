#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/pck.hpp"
#include "thandor/simulation_lab.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>
using namespace thandor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static std::vector<std::byte> models(){
 std::vector<std::byte>d(0x200+4*0x300);write_u32(d,0,fourcc("mdl"));write_u32(d,4,uint32_t(d.size()));write_u32(d,0xB0,4);
 const uint32_t ids[]{100,101,200,201},build[]{3,900,5,800},operating[]{96,97,128,129};
 for(uint32_t i=0;i<4;++i){auto o=0x200+i*0x300;write_u32(d,o,0x300);write_u32(d,o+8,ids[i]);write_u32(d,o+0x4C,i<2?1:7);write_u32(d,o+0x60,1000);write_u32(d,o+0x64,o+0x280);write_u32(d,o+0x188,build[i]);write_u32(d,o+0x18C,operating[i]);
  auto n=o+0x280;write_u32(d,n,0x80);write_u32(d,n+0xC,0x4000);const char* path="spr/test.spr";for(unsigned j=0;path[j];++j)write_u16(d,n+0x38+2*j,path[j]);}
 return d;
}
static std::vector<std::byte> army(){
 std::vector<std::byte>d(0x2C0);write_u32(d,0,fourcc("arm"));write_u32(d,4,uint32_t(d.size()));write_u32(d,0xB0,1);write_u32(d,0x200,0xC0);write_u32(d,0x208,42);write_u32(d,0x20C,0x240);
 write_u32(d,0x240,0x40);write_u32(d,0x248,1);write_u32(d,0x24C,0x280);write_u32(d,0x260,100);write_u32(d,0x264,101);
 write_u32(d,0x280,0x40);write_u32(d,0x2A0,200);write_u32(d,0x2A4,201);return d;
}
int main(int argc,char**argv)try{
 const auto bytes=models();const auto parsed=parse_model_visual_records(bytes,"mdl/unit.mdl");check(parsed.size()==4,"fixture model count");
 check(parsed[0].simulation.retail_build_energy_q4==3&&parsed[0].simulation.retail_energy_demand_q4==96,"build and operating MDL domains conflated");
 auto root=std::filesystem::temp_directory_path()/("thandor-build-energy-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(root);
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove_all(p,ec);}}cleanup{root};
 const std::vector<PckWriteEntry> entries{{"mdl/unit.mdl",bytes,0},{"arm/unit.arm",army(),0}};write_stored_pck(root/"DATEN.PCK",entries);
 auto catalog=AssetCatalog::scan_archive_root(root);auto* p=catalog.army_preset(42);check(p!=nullptr,"fixture army missing");
 check(p->retail_build_energy_q4==8,"build load must sum base variants including child, independent of attached operating-energy flag");
 check(p->retail_energy_demand_q4==224,"operating metric changed while adding build energy");
 // Tick-zero capacity is registered without running allocation, queue payment or fuel burn.
 SimulationLabConfig cfg;cfg.run_count=1;cfg.duration_ticks=8;SimulationLabSeedEntity generator;generator.faction_index=1;generator.root_model_maximum_integrity=100;
 SimulationLabModelComponentSeed component;component.maximum_integrity=100;component.energy_generation_capacity_q4=200;generator.model_components.push_back(component);
 std::array<SimulationLabFactionEconomySeed,8> economy{};economy[1].derive_energy_allocation_from_components=true;economy[1].baseline_energy_generation_capacity_q4=640;economy[1].tritium_current_q4=123;
 std::array<uint32_t,AiKnowledgeData::kParameterCount> ki{};SimulationLabExperiment lab;lab.reset(cfg,ki,std::span(&generator,1),economy);const auto&run=lab.runs()[0];
 check(run.tick==0&&run.faction_energy_generation_capacity_q4[1]==840,"initial generation capacity not registered");check(run.faction_economy[1].tritium_current_q4==123&&run.faction_supplied_energy_demand_q4[1]==0,"initial capacity refresh advanced economy");
 // A lethally damaged generator retains registered capacity through its
 // destruction animation; only model release removes the contribution.
 auto& dead=lab.runs()[0].entities[0].model_components[0];
 dead.integrity=0;dead.terminal_runtime_flag8=true;
 lab.step(FieldGrid::create(4,4,0,0),8);
 check(run.faction_energy_generation_capacity_q4[1]==840,"terminal generator lost capacity before model release");
 // An absorbed faction keeps its historical capacity and stock while its
 // registered components and baseline also contribute to the survivor.
 generator.faction_index=2;
 component.has_resource_storage_capacity_contribution=true;
 component.resource_storage_selector=1;
 component.resource_storage_capacity_contribution_q4=3000;
 generator.model_components[0]=component;
 economy[1].derive_storage_limits_from_components=true;
 economy[1].baseline_tritium_storage_limit_q4=4000;
 economy[2]=economy[1];
 economy[2].tritium_current_q4=6000;
 lab.reset(cfg,ki,std::span(&generator,1),economy);
 auto& merged=lab.runs()[0];
 merged.faction_frontend_player_counts[1]=1;
 simulation_lab_apply_relation_transition(merged,1,2,11,11);
 check(merged.faction_merge_redirect[2]==1 && merged.entities[0].faction_index==1,
       "absorption fixture did not transfer the generator");
 lab.step(FieldGrid::create(4,4,0,0),8);
 check(merged.faction_energy_generation_capacity_q4[2]==840 &&
       merged.faction_economy[2].tritium_storage_limit_q4==7000 &&
       merged.faction_economy[2].tritium_current_q4==6000,
       "capacity refresh overwrote the absorbed faction record");
 check(merged.faction_energy_generation_capacity_q4[1]==1480 &&
       merged.faction_economy[1].tritium_storage_limit_q4==11000,
       "survivor failed to retain the transferred capacity");
 if(argc>1){auto stock=AssetCatalog::scan_archive_root(argv[1]);const auto* arm=stock.army_preset(330);check(arm&&arm->retail_build_energy_q4==50&&arm->retail_energy_demand_q4==96,"stock ARM 330 native construction/operating split differs");
  if(argc>2){std::ifstream input(argv[2]);check(bool(input),"native census log missing");std::string line;uint32_t checked=0;
   while(std::getline(input,line)){if(!line.starts_with("BUILD_ENERGY,"))continue;auto split=line.find(',',13);auto id=uint32_t(std::stoul(line.substr(13,split-13)));auto expected=uint32_t(std::stoul(line.substr(split+1)));const auto* p=stock.army_preset(id);
    if(!p||p->retail_build_energy_q4!=expected)throw std::runtime_error("native construction-energy mismatch ARM "+std::to_string(id));++checked;}
   check(checked>100,"native census unexpectedly short");std::cout<<"NATIVE_BUILD_ENERGY_CENSUS PASS armies="<<checked<<'\n';}
 }
 std::cout<<"V777_BUILD_ENERGY PASS variant-0 tree, independent MDL domains, initial capacity, no tick-zero fuel burn\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
