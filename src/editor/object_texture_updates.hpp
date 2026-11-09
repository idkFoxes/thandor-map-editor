#pragma once
#include "thandor/gfx.hpp"
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace editor {
// Immutable CPU snapshots; commit only after the corresponding GPU update succeeds.
// Equality uses complete source/output bytes, never a collision-prone content hash.
struct ObjectTextureLayer {
    thandor::RgbaImage source;
    std::vector<uint8_t> pixels;
    uint64_t fingerprint{}; // hint only: full source comparison always validates reuse
};
struct ObjectTextureUpdate {
    static constexpr uint32_t extent=128;
    static constexpr size_t layer_bytes=size_t(extent)*extent*4;
    uint32_t capacity{};
    bool recreate{};
    uint64_t resampled{},reused{},relocated{};
    std::vector<std::shared_ptr<const ObjectTextureLayer>> layers;
    std::vector<uint32_t> dirty;
    std::vector<uint8_t> packed_pixels()const {
        std::vector<uint8_t> bytes;bytes.reserve(dirty.size()*layer_bytes);
        for(const auto layer:dirty)bytes.insert(bytes.end(),layers.at(layer)->pixels.begin(),layers[layer]->pixels.end());
        return bytes;
    }
};
class ObjectTextureUpdateCache {
public:
    static uint64_t fingerprint(const thandor::RgbaImage& source) noexcept {
        // FNV-1a is a lookup hint, not an equality oracle. Collisions must not
        // affect pixel correctness; every candidate is checked byte-for-byte.
        uint64_t hash=14695981039346656037ULL;
        const auto add=[&](uint8_t byte){hash=(hash^byte)*1099511628211ULL;};
        for(unsigned shift=0;shift<32;shift+=8){add(uint8_t(source.width>>shift));add(uint8_t(source.height>>shift));}
        for(const auto byte:source.pixels)add(byte);
        return hash;
    }
    static std::vector<uint8_t> resample(const thandor::RgbaImage& source) {
        constexpr auto extent=ObjectTextureUpdate::extent;
        std::vector<uint8_t> result(ObjectTextureUpdate::layer_bytes,0);
        const uint64_t size=uint64_t(source.width)*source.height*4;
        if(!source.width||!source.height||size!=source.pixels.size())return result;
        if(source.width==extent&&source.height==extent)return source.pixels;
        for(uint32_t y=0;y<extent;++y)for(uint32_t x=0;x<extent;++x){
            const auto sx=std::min(source.width-1,uint32_t(uint64_t(x)*source.width/extent));
            const auto sy=std::min(source.height-1,uint32_t(uint64_t(y)*source.height/extent));
            std::copy_n(source.pixels.data()+(size_t(sy)*source.width+sx)*4,4,
                result.data()+(size_t(y)*extent+x)*4);
        }
        return result;
    }
    ObjectTextureUpdate prepare(std::span<const thandor::RgbaImage> images,
        uint32_t allocated_capacity,uint32_t maximum_layers,bool has_image)const {
        if(images.empty()||images.size()>maximum_layers)throw std::runtime_error("invalid object texture array layer count");
        ObjectTextureUpdate plan;
        plan.recreate=!has_image||allocated_capacity<images.size();
        plan.capacity=allocated_capacity;
        if(plan.recreate){
            plan.capacity=1;
            while(plan.capacity<images.size()){
                if(plan.capacity>maximum_layers/2){plan.capacity=maximum_layers;break;}
                plan.capacity*=2;
            }
        }
        plan.layers.reserve(images.size());
        // During topology edits texture layers frequently move to different array
        // positions. A slot-only cache treated these as new textures and resampled
        // all of them. Build an index of committed source images and reuse the
        // exact immutable resampled pixels even when their slot changes.
        std::unordered_multimap<uint64_t,std::shared_ptr<const ObjectTextureLayer>> sources;
        sources.reserve(layers_.size()+images.size());
        for(const auto& layer:layers_)if(layer)sources.emplace(layer->fingerprint,layer);
        for(uint32_t i=0;i<images.size();++i){
            const auto& source=images[i];
            const auto old=i<layers_.size()?layers_[i]:nullptr;
            const auto equal_source=[&](const auto& layer){return layer&&
                source.width==layer->source.width&&source.height==layer->source.height&&
                source.pixels==layer->source.pixels;};
            const bool same=equal_source(old);
            if(same){plan.layers.push_back(old);++plan.reused;}
            else{
                const auto hash=fingerprint(source);
                std::shared_ptr<const ObjectTextureLayer> found;
                const auto range=sources.equal_range(hash);
                for(auto it=range.first;it!=range.second;++it){
                    if(equal_source(it->second)){found=it->second;break;}
                }
                if(found){plan.layers.push_back(std::move(found));++plan.reused;++plan.relocated;}
                else{
                    auto layer=std::make_shared<ObjectTextureLayer>();
                    layer->source=source;layer->pixels=resample(source);layer->fingerprint=hash;
                    sources.emplace(hash,layer);
                    plan.layers.push_back(std::move(layer));++plan.resampled;
                }
            }
            // Reusing a resampled layer at a new index does not imply the GPU
            // already contains those bytes at that index. Compare destination
            // contents unless the texture array has been recreated.
            if(plan.recreate||!old||(!same&&old->pixels!=plan.layers.back()->pixels))plan.dirty.push_back(i);
        }
        return plan;
    }
    ObjectTextureUpdate prepare_shared(
        std::span<const std::shared_ptr<const ObjectTextureLayer>> desired,
        uint32_t allocated_capacity, uint32_t maximum_layers, bool has_image) const {
        if (desired.empty() || desired.size()>maximum_layers)
            throw std::runtime_error("invalid versioned texture layer count");
        ObjectTextureUpdate plan;
        plan.recreate=!has_image || allocated_capacity<desired.size();
        plan.capacity=allocated_capacity;
        if (plan.recreate) {
            plan.capacity=1U;
            while(plan.capacity<desired.size()) {
                if(plan.capacity>maximum_layers/2U){plan.capacity=maximum_layers;break;}
                plan.capacity*=2U;
            }
        }
        plan.layers.assign(desired.begin(),desired.end());
        for(uint32_t i=0;i<desired.size();++i) {
            if(!desired[i])throw std::runtime_error("null versioned texture source");
            const auto old=i<layers_.size()?layers_[i]:nullptr;
            if(plan.recreate || !old || old->pixels!=desired[i]->pixels)
                plan.dirty.push_back(i);
            else ++plan.reused;
        }
        return plan;
    }
    const std::vector<std::shared_ptr<const ObjectTextureLayer>>& layers() const noexcept { return layers_; }
    void commit(ObjectTextureUpdate&& plan)noexcept {layers_=std::move(plan.layers);}
private:
    std::vector<std::shared_ptr<const ObjectTextureLayer>> layers_;
};
struct ObjectUploadStats {
    uint64_t calls{},skipped{},array_recreates{},partial_uploads{},layers_uploaded{},bytes_uploaded{},
        buffer_writes{},idle_waits{},frame_fence_waits{},frame_slot_uploads{},frame_slot_skips{},async_copies{},resampled{},reused{},relocated{};
    uint32_t capacity{},last_dirty_layers{};
};
} // namespace editor
