#include "../src/editor/object_texture_updates.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
static thandor::RgbaImage colored(uint8_t seed){
    thandor::RgbaImage image;image.width=4;image.height=4;
    image.pixels.resize(4*4*4);
    for(size_t i=0;i<image.pixels.size();++i) image.pixels[i]=uint8_t(seed+i);
    return image;
}
int main()try{
    // Frame slots are independently versioned. If slot 1 is still displaying A,
    // updating slot 0 with B must not touch slot 1 or force a full re-upload.
    editor::ObjectTextureUpdateCache logical,slot0,slot1;
    const std::array<thandor::RgbaImage,3> first{colored(11),colored(22),colored(33)};
    auto cpu=logical.prepare(first,512,512,true);
    logical.commit(std::move(cpu));
    const auto& ready=logical.layers();
    auto a=slot0.prepare_shared(ready,0,512,false);
    require(a.recreate&&a.capacity==4&&a.dirty==std::vector<uint32_t>({0,1,2}),"initial slot 0");
    require(a.packed_pixels().size()==3*editor::ObjectTextureUpdate::layer_bytes,"initial packed pixels");
    slot0.commit(std::move(a));
    auto b=slot1.prepare_shared(ready,0,512,false);
    require(b.recreate&&b.dirty.size()==3,"initial slot 1");
    slot1.commit(std::move(b));
    const auto* untouched=slot1.layers()[1].get();

    auto latest=first;latest[1]=colored(99);
    cpu=logical.prepare(latest,512,512,true);
    require(cpu.dirty==std::vector<uint32_t>{1},"logical single change");
    logical.commit(std::move(cpu));
    auto delta0=slot0.prepare_shared(logical.layers(),4,512,true);
    require(!delta0.recreate&&delta0.dirty==std::vector<uint32_t>{1},"slot 0 partial update");
    slot0.commit(std::move(delta0));
    require(slot1.layers()[1].get()==untouched,"in-flight old slot content mutated");
    auto unchanged0=slot0.prepare_shared(logical.layers(),4,512,true);
    require(unchanged0.dirty.empty(),"no change requires no GPU upload");
    auto delta1=slot1.prepare_shared(logical.layers(),4,512,true);
    require(delta1.dirty==std::vector<uint32_t>{1},"late frame must catch up to current scene");
    slot1.commit(std::move(delta1));

    // Switching back to older run should upload only the differing layer.
    cpu=logical.prepare(first,512,512,true);
    logical.commit(std::move(cpu));
    auto reverse=slot0.prepare_shared(logical.layers(),4,512,true);
    require(reverse.dirty==std::vector<uint32_t>{1},"run switch scene version not restored");
    slot0.commit(std::move(reverse));

    // Per-slot growth always recreates and uploads every live layer.
    std::vector<thandor::RgbaImage> bigger(first.begin(),first.end());
    bigger.push_back(colored(44));bigger.push_back(colored(55));
    cpu=logical.prepare(bigger,512,512,true);logical.commit(std::move(cpu));
    auto grow=slot0.prepare_shared(logical.layers(),4,512,true);
    require(grow.recreate&&grow.capacity==8&&grow.dirty.size()==5,"capacity growth image replacement");
    auto grow_other=slot1.prepare_shared(logical.layers(),4,512,true);
    require(grow_other.recreate&&grow_other.capacity==8,"other slot must recreate independently");
    slot0.commit(std::move(grow));
    slot1.commit(std::move(grow_other));

    // Equal resampled pixels can share an existing GPU layer even when source
    // dimensions change; alias/semantic index changes must still remain correct.
    auto shrink=slot0.prepare_shared(std::span(logical.layers()).first(2),8,512,true);
    require(!shrink.recreate&&shrink.dirty.empty(),"shrink should not change surviving atlas layers");
    std::cout<<"RENDERING_FRAME_VERSIONED_ATLAS PASS first partial idle late_frame switch grow shrink\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
