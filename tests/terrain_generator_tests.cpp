#include "thandor/terrain_generator.hpp"
#include "thandor/pck.hpp"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid generator operation accepted");}
bool equal(const FieldCell& a,const FieldCell& b){return a.world_x_q12==b.world_x_q12&&a.world_y_q12==b.world_y_q12&&a.terrain_height_q12==b.terrain_height_q12&&a.water_delta_q12==b.water_delta_q12&&a.flags_and_material==b.flags_and_material;}
int main(int argc,char** argv)try{
 auto f=FieldGrid::create(59,59,0,3);const auto before=f.serialize();
 const TerrainRegionPoint polygon[]{{10,10},{48,10},{48,48},{10,48}};
 auto mask=terrain_polygon_mask(59,59,polygon);check(mask[10*59+10]&&mask[30*59+30]&&!mask[0],"lasso rasterizer did not include boundary/interior correctly");
 const TerrainRegionPoint concave[]{{5,5},{30,5},{30,10},{10,10},{10,30},{5,30}};
 auto c=terrain_polygon_mask(59,59,concave);check(c[20*59+7]&&!c[20*59+20],"concave polygon incorrectly filled its cutout");
 auto combined=mask;combine_terrain_region(combined,c,TerrainRegionCombine::Add);check(combined[20*59+7],"add selection failed");
 combine_terrain_region(combined,c,TerrainRegionCombine::Subtract);check(!combined[20*59+7]&&combined[30*59+30],"subtract selection failed");
 TerrainGeneratorSettings s;s.blend_cells=5;s.height=3;s.keep_water=false;
 for(unsigned preset=0;preset<7;++preset){s.preset=static_cast<TerrainGeneratorPreset>(preset);s.water_level=s.preset==TerrainGeneratorPreset::Lake?-0.8F:0;
  auto a=generate_terrain_region(f,mask,s);auto b=generate_terrain_region(f,mask,s);check(a.field.serialize()==b.field.serialize(),"same seed/settings changed preview");
  check(a.changed_cells>0,"preset generated no terrain");
  s.seed=next_terrain_seed(s.seed);auto different=generate_terrain_region(f,mask,s);check(a.field.serialize()!=different.field.serialize(),"reroll did not change terrain");
  for(std::size_t i=0;i<mask.size();++i){if(!mask[i])check(equal(f.cells()[i],a.field.cells()[i]),"inside generation changed outside cells");
   check(f.cells()[i].flags_and_material==a.field.cells()[i].flags_and_material,"generator changed unrelated native flags/material");}
 }
 check(f.serialize()==before,"preview mutated the original field");
 s.preset=TerrainGeneratorPreset::Mountains;s.outside=true;auto outside=generate_terrain_region(f,mask,s);
 for(std::size_t i=0;i<mask.size();++i)if(mask[i])check(equal(f.cells()[i],outside.field.cells()[i]),"outside generation changed inside cells");
 s.outside=false;auto blend=terrain_region_weights(f,mask,s);check(blend[10*59+10]==0&&blend[11*59+11]<blend[30*59+30]&&blend[0]==0,"boundary feathering leaks or does not taper");
 std::vector<uint8_t> protect(mask.size());protect[30*59+30]=1;
 auto protected_result=generate_terrain_region(f,mask,s,protect);check(equal(f.at(30,30),protected_result.field.at(30,30)),"protected footprint modified");
 auto wet=f;wet.at(30,30).water_delta_q12=200;s.keep_water=true;auto preserve=generate_terrain_region(wet,mask,s);
 check(equal(wet.at(30,30),preserve.field.at(30,30)),"existing water changed despite protection");
 s.preset=TerrainGeneratorPreset::Depression;auto dry=generate_terrain_region(f,mask,s);check(dry.field.at(30,30).water_delta_q12<0,"depression invented water from a hidden dry-cell surface");
 s.preset=TerrainGeneratorPreset::Lake;s.water_level=-0.8F;auto lake=generate_terrain_region(f,mask,s);
 check(lake.field.at(30,30).water_delta_q12>0,"lake did not fill its bed");
 const int32_t surface=float_to_q12(s.water_level);
 for(const auto& cell:lake.field.cells())if(cell.water_delta_q12>0)check(cell.terrain_height_q12+cell.water_delta_q12==surface,"lake surface is not flat");
 s.preset=TerrainGeneratorPreset::River;s.water_level=0;s.drivable_river=true;s.river_angle_degrees=0;s.ford_count=0;
 auto river=generate_terrain_region(f,mask,s);check(river.river_cells>0&&river.blocked_river_count==0,"shallow flat river failed native ground traversal checks");
 std::size_t wet_count=0;for(const auto& cell:river.field.cells())if(cell.water_delta_q12>0){++wet_count;check(cell.water_delta_q12<500,"drivable river exceeds native shallow-water threshold");}
 check(wet_count>0&&river.river_water_cells>0,"river generated no water");
 auto loaded_river=FieldGrid::parse(river.field.serialize());
 for(uint32_t y=0;y<loaded_river.height();++y)for(uint32_t x=0;x<loaded_river.width();++x)if(loaded_river.at(x,y).water_delta_q12>0)
  for(uint32_t band=1;band<7;++band)check(!loaded_river.terrain_forbidden_for_traversal_band(x,y,band),"drivable water failed after FLD serialization");
 s.ford_count=2;auto ford=generate_terrain_region(f,mask,s);check(ford.blocked_river_count==0,"dry crossings broke shallow river traversal");check(ford.field.serialize()!=river.field.serialize(),"dry crossings did not alter the channel");
 auto blocked=f;blocked.at(30,30).flags_and_material|=0x80000000U;s.river_bend=0;
 check(generate_terrain_region(blocked,mask,s).blocked_river_count>0,"hard-blocked river terrain was advertised as drivable");
 s.preset=TerrainGeneratorPreset::Hills;s.paint_material=true;s.material=9;auto painted=generate_terrain_region(f,mask,s);check(painted.field.at(30,30).material()==9&&painted.field.at(0,0).material()==3,"material generation escaped the mask");
 auto parsed=FieldGrid::parse(painted.field.serialize());check(parsed.has_game_compatible_dimensions()&&parsed.converter_version()==FieldGrid::kGameConverterVersion,"generated field cannot roundtrip as game FLD");
 // Patterns must produce distinct deterministic terrain while respecting all edit boundaries.
 TerrainGeneratorSettings pattern_settings;pattern_settings.height=6;pattern_settings.keep_water=false;
 const auto classic=generate_terrain_region(f,mask,pattern_settings).field.serialize();
 std::vector<std::vector<std::byte>> pattern_outputs;
 for(const auto pattern:{TerrainNoisePattern::Classic,TerrainNoisePattern::DomainWarped,TerrainNoisePattern::Cellular}){
  pattern_settings.pattern=pattern;
  for(const auto preset:{TerrainGeneratorPreset::Hills,TerrainGeneratorPreset::Mountains,TerrainGeneratorPreset::Depression,TerrainGeneratorPreset::Lake,TerrainGeneratorPreset::Plateau,TerrainGeneratorPreset::Plains}){
   pattern_settings.preset=preset;
   const auto generated=generate_terrain_region(f,mask,pattern_settings,protect);
   check(generated.field.serialize()==generate_terrain_region(f,mask,pattern_settings,protect).field.serialize(),"new pattern is not deterministic");
   auto rerolled=pattern_settings;rerolled.seed=next_terrain_seed(rerolled.seed);
   check(generated.field.serialize()!=generate_terrain_region(f,mask,rerolled,protect).field.serialize(),"new pattern ignores reroll");
   for(std::size_t i=0;i<mask.size();++i){if(!mask[i]||protect[i]||generated.weights[i]==0)check(equal(f.cells()[i],generated.field.cells()[i]),"new pattern escaped selection or protection");
    check(f.cells()[i].flags_and_material==generated.field.cells()[i].flags_and_material,"new pattern changed unrelated native cell flags");}
   pattern_settings.outside=true;const auto inverted=generate_terrain_region(f,mask,pattern_settings,protect);pattern_settings.outside=false;
   for(std::size_t i=0;i<mask.size();++i)if(mask[i]||protect[i])check(equal(f.cells()[i],inverted.field.cells()[i]),"new pattern escaped outside selection");
  }
  pattern_settings.preset=TerrainGeneratorPreset::Hills;pattern_outputs.push_back(generate_terrain_region(f,mask,pattern_settings).field.serialize());
 }
 check(pattern_outputs[0]==classic&&pattern_outputs[0]!=pattern_outputs[1]&&pattern_outputs[0]!=pattern_outputs[2]&&pattern_outputs[1]!=pattern_outputs[2],"terrain patterns do not produce distinct shapes");
 pattern_settings.pattern=TerrainNoisePattern::DomainWarped;pattern_settings.warp_strength=0;
 check(generate_terrain_region(f,mask,pattern_settings).field.serialize()==classic,"zero domain warp should preserve classic terrain");
 pattern_settings.warp_strength=0.75F;
 pattern_settings.terrace_steps=6;pattern_settings.blend_cells=0;
 const auto terraced=generate_terrain_region(f,mask,pattern_settings);
 std::set<int32_t> levels;
 for(std::size_t i=0;i<mask.size();++i)if(mask[i])levels.insert(terraced.field.cells()[i].terrain_height_q12);
 check(levels.size()>2&&levels.size()<=13,"terracing did not form discrete height steps");
 check(terraced.field.serialize()!=pattern_outputs[1],"terracing did not change terrain");
 pattern_settings.blend_cells=5;const auto terrace_blend=generate_terrain_region(f,mask,pattern_settings,protect);
 check(equal(f.at(10,10),terrace_blend.field.at(10,10))&&equal(f.at(30,30),terrace_blend.field.at(30,30)),"terraces damaged blended boundary or protected cell");
 const auto terrace_roundtrip=FieldGrid::parse(terrace_blend.field.serialize());check(terrace_roundtrip.serialize()==terrace_blend.field.serialize(),"terraces did not roundtrip losslessly");
 // A drawn centerline follows the user's route, ends at its endpoints, and stays in the region.
 TerrainGeneratorSettings path_settings;path_settings.preset=TerrainGeneratorPreset::River;path_settings.keep_water=false;
 path_settings.use_river_path=true;path_settings.river_bend=0;path_settings.river_width_cells=4;
 path_settings.river_path={{18,18},{30,18},{40,30},{40,40}};
 auto drawn=generate_terrain_region(f,mask,path_settings);
 check(drawn.river_water_cells>0&&drawn.blocked_river_count==0,"drawn shallow river failed native terrain admission");
 check(drawn.field.at(20,18).water_delta_q12>0&&drawn.field.at(40,38).water_delta_q12>0,"drawn river did not follow both legs");
 check(equal(f.at(15,40),drawn.field.at(15,40)),"drawn river changed ground away from its path");
 // The one-vertex native water-normal collar can adjust hidden dry water planes here.
 for(const auto end:std::array<TerrainRegionPoint,2>{{{11,18},{40,47}}}){
  const auto& cell=drawn.field.at(static_cast<uint32_t>(end.x),static_cast<uint32_t>(end.y));
  check(cell.terrain_height_q12==0&&cell.water_delta_q12<0,"drawn river carved or filled beyond its endpoint caps");
 }
 check(drawn.field.serialize()==generate_terrain_region(f,mask,path_settings).field.serialize(),"drawn path is not deterministic");
 auto moved_path=path_settings;moved_path.river_path={{18,40},{30,40},{40,30},{40,18}};
 check(drawn.field.serialize()!=generate_terrain_region(f,mask,moved_path).field.serialize(),"editing centerline did not change river");
 for(std::size_t i=0;i<mask.size();++i)if(!mask[i]||drawn.weights[i]==0)check(equal(f.cells()[i],drawn.field.cells()[i]),"drawn river escaped selection");
 path_settings.river_bend=0.6F;const auto bent=generate_terrain_region(f,mask,path_settings);
 path_settings.seed=next_terrain_seed(path_settings.seed);const auto bent_reroll=generate_terrain_region(f,mask,path_settings);
 check(bent.field.serialize()!=bent_reroll.field.serialize(),"drawn river bends ignored reroll");
 path_settings.ford_count=1;const auto drawn_ford=generate_terrain_region(f,mask,path_settings);
 check(drawn_ford.field.serialize()!=bent_reroll.field.serialize()&&drawn_ford.blocked_river_count==0,"drawn river ford failed");
 auto drawn_loaded=FieldGrid::parse(drawn_ford.field.serialize());
 for(uint32_t y=0;y<drawn_loaded.height();++y)for(uint32_t x=0;x<drawn_loaded.width();++x)if(drawn_loaded.at(x,y).water_delta_q12>0)
  for(uint32_t band=1;band<7;++band)check(!drawn_loaded.terrain_forbidden_for_traversal_band(x,y,band),"drawn river failed native admission after serialization");
 const auto path_protected=generate_terrain_region(f,mask,path_settings,protect);check(equal(f.at(30,30),path_protected.field.at(30,30)),"drawn river modified protected cell");
 path_settings.use_river_path=false;const auto directional=generate_terrain_region(f,mask,path_settings);path_settings.river_path.clear();
 check(directional.field.serialize()==generate_terrain_region(f,mask,path_settings).field.serialize(),"unused drawn path changed directional river");
 path_settings.use_river_path=true;rejects([&]{generate_terrain_region(f,mask,path_settings);});
 path_settings.river_path={{20,20},{20,20}};rejects([&]{generate_terrain_region(f,mask,path_settings);});
 path_settings.river_path={{20,20},{std::numeric_limits<float>::quiet_NaN(),25}};rejects([&]{generate_terrain_region(f,mask,path_settings);});
 path_settings.river_path={{20,20},{60,25}};rejects([&]{generate_terrain_region(f,mask,path_settings);});
 path_settings.river_path.assign(513,{20,20});rejects([&]{generate_terrain_region(f,mask,path_settings);});
 auto invalid_pattern=pattern_settings;invalid_pattern.pattern=static_cast<TerrainNoisePattern>(3);rejects([&]{generate_terrain_region(f,mask,invalid_pattern);});
 invalid_pattern=pattern_settings;invalid_pattern.terrace_steps=1;rejects([&]{generate_terrain_region(f,mask,invalid_pattern);});
 invalid_pattern=pattern_settings;invalid_pattern.warp_strength=std::numeric_limits<float>::infinity();rejects([&]{generate_terrain_region(f,mask,invalid_pattern);});
 check(f.serialize()==before,"new generator patterns mutated source terrain");
 rejects([&]{generate_terrain_region(f,std::span<const uint8_t>{},s);});s.feature_size_cells=0;rejects([&]{generate_terrain_region(f,mask,s);});s.feature_size_cells=14;
 std::fill(mask.begin(),mask.end(),0);rejects([&]{generate_terrain_region(f,mask,s);});
 if(argc>1){const auto p=PckArchive::open(std::filesystem::path(argv[1])/"LEVEL.PCK");const auto loaded=FieldGrid::parse(p.extract("level/asgard.fld"));const auto original=loaded.serialize();
  std::vector<uint8_t> selected(loaded.cells().size(),1);TerrainGeneratorSettings loaded_settings;loaded_settings.height=3;
  for(const auto pattern:{TerrainNoisePattern::Classic,TerrainNoisePattern::DomainWarped,TerrainNoisePattern::Cellular}){
   loaded_settings.pattern=pattern;loaded_settings.terrace_steps=6;
   const auto generated=generate_terrain_region(loaded,selected,loaded_settings);check(generated.changed_cells>0,"loaded stock map was not editable");check(loaded.serialize()==original,"preview changed loaded map in place");
   for(std::size_t i=0;i<loaded.cells().size();++i)if(loaded.cells()[i].water_delta_q12>=0)check(equal(loaded.cells()[i],generated.field.cells()[i]),"loaded map water protection failed");
  }
  std::cout<<"V763_LOADED_MAP PASS stock Asgard, all patterns with terraces, source immutability and water preservation\n";
 }
 std::cout<<"V763_TERRAIN_GENERATOR PASS all presets and patterns, terraces, drawn rivers, repeat/reroll, masks, protections, native river traversal, crossings and FLD roundtrip\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
