#include "../src/editor/object_texture_updates.hpp"
#include <iostream>
#include <array>
#include <random>
#include <stdexcept>
using namespace editor;
static void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Independent old full-upload nearest-neighbor oracle, including malformed-image fallback.
static std::vector<uint8_t> reference(const thandor::RgbaImage& image){
    std::vector<uint8_t> result(128*128*4);
    if(!image.width||!image.height||uint64_t(image.width)*image.height*4!=image.pixels.size())return result;
    for(size_t y=0;y<128;++y)for(size_t x=0;x<128;++x)for(size_t c=0;c<4;++c)
        result[(y*128+x)*4+c]=image.pixels[((y*image.height/128)*image.width+x*image.width/128)*4+c];
    return result;
}
int main()try{
    std::mt19937 rng(679);
    for(unsigned i=0;i<256;++i){
        thandor::RgbaImage image{uint32_t(1+rng()%257),uint32_t(1+rng()%193),{}};
        image.pixels.resize(size_t(image.width)*image.height*4);for(auto& b:image.pixels)b=uint8_t(rng());
        if(i%11==0)image.pixels.pop_back();if(i%17==0)image.width=0;
        check(ObjectTextureUpdateCache::resample(image)==reference(image),"canonical bytes differ from old full upload");
    }
    ObjectTextureUpdateCache cache;std::vector<thandor::RgbaImage> images;
    std::vector<std::vector<uint8_t>> gpu;uint32_t capacity=0;uint64_t recreated=0,unchanged=0;
    for(unsigned step=0;step<200;++step){
        const auto count=1+step%23;images.resize(count);
        for(auto& image:images)if(image.pixels.empty()){
            image.width=3;image.height=7;image.pixels.resize(3*7*4);for(auto& b:image.pixels)b=uint8_t(rng());
        }
        images[step%count].pixels[(step*17)%images[step%count].pixels.size()]^=0x80;
        auto plan=cache.prepare(images,capacity,29,capacity!=0);
        check(plan.capacity>=count&&plan.capacity<=29,"capacity growth exceeds device limit");
        if(plan.recreate){gpu.assign(plan.capacity,std::vector<uint8_t>(128*128*4));capacity=plan.capacity;++recreated;}
        const auto payload=plan.packed_pixels();check(payload.size()==plan.dirty.size()*128*128*4,"sparse payload size wrong");
        for(size_t i=0;i<plan.dirty.size();++i){
            const auto layer=plan.dirty[i];check(layer<count,"out-of-range copy admitted");
            std::copy_n(payload.begin()+i*128*128*4,128*128*4,gpu[layer].begin());
        }
        for(size_t i=0;i<images.size();++i)check(gpu[i]==reference(images[i]),"incremental output differs after growth/shrink/mutation");
        const auto frozen=plan.layers;cache.commit(std::move(plan));
        auto identical=cache.prepare(images,capacity,29,true);
        check(identical.dirty.empty()&&identical.resampled==0&&identical.reused==count,"identical rebuild did work");++unchanged;
        // An abandoned preparation must not publish new source bytes to the committed cache.
        const auto original=images[0].pixels[0];images[0].pixels[0]^=1;
        auto abandoned=cache.prepare(images,capacity,29,true);images[0].pixels[0]=original;
        auto recovered=cache.prepare(images,capacity,29,true);check(recovered.dirty.empty(),"uncommitted upload changed retained state");
        check(frozen[0]->source.pixels[0]==original,"immutable layer snapshot mutated");
    }
    check(recreated<8&&unchanged==200,"geometric capacity reuse failed");
    {
        ObjectTextureUpdateCache cache;thandor::RgbaImage image{256,256,std::vector<uint8_t>(256*256*4,7)};
        auto first=cache.prepare(std::span(&image,1),1,1,false);cache.commit(std::move(first));
        image.pixels[4]=99;auto same_output=cache.prepare(std::span(&image,1),1,1,true);
        check(same_output.dirty.empty()&&same_output.resampled==1,"unobserved source texel forced a GPU upload");
        image.pixels[0]=99;auto changed=cache.prepare(std::span(&image,1),1,1,true);check(changed.dirty.size()==1,"changed output texel was skipped");
    }
    {
        ObjectTextureUpdateCache reordered;
        std::vector<thandor::RgbaImage> originals(8);
        for(uint32_t i=0;i<originals.size();++i){
            originals[i].width=5;originals[i].height=5;
            originals[i].pixels.assign(100,static_cast<uint8_t>(i*20+1));
        }
        auto initial=reordered.prepare(originals,8,8,false);
        check(initial.resampled==8,"initial image set not sampled");
        reordered.commit(std::move(initial));
        std::rotate(originals.begin(),originals.begin()+3,originals.end());
        auto moved=reordered.prepare(originals,8,8,true);
        check(moved.resampled==0&&moved.reused==8&&moved.relocated==8,
              "relocated source images were needlessly resampled");
        check(moved.dirty.size()==8,"relocated GPU destinations were not invalidated");
        for(uint32_t layer=0;layer<8;++layer)
            check(moved.layers[layer]->pixels==reference(originals[layer]),"relocated pixels changed");
        reordered.commit(std::move(moved));
        auto again=reordered.prepare(originals,8,8,true);
        check(again.dirty.empty()&&again.relocated==0,"reordered cache did not stabilize");
    }
    {
        ObjectTextureUpdateCache duplicates;
        thandor::RgbaImage a{4,4,std::vector<uint8_t>(64,123)};
        std::array<thandor::RgbaImage,3> source{a,a,a};
        auto plan=duplicates.prepare(source,3,3,false);
        check(plan.resampled==1&&plan.relocated==2,"duplicate image resampled repeatedly");
        check(plan.layers[0]==plan.layers[1]&&plan.layers[1]==plan.layers[2],
              "identical source did not share cached pixels");
    }
    bool rejected=false;try{cache.prepare(images,1,1,true);}catch(const std::exception&){rejected=true;}
    check(rejected,"layer limit not enforced");
    std::cout<<"OBJECT_TEXTURE_UPDATES PASS oracle_cases=256 incremental_steps=200 identical_skips=200 committed_snapshot=1 capacity_limit=1\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
