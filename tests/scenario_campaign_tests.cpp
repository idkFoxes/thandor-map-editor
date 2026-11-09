#include "thandor/campaign.hpp"
#include "thandor/level.hpp"
#include "thandor/pck.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
template<class F>static void rejects(F f){try{f();}catch(const FormatError&){return;}throw std::runtime_error("malformed campaign accepted");}
static std::vector<std::byte> fixture(){
 std::vector<std::byte> b(0x200+3*0x180);write_u32(b,0,fourcc("cgn"));write_u32(b,4,static_cast<uint32_t>(b.size()));
 write_i32(b,0xB4,10);write_i32(b,0xB8,3);b[0x81]=std::byte{0xA7};
 for(int i=0;i<3;++i){auto r=CampaignLevelRecord::minimal(10*(i+1),"level/test/map"+std::to_string(i)+".lev");std::copy(r.bytes().begin(),r.bytes().end(),b.begin()+0x200+i*0x180);}
 return b;
}
int main(int argc,char** argv)try{
 auto input=fixture();auto c=CampaignAsset::parse(input);check(c.serialize()==input,"CGN no-op changed unknown bytes");
 c.record(0).set_successor(1,20);c.record(1).set_successor(2,30);c.record(2).set_successor(1,0);
 check(c.continuation(10,1).successor_id==20&&c.continuation(10,1).kind==CampaignContinuationKind::NextLevel,"branch did not use outcome");
 check(c.continuation(30,1).kind==CampaignContinuationKind::CampaignFinished,"unmatched terminal id did not finish");
 c.record(0).set_successor(0,-1);check(c.continuation(10,0).kind==CampaignContinuationKind::NoTransition,"negative successor incorrectly finished/released campaign");
 check(c.continuation(999,0).kind==CampaignContinuationKind::NoTransition,"unknown current id advanced");
 check(c.unreachable_level_ids().empty(),"branch reachability omitted an existing target");
 c.record(1).set_successor(2,0);check(c.unreachable_level_ids()==std::vector<int32_t>{30},"unreachable record not found");
 rejects([&]{c.record(0).set_successor(8,10);});rejects([&]{c.continuation(10,255);});
 c.record(0).set_carry_over(1,true);c.record(0).set_retain_previous_carry_over(0,true);
 check(c.record(0).carry_over(1)&&!c.record(0).carry_over(0)&&c.record(0).retain_previous_carry_over(0),"outcome masks interpreted as faction masks");
 CampaignLevelRecord::ExitZone z{4096,-8192,12288,40960,81920};c.record(0).set_exit_zone(2,z);
 check(c.record(0).exit_zone(2).destination_y_q12==81920,"exit zone coordinates lost");
 rejects([&]{c.record(0).set_exit_zone(8,z);});
 const auto before=c.record(0).level_path();rejects([&]{c.record(0).set_level_path(std::string(100,'x'));});
 check(c.record(0).level_path()==before,"rejected path corrupted campaign record");
 rejects([&]{c.record(0).set_level_path("../bad");});rejects([&]{c.record(0).set_level_path("/bad");});
 auto zero=c;zero.record(2).set_level_id(0);zero.record(0).set_successor(3,0);
 check(zero.continuation(10,3).kind==CampaignContinuationKind::NextLevel,"record zero incorrectly treated as an end opcode");
 auto r=CampaignLevelRecord::minimal(42,"level/nimm2/gebirgsjäger.lev");
 check(r.level_path()=="nimm2\\gebirgsjäger","UTF-16 relative map path not preserved");
 const auto created=CampaignAsset::from_template(c,42,std::span(&r,1));
 auto exported=created.serialize();check(exported.size()==0x380&&read_u32(exported,4)==0x380,"new record count/size incorrect");
 check(CampaignAsset::parse(exported).first_level_id()==42&&exported[0x81]==std::byte{0xA7},"template common header changed");
 auto malformed=input;write_i32(malformed,0xB8,-1);rejects([&]{CampaignAsset::parse(malformed);});
 malformed=input;write_i32(malformed,0xB4,999);rejects([&]{CampaignAsset::parse(malformed);});
 malformed=input;write_u32(malformed,4,0xFFFFFFF0);rejects([&]{CampaignAsset::parse(malformed);});
 malformed=input;malformed.resize(600);rejects([&]{CampaignAsset::parse(malformed);});
 malformed=input;malformed[0]=std::byte{0};rejects([&]{CampaignAsset::parse(malformed);});
 // Preserve a trailing vendor payload and a declared size smaller than the container on no-op writes.
 auto extended=input;extended.push_back(std::byte{0x55});check(CampaignAsset::parse(extended).serialize()==extended,"CGN trailing payload changed");
 unsigned stock=0,levels=0;
 if(argc>1){
  auto p=PckArchive::open(std::filesystem::path(argv[1])/"LEVEL.PCK");
  for(const auto& e:p.entries()){
   if(e.normalized_name.ends_with(".cgn")){
    const auto original=p.extract(e);const auto parsed=CampaignAsset::parse(original);
    check(parsed.serialize()==original,"stock CGN not byte-identical");check(parsed.authoring_errors().empty(),"stock campaign rejected structurally");++stock;
   }
   if(e.normalized_name.ends_with(".lev")){
    auto l=LevelAsset::parse(p.extract(e));
    auto invalid=l.end_condition_trigger(0);invalid.faction_runtime_index=8;rejects([&]{l.set_end_condition_trigger(0,invalid);});
    invalid.faction_runtime_index=1;invalid.end_movie_selection_index=8;rejects([&]{l.set_end_condition_trigger(0,invalid);});
    for(uint32_t i=0;i<16;++i){const auto t=l.end_condition_trigger(i);if(t.active())check(t.faction_runtime_index<8&&t.end_movie_selection_index<8,"stock trigger exceeds table bounds");}
    ++levels;
   }
  }
  check(stock==13&&levels==55,"unexpected supplied campaign census");
 }
 std::cout<<"Campaign read/write, branching, negative/terminal ids, reachability, carry-over, UTF16 and bounds PASS; stock CGNs="<<stock<<" LEVs="<<levels<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
