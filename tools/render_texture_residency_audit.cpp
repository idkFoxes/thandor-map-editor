#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/render_texture_dependencies.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include <iostream>
#include <set>
using namespace thandor;
int main(int argc,char**argv)try{
 if(argc<2||argc>3||(argc==3&&std::string(argv[2])!="--all"))throw std::runtime_error("usage: render_texture_residency_audit PCK_ROOT [--all]");
 const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
 std::set<uint64_t> keys;std::set<uint32_t> effects,shots;
 const auto visit_sprite=[&](const std::string& path,uint32_t frames,bool army){
  const auto sprite=SpriteAsset::parse(catalog.load(path));const auto& mesh=sprite.mesh();
  for(size_t i=0;i+2<mesh.indices.size();i+=3){const auto& vertex=mesh.vertices.at(mesh.indices[i]);
   if(vertex.texture_subresource==UINT32_MAX)continue;
   for(uint32_t frame=0;frame<std::max(1U,frames);++frame)
    keys.insert((army?0U:(uint64_t{1}<<63U))|(vertex.texture_subresource+frame));
  }
 };
 RenderTextureDependencies preload(
  [&](uint32_t id){return catalog.effect_definition(id);},
  [&](uint32_t id){return catalog.shot_definition(id);},
  [&](const auto& effect){effects.insert(effect.definition_id);visit_sprite(effect.sprite_path,effect.runtime_value_0c,(effect.creation_flags&2)!=0);},
  [&](const auto& shot){shots.insert(shot.definition_id);visit_sprite(shot.sprite_path,shot.animation_frame_count,false);});
 preload.shot(0);
 if(argc==3){const auto runtime=simulation_lab_prepare_runtime_catalog(catalog);
  for(const auto& [id,unused]:runtime->shots){(void)unused;preload.shot(id);}
  for(const auto& [id,unused]:runtime->effects){(void)unused;preload.effect(id);}
 }
 const auto missing_key=uint64_t{1}<<63U;
 if(!keys.contains(missing_key))throw std::runtime_error("stock SHT0 did not preload the reported missing key");
 for(auto id:{9U,71U})if(!effects.contains(id))throw std::runtime_error("stock laser effect chain was omitted");
 std::cout<<"PASS mode="<<(argc==3?"all_catalog":"SHT0")<<" stock_SHT0="<<catalog.shot_definition(0)->sprite_path
          <<" reported_key="<<missing_key<<" resident=1 keys="<<keys.size()<<" shots="<<shots.size()<<" effects="<<effects.size()<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
