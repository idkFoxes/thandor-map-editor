// Frozen-state path diagnostic; recovered FIFO is not a native game replay.
#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/simulation_lab_catalog.hpp"
#include "thandor/v528_pathing.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
static std::vector<std::string> split(std::string s){std::vector<std::string> out;std::stringstream stream(s);std::string v;while(std::getline(stream,v,','))out.push_back(v);return out;}
int main(int argc,char** argv)try{if(argc!=3)throw std::runtime_error("Usage: route_probe PCK_ROOT entities.csv (Mittelpunkt, tick 36000)");using namespace thandor;auto c=AssetCatalog::scan_archive_root(argv[1]);auto g=std::find_if(c.map_groups().begin(),c.map_groups().end(),[](auto& g){return g.display_name=="mittelpunkt";});auto d=MapDocument::open(c,*g);auto statics=simulation_lab_prepare_static_navigation_blockers(c,d.level());
 struct E{uint32_t id,arm,f;int32_t x,y,tx,ty,radius;bool stalled;};std::vector<E> es;
 std::ifstream in(argv[2]);if(!in)throw std::runtime_error("Cannot open entity trace");std::string line;std::getline(in,line);auto h=split(line);std::map<std::string,size_t> col;for(size_t i=0;i<h.size();++i)col[h[i]]=i;
 while(std::getline(in,line)){auto r=split(line);auto v=[&](const char* k){return std::stoll(r.at(col.at(k)));};if(v("tick")!=36000||v("removed"))continue;auto arm=uint32_t(v("arm"));const auto* p=c.army_preset(arm);es.push_back({uint32_t(v("entity_id")),arm,uint32_t(v("faction")),int32_t(v("x_q12")),int32_t(v("y_q12")),int32_t(v("move_target_x_q12")),int32_t(v("move_target_y_q12")),p->simulation_motion_profile.collision_radius_q12,v("stationary_ticks")>=200});}
 for(auto&e:es)if(e.stalled){auto blockers=statics;for(auto&b:es)if(b.id!=e.id)blockers.push_back({b.x,b.y,b.radius,c.army_preset(b.arm)->runtime_model_class_id});auto p=c.army_preset(e.arm);auto mask=v519_path_masks(e.f,p->traversal_low_band_index,p->traversal_high_band_index);
 auto fast=v565_resolve_route_fast_for_simulation_lab(d.field(),e.x,e.y,e.tx,e.ty,mask.faction_presence_mask,p->traversal_low_band_index,p->traversal_high_band_index,blockers,e.radius);
 auto fifo=v528_resolve_route(d.field(),e.x,e.y,e.tx,e.ty,mask.faction_presence_mask,p->traversal_low_band_index,p->traversal_high_band_index,blockers,e.radius);
 for(auto pair:{std::pair{"fast",&fast},std::pair{"fifo",&fifo}}){auto& result=*pair.second;std::cout<<"ROUTE entity="<<e.id<<" solver="<<pair.first<<" disposition="<<v528_route_disposition_name(result.disposition)<<" usable="<<result.usable()<<" expanded="<<result.expanded_nodes<<" primary="<<(result.primary_world_q12?result.primary_world_q12->x_q12:0)<<","<<(result.primary_world_q12?result.primary_world_q12->y_q12:0)<<" resolved="<<result.resolved_world_q12.x_q12<<","<<result.resolved_world_q12.y_q12<<"\n";}}
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
