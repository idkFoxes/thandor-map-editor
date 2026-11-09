// Compile model/gfx/binary translation units with ASan/UBSan for this probe.
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"
#include "thandor/binary.hpp"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc!=2)throw std::runtime_error("usage: release_asset_sanitizer GAME_ROOT");
 auto catalog=thandor::AssetCatalog::scan_archive_root(argv[1]);size_t originals=0,mutations=0,rejected=0;
 for(const auto& asset:catalog.assets()){
  const bool sprite=asset.normalized_path.ends_with(".spr"),gfx=asset.normalized_path.ends_with(".gfx");
  if(!sprite&&!gfx)continue;
  const auto bytes=catalog.load(asset);
  const auto parse=[&](const auto& data){
   if(sprite){auto s=thandor::SpriteAsset::parse(data);(void)s.mesh();(void)s.raycast_mesh();for(size_t i=0;i<s.group_count();++i)(void)s.lod_mesh(i);}
   else{auto g=thandor::GfxAsset::parse(data);for(size_t i=0;i<g.subresources().size();++i)(void)g.decode(thandor::GraphicsSubresourceIndex{uint32_t(i)},false);}
  };
  parse(bytes);++originals;
  for(const size_t offset:{0U,4U,12U,0xB0U,0xB4U,0xB8U,0xBCU,0x200U,0x204U,0x208U,0x210U,0x220U}){
   if(offset+4>bytes.size())continue;
   auto bad=bytes;thandor::write_u32(bad,offset,0xFFFFFFFFU);++mutations;
   try{parse(bad);}catch(const std::bad_alloc&){throw;}catch(const std::exception&){++rejected;}
  }
  for(size_t length:{size_t(0),size_t(16),size_t(0x200),bytes.size()-1}){
   auto bad=bytes;bad.resize(std::min(length,bad.size()));++mutations;
   try{parse(bad);}catch(const std::bad_alloc&){throw;}catch(const std::exception&){++rejected;}
  }
 }
 std::cout<<"ASSET_SANITIZER PASS stock_sprite_gfx="<<originals<<" mutations="<<mutations<<" rejected="<<rejected<<" ASan_UBSan=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
