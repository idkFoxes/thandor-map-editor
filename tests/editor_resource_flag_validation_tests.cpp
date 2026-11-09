#include "editor_resource_validation.hpp"
#include "thandor/map_document.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace thandor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
 auto field=FieldGrid::create(59,83,0,0);
 const auto make=[&](uint32_t id,uint32_t x,uint32_t y){LevelPlacement p;p.army_asset_id=id;const auto& cell=field.at(x,y);p.set_render_position_q12(cell.world_x_q12,cell.world_y_q12);return p;};
 std::vector<LevelPlacement> placements{make(814,20,30),make(811,28,40),make(330,14,18),make(332,16,20),make(137,15,15)};
 auto result=editor::validate_resource_deposit_flags(field,placements);
 check(result.checked==2&&result.issues.size()==2,"must check only deposit objects, not extractors or units");
 check(result.issues[0].cell_x==20&&result.issues[0].cell_y==30,"LEV axes transposed in resource validator");
 check(result.issues[0].audit.required_support_mask==0x800&&result.issues[1].audit.required_support_mask==0x1000,"wrong resource masks");
 field.at(20,30).flags_and_material|=0x80000800U;field.at(28,40).flags_and_material|=0x80001000U;
 check(editor::validate_resource_deposit_flags(field,placements).issues.empty(),"hard-blocked but correctly flagged resource reported");
 field.at(20,30).flags_and_material^=0x1800U;
 result=editor::validate_resource_deposit_flags(field,placements);
 check(result.issues.size()==1&&result.issues.front().placement_index==0,"wrong resource bit must not satisfy matching resource");
 placements.push_back(make(815,20,30));
 check(editor::validate_resource_deposit_flags(field,placements).issues.size()==2,"resource alias skipped");
 LevelPlacement outside;outside.army_asset_id=814;outside.set_render_position_q12(0x1000000,0x1000000);placements.push_back(outside);
 const auto before=field.serialize();
 check(editor::validate_resource_deposit_flags(field,placements).checked==3,"outside record treated as missing terrain flag");
 check(field.serialize()==before,"validator mutated the field");
 std::cout<<"PASS resource object scope, matching bits, axis order, blocked terrain, aliases, outside scope, read-only\n";
 if(argc>1){const auto c=AssetCatalog::scan_archive_root(argv[1]);for(const auto& g:c.map_groups()){
  auto d=MapDocument::open(c,g);const auto audit=editor::validate_resource_deposit_flags(d.field(),d.level().placements());
  std::cout<<"MAP "<<g.level_path<<" deposits="<<audit.checked<<" missing_flags="<<audit.issues.size()<<'\n';
  if(g.level_path=="level/asgard.lev")check(audit.checked==21&&audit.issues.empty(),"Asgard stock deposit validation regression");
 }}
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
