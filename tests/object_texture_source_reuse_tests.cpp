#include "object_texture_source_reuse.hpp"
#include <cassert>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <iostream>

using thandor::RgbaImage;
static RgbaImage image(std::uint8_t color) {
    return RgbaImage{2,2,std::vector<std::uint8_t>(16,color)};
}
int main() {
    editor::ObjectTextureSourceReuse cache;
    cache.begin("mapA|army0|effect0");
    auto a=cache.obtain(10,[]{return image(12);});
    auto b=cache.obtain(20,[]{return image(23);});
    std::vector<RgbaImage> first;
    first.push_back(std::move(a));first.push_back(std::move(b));
    const auto* old_data=first[1].pixels.data();
    cache.commit(std::move(first),{{10,0},{20,1}});
    assert(cache.retained_bytes()==32);
    cache.begin("mapA|army0|effect0");
    auto newer=cache.obtain(20,[]{assert(false && "unexpected decode");return image(99);});
    assert(newer.pixels.data()==old_data); // moved pixels, no deep copy
    assert(newer.pixels[0]==23);
    auto other=cache.obtain(30,[]{return image(34);});
    cache.commit(std::vector<RgbaImage>{std::move(newer),std::move(other)},{{20,0},{30,1}});
    assert(cache.hits()==1 && cache.misses()==3);
    cache.begin("mapA|army0|effect0");
    auto changed=cache.obtain(20,[]{assert(false);return image(99);});
    assert(changed.pixels[0]==23);
    cache.commit(std::vector<RgbaImage>{std::move(changed)},{{20,0}});
    cache.begin("mapB|army0|effect0");
    auto invalidated=cache.obtain(20,[]{return image(77);});
    assert(invalidated.pixels[0]==77);
    cache.commit(std::vector<RgbaImage>{std::move(invalidated)},{{20,0}});
    cache.begin("mapB|army0|effect0");
    // Editable overrides intentionally bypass obtain(), so their pixels are never stale.
    auto override=image(99);
    cache.commit(std::vector<RgbaImage>{std::move(override)},{{20,0}});
    cache.begin("mapB|army0|effect0");
    auto final=cache.obtain(20,[]{assert(false);return image(77);});
    assert(final.pixels[0]==99);
    std::cout<<"object texture source move reuse passed\n";
}
