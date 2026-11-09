#include "thandor/map_document.hpp"
#include "thandor/terrain_generator.hpp"
#include "thandor/terrain_generation_quality.hpp"
#include "thandor/new_map_terrain.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
using namespace thandor;
constexpr unsigned N=131;
using Mask=std::vector<uint8_t>;
Mask ellipse(float cx,float cy,float rx,float ry,float phase=0,float wobble=.04F){
 std::vector<TerrainRegionPoint> p;for(int i=0;i<160;++i){float a=2*std::numbers::pi_v<float>*i/160;float r=1+wobble*(std::sin(3*a+phase)+.5F*std::sin(7*a-phase));p.push_back({cx+rx*r*std::cos(a),cy+ry*r*std::sin(a)});}return terrain_polygon_mask(N,N,p);
}
Mask ring(float r1,float r2,float phase){auto m=ellipse(65,65,r2,r2,phase);auto inside=ellipse(65,65,r1,r1,phase+.2F);combine_terrain_region(m,inside,TerrainRegionCombine::Subtract);return m;}
void shape(FieldGrid& f,const Mask& m,const TerrainGeneratorSettings& s,const Mask& protect={}){f=generate_terrain_region(f,m,s,protect).field;}
void ground_layers(FieldGrid& f,bool rainforest){
 for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){auto& c=f.at(x,y);float h=c.terrain_height(),d=0;
  if(x+1<N)d=std::max(d,std::abs(h-f.at(x+1,y).terrain_height()));if(y+1<N)d=std::max(d,std::abs(h-f.at(x,y+1).terrain_height()));
  float variation=std::sin(x*.25F+y*.11F)+std::sin(y*.18F-x*.12F);
  if(c.water_delta_q12>0)c.set_material(10);else if(h<(rainforest?1.88F:.29F))c.set_material(10);
  else if(d>.5F||(rainforest&&h>7))c.set_material(7);
  else c.set_material(rainforest?(variation>.5F?19:24):0);
 }
}
void light(MapDocument& d,bool forest){auto& l=d.level().lighting();l.cycle_duration_ticks=0;l.ramp_12c=forest?0xC0B5E7D5U:0xD0B8F5FFU;l.base_color=0x00DCE5DB;l.ramp_124=forest?0xFF526857U:0xFF555D55U;}
void save(MapDocument& d,const AssetCatalog& c,const std::filesystem::path& path){
 d.save_project(path);auto back=MapDocument::open_project(c,path);if(back.field().serialize()!=d.field().serialize()||!back.level().placements().empty())throw std::runtime_error("project roundtrip/terrain-only check failed");
 size_t wet=0;float lo=1e9,hi=-1e9;for(auto& a:back.field().cells()){wet+=a.water_delta_q12>0;lo=std::min(lo,a.terrain_height());hi=std::max(hi,a.terrain_height());}
 std::cout<<path.filename()<<" dimensions="<<N<<"x"<<N<<" wet="<<wet<<" dry="<<N*N-wet<<" heights="<<lo<<".."<<hi<<" placements=0 roundtrip=PASS\n";
}
int main(int argc,char** argv)try{
 if(argc!=3)throw std::runtime_error("Usage: create_examples GAME_ARCHIVES OUTPUT_DIRECTORY");auto c=AssetCatalog::scan_archive_root(argv[1]);auto group=c.map_groups().front();for(auto& g:c.map_groups())if(g.level_path=="level/hargonatoll.lev")group=g;
 std::filesystem::path out=argv[2];std::filesystem::create_directories(out);Mask all(N*N,1);
 auto at=MapDocument::create_empty(c,group,"atlantis_rings","Atlantis - Coastal Rings",N,N,0,0);at.level().paths().surface_textures="gfx/texturen/water";
 auto& f=at.field();auto land_mask=ellipse(65,65,59,59,.7F,.027F);
 for(auto [inner,outer,phase]:std::vector<std::tuple<float,float,float>>{{12,22,.3F},{42,52,1.2F}}){auto water=ring(inner,outer,phase);combine_terrain_region(land_mask,water,TerrainRegionCombine::Subtract);}
 for(unsigned y=60;y<=70;++y)for(unsigned x=65;x<123;++x)land_mask[y*N+x]=1;
 std::vector<float> land_weights(N*N),sea_weights(N*N);for(size_t i=0;i<land_mask.size();++i){land_weights[i]=land_mask[i]?1:0;sea_weights[i]=land_mask[i]?0:1;}
 auto inland=terrain_basin_distances(f,land_weights),offshore=terrain_basin_distances(f,sea_weights);
 for(size_t i=0;i<land_mask.size();++i){auto& cell=f.cells()[i];float h=land_mask[i]?.2F+.12F+std::min(.60F,inland[i]*.14F):.2F-.06F-std::min(.75F,offshore[i]*.14F);cell.terrain_height_q12=float_to_q12(h);cell.water_delta_q12=float_to_q12(.2F)-cell.terrain_height_q12;}
 for(auto site:std::vector<TerrainRegionPoint>{{65,65},{96,65},{49,92},{49,38}})for(int dy=-5;dy<=5;++dy)for(int dx=-5;dx<=5;++dx){if(std::abs(dx+dy)>5)continue;auto& cell=f.at(static_cast<unsigned>(site.x)+dx,static_cast<unsigned>(site.y)+dy);cell.terrain_height_q12=float_to_q12(.92F);cell.water_delta_q12=float_to_q12(.2F)-cell.terrain_height_q12;}
 align_generated_shores(f,std::vector<float>(N*N,1));
 TerrainGenerationResult at_check;at_check.field=f;at_check.building_sites={{65,65},{96,65},{49,92},{49,38}};assess_generated_ground(at_check);std::cout<<"Atlantis connected building areas="<<at_check.connected_building_sites<<"/4 reachable="<<at_check.reachable_cells<<"\n";
 if(!at_check.playability_ok)throw std::runtime_error("Atlantis terrain access failed");
 ground_layers(f,false);light(at,false);save(at,c,out/"Atlantis_Coastal_Rings.thmap");
 TerrainGeneratorSettings lake;lake.preset=TerrainGeneratorPreset::Lake;lake.keep_water=false;lake.pattern=TerrainNoisePattern::DomainWarped;lake.feature_size_cells=9;
 auto demo=MapDocument::create_empty(c,group,"mountain_methods","Mountains - Connected Valleys",N,N,float_to_q12(2.2F),24);demo.level().paths().surface_textures="gfx/texturen/water";
 TerrainGeneratorSettings ds;ds.preset=TerrainGeneratorPreset::Mountains;ds.pattern=TerrainNoisePattern::DomainWarped;ds.height=10;ds.feature_size_cells=16;ds.seed=55631;
 auto dm=generate_terrain_region(demo.field(),all,ds);if(!dm.playability_ok)throw std::runtime_error("mountain demonstration failed");demo.field()=dm.field;ground_layers(demo.field(),true);light(demo,true);save(demo,c,out/"Mountains_Connected_Valleys.thmap");
 auto rf=MapDocument::create_empty(c,group,"rainforest_basin","Rainforest - River Basin",N,N,float_to_q12(2.2F),24);rf.level().paths().surface_textures="gfx/texturen/water";auto& forest=rf.field();
 TerrainGeneratorSettings mountains;mountains.preset=TerrainGeneratorPreset::Mountains;mountains.pattern=TerrainNoisePattern::DomainWarped;mountains.height=3;mountains.feature_size_cells=16;mountains.warp_strength=1.2F;mountains.seed=83477;mountains.keep_water=false;
 auto land=generate_terrain_region(forest,all,mountains);if(!land.playability_ok)throw std::runtime_error("initial rainforest layout did not pass");forest=land.field;Mask protect(N*N);
 for(auto p:land.building_sites){std::cout<<"rainforest building site "<<p.x<<","<<p.y<<"\n";for(int dy=-5;dy<=5;++dy)for(int dx=-5;dx<=5;++dx){if(std::abs(dx+dy)>5)continue;int x=static_cast<int>(p.x)+dx,y=static_cast<int>(p.y)+dy;if(x>=0&&y>=0&&x<int(N)&&y<int(N))protect[y*N+x]=1;}}
 // A basin in low ground, away from the reserved building footprints.
 float best=1e9;TerrainRegionPoint basin{89,83};
 for(unsigned y=16;y<38;++y)for(unsigned x=103;x<116;++x){if(protect[y*N+x])continue;float score=forest.at(x,y).terrain_height()+.04F*std::hypot(float(x)-108,float(y)-26);if(score<best){best=score;basin={float(x),float(y)};}}
 auto basin_mask=ellipse(basin.x,basin.y,15,12,.5F,.14F);lake.base_height=2.2F;lake.height=1.6F;lake.water_level=1.85F;lake.blend_cells=1;
 const std::vector<std::vector<TerrainRegionPoint>> routes={{{90,10},{87,20},{70,35},{60,55},{65,78},{76,96},{75,115},{72,120}},{{10,65},{18,66},{35,58},{60,55}},{{120,87},{113,86},{94,91},{76,96}}};
 for(std::size_t i=0;i<routes.size();++i){TerrainGeneratorSettings river;river.preset=TerrainGeneratorPreset::River;river.keep_water=false;river.use_river_path=true;river.river_path=routes[i];river.drivable_river=true;river.shallow_depth=.08F;river.water_level=1.85F;river.river_width_cells=i?3.5F:5.5F;river.blend_cells=18;river.river_bend=.13F;river.feature_size_cells=16;river.seed=99123+static_cast<uint32_t>(i);auto channel=generate_terrain_region(forest,all,river,protect);std::cout<<"river "<<i<<" wet="<<channel.river_water_cells<<" blocked="<<channel.blocked_river_count<<"\n";for(size_t k=0,reported=0;k<channel.blocked_river_cells.size()&&reported<6;++k)if(channel.blocked_river_cells[k]){std::cout<<" blocked cell "<<k%N<<","<<k/N<<" wet="<<(channel.field.cells()[k].water_delta_q12>0)<<"\n";++reported;}forest=std::move(channel.field);}
 shape(forest,basin_mask,lake,protect);ground_layers(forest,true);light(rf,true);TerrainGenerationResult check;check.field=forest;check.building_sites=land.building_sites;assess_generated_ground(check);std::cout<<"rainforest connected building sites="<<check.connected_building_sites<<"/"<<check.building_sites.size()<<" traversable="<<check.traversable_cells<<" reachable="<<check.reachable_cells<<"\n";
 if(!check.playability_ok)throw std::runtime_error("final rainforest layout lost ground access");save(rf,c,out/"Rainforest_River_Basin.thmap");
}catch(std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
