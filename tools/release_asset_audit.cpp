#include "thandor/catalog.hpp"
#include "thandor/audio.hpp"
#include <iostream>

int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("usage: release_asset_audit GAME_ROOT");
    const auto catalog=thandor::AssetCatalog::scan_archive_root(argv[1]);
    const auto report=catalog.validate_supported_assets();
    size_t images=0,sounds=0;
    for(const auto& asset:catalog.assets()){
        if(asset.normalized_path.ends_with(".gfx")){
            const auto gfx=thandor::GfxAsset::parse(catalog.load(asset));
            for(size_t i=0;i<gfx.subresources().size();++i){
                const auto image=gfx.decode(thandor::GraphicsSubresourceIndex{uint32_t(i)},false);
                if(image.pixels.size()!=uint64_t(image.width)*image.height*4)
                    throw std::runtime_error("decoded GFX size mismatch: "+asset.normalized_path);
                ++images;
            }
        }else if(asset.normalized_path.ends_with(".sam")){
            (void)thandor::decode_sam_to_pcm(catalog.load(asset));++sounds;
        }
    }
    std::cout<<"decoded_GFX_subresources="<<images<<" decoded_SAM_sounds="<<sounds<<'\n'
             <<"assets="<<catalog.assets().size()<<" graphics="<<report.texture_files
             <<" models="<<report.model_files<<" effects="<<report.effect_files
             <<" issues="<<report.issues.size()<<'\n';
    for(const auto& issue:report.issues)std::cout<<issue.path<<" "<<issue.message<<'\n';
    return report.ok()?0:1;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
