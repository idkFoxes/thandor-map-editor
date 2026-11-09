#include "../src/editor/object_texture_stable_slots.hpp"
#include "../src/editor/object_texture_updates.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using namespace editor;
static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
int main() try {
    const std::unordered_map<uint64_t,uint32_t> aliases{{99,0},{5,0},{7,1},{UINT64_MAX,0}};
    const auto unique=unique_object_texture_slot_keys(aliases,3);
    check(unique.size()==2&&unique.at(5)==0&&unique.at(7)==1,"fallback aliases did not select deterministic image representatives");
    const auto alias_permutation=stable_object_texture_slot_permutation(unique,aliases,3);
    check(alias_permutation.size()==3&&alias_permutation[2]==2,"fallback alias normalization moved an anonymous image");
    check(alias_permutation[aliases.at(99)]==alias_permutation[aliases.at(5)],"fallback aliases no longer share one image");

    std::mt19937 engine{685};
    std::unordered_map<std::uint64_t,std::uint32_t> previous;
    std::vector<std::uint64_t> semantic_keys;
    ObjectTextureUpdateCache gpu;
    std::uint32_t capacity=0;
    std::vector<std::vector<std::uint8_t>> previous_gpu;
    std::uint64_t full_bytes=0,stable_bytes=0;
    for (std::uint32_t step=0;step<180;++step) {
        // Emulate a scenario with mostly resident images but changing scene
        // traversal order; periodically grow/shrink and replace some assets.
        if (step%17==0 && semantic_keys.size()>8)
            semantic_keys.erase(semantic_keys.begin(),semantic_keys.begin()+2);
        if (semantic_keys.size()<8 || step%13==0)
            for (int n=0;n<3;++n) semantic_keys.push_back(1000ULL+step*3+n);
        std::shuffle(semantic_keys.begin(),semantic_keys.end(),engine);
        const auto count=semantic_keys.size();
        std::unordered_map<std::uint64_t,std::uint32_t> current;
        std::vector<thandor::RgbaImage> images;
        for (std::uint32_t i=0;i<count;++i) {
            current.emplace(semantic_keys[i],i);
            thandor::RgbaImage image{4,4,std::vector<std::uint8_t>(64,
                static_cast<std::uint8_t>((semantic_keys[i]*59)%251))};
            images.push_back(std::move(image));
        }
        const auto permutation=stable_object_texture_slot_permutation(current,previous,count);
        std::vector<thandor::RgbaImage> ordered(count);
        std::vector<bool> assigned(count,false);
        std::uint32_t unchanged=0;
        for (std::uint32_t i=0;i<count;++i) {
            const auto destination=permutation[i];
            check(destination<count&&!assigned[destination],"not a bijection");
            assigned[destination]=true;
            auto found=previous.find(semantic_keys[i]);
            if(found!=previous.end() && found->second<count && found->second==destination) ++unchanged;
            ordered[destination]=std::move(images[i]);
            current[semantic_keys[i]]=destination;
        }
        check(std::all_of(assigned.begin(),assigned.end(),[](bool b){return b;}),"hole in atlas");
        const auto update=gpu.prepare(ordered,capacity,256,capacity!=0);
        if(update.recreate)capacity=update.capacity;
        for(std::uint32_t i=0;i<count;++i)
            check(update.layers[i]->pixels==ObjectTextureUpdateCache::resample(ordered[i]),"incorrect mapped pixels");
        full_bytes+=count*ObjectTextureUpdate::layer_bytes;
        stable_bytes+=update.dirty.size()*ObjectTextureUpdate::layer_bytes;
        gpu.commit(ObjectTextureUpdate(update));
        previous=std::move(current);
        if(step>20 && step%10!=0)check(unchanged>0,"no existing resident semantic slot retained");
    }
    // Regression for Asgard V685: a generated shadow atlas was appended to
    // texture_layers without a semantic key. The old implementation threw on
    // current.size()!=images.size(), leaving every tree/object texture absent.
    {
        const std::unordered_map<std::uint64_t,std::uint32_t> before{{101,0},{102,1},{103,2}};
        const std::unordered_map<std::uint64_t,std::uint32_t> after{{101,3},{102,0},{103,2}};
        // Image 1 is an anonymous generated-shadow atlas, not in 'after'.
        const auto mapping=stable_object_texture_slot_permutation(after,before,4);
        check(mapping.size()==4,"shadow atlas mapping count");
        check(mapping[1]==1,"shadow atlas image moved");
        check(mapping[3]==0,"existing semantic layer did not retain slot zero");
        check(mapping[0]==3 && mapping[2]==2,"remaining semantic slot allocation");
        std::vector<std::uint32_t> image_ids{102,9999,103,101};
        std::vector<std::uint32_t> reordered(image_ids.size(),0);
        for(std::uint32_t i=0;i<image_ids.size();++i)
            reordered[mapping[i]]=image_ids[i];
        check(reordered[1]==9999,"shadow mask source image changed");
        for(const auto& [key,source_index]:after)
            check(reordered[mapping[source_index]]==key,"semantic layer corrupted");
        // Static generated shadow receiver vertices keep texture_layer==1.
        const std::uint32_t shadow_receiver_layer=1;
        check(reordered[shadow_receiver_layer]==9999,"shadow receiver sampled a sprite");
    }
    {
        // Generated-shadow atlases can also be interspersed while multiple
        // casters are assembled. Random reorder tests below include holes.
        std::unordered_map<std::uint64_t,std::uint32_t> before;
        for(std::uint32_t i=0;i<53;++i)before.emplace(1000+i,i);
        std::unordered_map<std::uint64_t,std::uint32_t> after;
        for(std::uint32_t i=0;i<53;++i)after.emplace(1000+i,(i+17)%53);
        const auto first_build=stable_object_texture_slot_permutation(after,{},64);
        for(std::uint32_t i=0;i<64;++i)
            check(first_build[i]==i,"first map build must retain original texture ordering");
        // The actual failed Asgard rebuild had 64 images but only 53
        // semantic keys: 11 generated shadow atlases were anonymous.
        const auto mapping=stable_object_texture_slot_permutation(after,before,64);
        std::vector<bool> used(64,false);
        for(std::uint32_t anon=53;anon<64;++anon)
            check(mapping[anon]==anon,"Asgard generated shadow atlas relocated");
        for(auto slot:mapping){check(slot<64&&!used[slot],"atlas permutation is not bijective");used[slot]=true;}
    }
    {
        std::unordered_map<std::uint64_t,std::uint32_t> previous_keys;
        for(std::uint32_t frame=0;frame<120;++frame){
            constexpr std::uint32_t count=40;
            std::vector<std::uint32_t> indices(count);
            for(std::uint32_t i=0;i<count;++i)indices[i]=i;
            std::shuffle(indices.begin(),indices.end(),engine);
            const std::uint32_t anonymous=frame%count;
            std::unordered_map<std::uint64_t,std::uint32_t> keyed;
            for(std::uint32_t k=0,j=0;k<count;++k){
                if(indices[k]==anonymous)continue;
                keyed.emplace(5000ULL+j++,indices[k]);
            }
            const auto mapping=stable_object_texture_slot_permutation(keyed,previous_keys,count);
            check(mapping[anonymous]==anonymous,"anonymous layer moved during churn");
            std::vector<bool> used(count,false);
            for(auto slot:mapping){check(slot<count&&!used[slot],"churn produced duplicate slot");used[slot]=true;}
            for(auto& [key,index]:keyed)index=mapping[index];
            previous_keys=std::move(keyed);
        }
    }
    {
        // Anonymous-only image arrays are valid as well.
        const auto mapping=stable_object_texture_slot_permutation({}, {},3);
        check(mapping==std::vector<std::uint32_t>({0,1,2}),"anonymous-only atlas changed");
    }
    check(stable_bytes<full_bytes/3,"stable key mapping failed to suppress uploads");
    bool rejected=false;
    try { (void)stable_object_texture_slot_permutation({{1,0},{2,0}}, {},2); }
    catch(const std::invalid_argument&) {rejected=true;}
    check(rejected,"duplicate source index allowed");
    std::cout<<"OBJECT_TEXTURE_STABLE_SLOTS PASS full_bytes="<<full_bytes
             <<" upload_bytes="<<stable_bytes<<'\n';
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';return 1;
}
