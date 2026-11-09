#pragma once

#include "thandor/model.hpp"

#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>

namespace editor {

// Caches *decoded* immutable GFX sprite canvases between static-scene rebuilds.
// Return by value because the caller owns the assembled texture-layer array;
// no cache reference survives eviction or a source-family replacement.
// Budget applies to RGBA pixel payloads, not driver textures or source GFX data.
class SpriteCanvasCache {
public:
    static constexpr std::size_t kDefaultBudgetBytes = 8U * 1024U * 1024U;
    explicit SpriteCanvasCache(std::size_t budget_bytes = kDefaultBudgetBytes)
        : budget_bytes_(budget_bytes) {}

    template<class Decode>
    thandor::RgbaImage obtain(std::uint32_t subresource, Decode&& decode) {
        if (auto found = layers_.find(subresource); found != layers_.end()) {
            ++hits_;
            order_.splice(order_.begin(), order_, found->second.order);
            return found->second.image;
        }
        ++misses_;
        auto canvas = thandor::make_sprite_texture_square_canvas(
            std::forward<Decode>(decode)());
        const auto bytes = canvas.pixels.size();
        if (bytes > budget_bytes_) return canvas;
        while (resident_bytes_ > budget_bytes_ - bytes && !order_.empty()) {
            const auto last = order_.back();
            const auto found = layers_.find(last);
            resident_bytes_ -= found->second.image.pixels.size();
            layers_.erase(found);
            order_.pop_back();
            ++evictions_;
        }
        order_.push_front(subresource);
        try {
            layers_.emplace(subresource, Entry{canvas, order_.begin()});
        } catch (...) {
            order_.pop_front();
            throw;
        }
        resident_bytes_ += bytes;
        return canvas;
    }

    std::uint64_t hits() const noexcept { return hits_; }
    std::uint64_t misses() const noexcept { return misses_; }
    std::uint64_t evictions() const noexcept { return evictions_; }
    std::size_t resident_bytes() const noexcept { return resident_bytes_; }

private:
    struct Entry {
        thandor::RgbaImage image;
        std::list<std::uint32_t>::iterator order;
    };
    std::size_t budget_bytes_{};
    std::size_t resident_bytes_{};
    std::uint64_t hits_{}, misses_{}, evictions_{};
    std::list<std::uint32_t> order_;
    std::unordered_map<std::uint32_t, Entry> layers_;
};

} // namespace editor
