#pragma once

#include "thandor/gfx.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace editor {
// Recycles source RGBA pixel buffers between independent full object-scene builds.
// The rebuild owns its new layer order; old key->layer indices only select movable
// pixel buffers. It does not keep old GPU slots or influence render ordering.
// Source families are immutable while their signature remains unchanged. Caller
// bypasses obtain() for editable player texture overrides.
class ObjectTextureSourceReuse {
public:
    void begin(const std::string& source_signature) {
        if (source_signature != source_signature_) {
            previous_.clear();
            previous_lookup_.clear();
            source_signature_ = source_signature;
        }
        working_ = std::move(previous_);
    }

    template<class Build>
    thandor::RgbaImage obtain(std::uint64_t key, Build&& build) {
        if (auto found = previous_lookup_.find(key); found != previous_lookup_.end() &&
                found->second < working_.size()) {
            auto& image = working_[found->second];
            if (image.width != 0U && image.height != 0U && !image.pixels.empty()) {
                ++hits_;
                return std::move(image);
            }
        }
        ++misses_;
        return std::forward<Build>(build)();
    }

    void commit(std::vector<thandor::RgbaImage>&& sources,
                const std::unordered_map<std::uint64_t,std::uint32_t>& lookup) {
        // Copy the relatively small key map before committing images. If it
        // throws, the old cache is invalidated without affecting renderer state.
        auto next_lookup=lookup;
        previous_ = std::move(sources);
        previous_lookup_ = std::move(next_lookup);
        working_.clear();
    }

    std::uint64_t hits() const noexcept { return hits_; }
    std::uint64_t misses() const noexcept { return misses_; }
    std::size_t retained_bytes() const noexcept {
        std::size_t bytes=0;
        for (const auto& image:previous_) bytes+=image.pixels.size();
        return bytes;
    }
private:
    std::string source_signature_;
    std::unordered_map<std::uint64_t,std::uint32_t> previous_lookup_;
    std::vector<thandor::RgbaImage> previous_,working_;
    std::uint64_t hits_{},misses_{};
};
} // namespace editor
