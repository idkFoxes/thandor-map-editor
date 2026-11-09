#include "sprite_canvas_cache.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    constexpr std::size_t one_canvas_bytes = 16U * 16U * 4U;
    editor::SpriteCanvasCache cache(one_canvas_bytes);
    unsigned decodes = 0U;
    const auto decode = [&](std::uint8_t intensity) {
        ++decodes;
        return thandor::RgbaImage{16, 16, std::vector<std::uint8_t>(one_canvas_bytes, intensity)};
    };
    auto first = cache.obtain(11U, [&] { return decode(5U); });
    assert(cache.hits() == 0U && cache.misses() == 1U && decodes == 1U);
    auto same = cache.obtain(11U, [&] { return decode(7U); });
    assert(first.pixels == same.pixels && decodes == 1U);
    assert(cache.hits() == 1U && cache.resident_bytes() == one_canvas_bytes);
    auto second = cache.obtain(12U, [&] { return decode(7U); });
    assert(cache.evictions() == 1U && decodes == 2U);
    assert(first.pixels.front() == 5U && second.pixels.front() == 7U);
    auto restored = cache.obtain(11U, [&] { return decode(5U); });
    assert(restored.pixels == first.pixels && cache.evictions() == 2U && decodes == 3U);

    // A non-square source still retains exactly the retail top-left canvas pixels.
    editor::SpriteCanvasCache square(4096U);
    auto image = square.obtain(2U, [] {
        return thandor::RgbaImage{16, 8, std::vector<std::uint8_t>(16U*8U*4U, 42U)};
    });
    assert(image.width == 16U && image.height == 16U);
    assert(image.pixels[0] == 42U && image.pixels[8U*16U*4U] == 0U);
    assert(square.resident_bytes() == 1024U);
    std::cout << "sprite canvas bounded-cache tests passed\n";
}
