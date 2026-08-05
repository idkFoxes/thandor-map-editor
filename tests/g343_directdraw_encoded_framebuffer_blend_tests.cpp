#include "thandor/catalog.hpp"
#include "thandor/gfx.hpp"
#include "../src/editor/primitive_render.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

float srgb_to_linear(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value <= 0.04045F ? value / 12.92F
                            : std::pow((value + 0.055F) / 1.055F, 2.4F);
}

float linear_to_srgb(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value <= 0.0031308F ? value * 12.92F
                              : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}

std::uint8_t linear_light_add(std::uint8_t destination, std::uint8_t source) {
    const float result = srgb_to_linear(destination / 255.0F) +
                         srgb_to_linear(source / 255.0F);
    return static_cast<std::uint8_t>(std::lround(
        std::clamp(linear_to_srgb(result), 0.0F, 1.0F) * 255.0F));
}

struct PixelPeak {
    std::uint8_t r{}, g{}, b{};
};

PixelPeak dominant_nonblack(const thandor::RgbaImage& image) {
    std::map<std::uint32_t, std::size_t> counts;
    for (std::size_t index = 0; index + 3U < image.pixels.size(); index += 4U) {
        const auto r = image.pixels[index];
        const auto g = image.pixels[index + 1U];
        const auto b = image.pixels[index + 2U];
        if (r == 0U && g == 0U && b == 0U) continue;
        ++counts[(static_cast<std::uint32_t>(r) << 16U) |
                 (static_cast<std::uint32_t>(g) << 8U) | b];
    }
    require(!counts.empty(), "resource texture has no nonblack pixels");
    const auto selected = std::max_element(counts.begin(), counts.end(),
        [](const auto& left, const auto& right) { return left.second < right.second; });
    return {static_cast<std::uint8_t>((selected->first >> 16U) & 0xFFU),
            static_cast<std::uint8_t>((selected->first >> 8U) & 0xFFU),
            static_cast<std::uint8_t>(selected->first & 0xFFU)};
}
}

int main(int argc, char** argv) try {
    using editor::directdraw_add_encoded_byte;
    require(directdraw_add_encoded_byte(240U, 30U) == 255U,
            "encoded DirectDraw add does not saturate");
    require(directdraw_add_encoded_byte(120U, 40U) == 160U,
            "encoded DirectDraw add changed ordinary byte addition");
    require(directdraw_add_encoded_byte(200U, 39U) > linear_light_add(200U, 39U) + 25U,
            "gold source no longer demonstrates the DirectDraw/linear blend gap");
    require(directdraw_add_encoded_byte(200U, 107U) == 255U &&
            linear_light_add(200U, 107U) < 225U,
            "blue source no longer demonstrates the DirectDraw/linear blend gap");

    const std::filesystem::path source = THANDOR_SOURCE_DIR;
    const auto context = read_text(source / "src/editor/vulkan_context.cpp");
    const auto terrain = read_text(source / "shaders/terrain.frag");
    const auto model = read_text(source / "shaders/model.frag");
    const auto unorm = context.find("VK_FORMAT_B8G8R8A8_UNORM");
    const auto srgb = context.find("VK_FORMAT_B8G8R8A8_SRGB");
    require(unorm != std::string::npos && srgb != std::string::npos && unorm < srgb,
            "Vulkan surface selection does not prefer the DirectDraw-style UNORM target");
    require(terrain.find("return linearToSrgb(sampledLinear) *") != std::string::npos &&
            terrain.find("return srgbToLinear(linearToSrgb(sampledLinear)") == std::string::npos,
            "terrain shader still converts byte-domain output back to linear");
    require(model.find("return linear_to_srgb(sampled_linear) *") != std::string::npos &&
            model.find("return srgb_to_linear(linear_to_srgb(sampled_linear)") == std::string::npos,
            "model shader still converts byte-domain output back to linear");

    if (argc >= 2) {
        auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        auto graphics = thandor::GfxAsset::parse(catalog.load("gfx/mdl/sarmy0.gfx"));
        const auto power = dominant_nonblack(graphics.decode(thandor::GraphicsSubresourceIndex{27U}, false));
        const auto building = dominant_nonblack(graphics.decode(thandor::GraphicsSubresourceIndex{33U}, false));
        require(power.b >= 90U && power.b > power.r * 2U,
                "stock power particle source lost its blue encoded-byte peak");
        require(building.r >= 35U && building.g >= 18U && building.b == 0U,
                "stock building particle source lost its gold encoded-byte peak");
        const auto power_direct = directdraw_add_encoded_byte(200U, power.b);
        const auto power_linear = linear_light_add(200U, power.b);
        const auto building_direct = directdraw_add_encoded_byte(200U, building.r);
        const auto building_linear = linear_light_add(200U, building.r);
        require(power_direct == 255U && power_linear + 20U < power_direct,
                "stock power source does not expose the encoded additive intensity gap");
        require(building_direct > building_linear + 25U,
                "stock building source does not expose the encoded additive intensity gap");
        std::cout << "G343_STOCK_DIRECTDRAW_ENCODED_BLEND PASS power_peak="
                  << static_cast<unsigned>(power.r) << ',' << static_cast<unsigned>(power.g) << ','
                  << static_cast<unsigned>(power.b) << " building_peak="
                  << static_cast<unsigned>(building.r) << ',' << static_cast<unsigned>(building.g)
                  << ',' << static_cast<unsigned>(building.b) << " power_linear="
                  << static_cast<unsigned>(power_linear) << " power_direct="
                  << static_cast<unsigned>(power_direct) << '\n';
    }

    std::cout << "G343 DirectDraw encoded-framebuffer blend checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G343 DirectDraw encoded-framebuffer blend checks failed: "
              << error.what() << '\n';
    return 1;
}
