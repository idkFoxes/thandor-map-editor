#include "../src/editor/editor_asset_fallback.hpp"
#include <iostream>
#include <stdexcept>
int main()try{
 const auto sprite=editor::missing_sprite_placeholder();
 if(sprite.group_count()!=1||!sprite.mesh().indices.empty()||!sprite.raycast_mesh().indices.empty()||
    !sprite.lod_mesh(0).indices.empty()||sprite.has_generated_shadow_mesh())throw std::runtime_error("invalid fallback sprite");
 const auto image=editor::missing_texture_placeholder();
 const auto gfx=thandor::GfxAsset::parse(thandor::encode_single_rgba_gfx(image));
 const auto decoded=gfx.decode(thandor::GraphicsSubresourceIndex{0},false);
 if(decoded.width!=image.width||decoded.height!=image.height||decoded.pixels!=image.pixels)throw std::runtime_error("invalid fallback GFX");
 std::cout<<"ASSET_FALLBACK PASS valid_empty_LOD=1 raycast_safe=1 texture_roundtrip=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
