#include "../src/editor/texture_sampling.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
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
}

int main() try {
    using editor::TextureSamplingMode;
    using editor::complete_mip_level_count;
    using editor::texture_sampling_plan;

    require(complete_mip_level_count(128U) == 8U,
            "128-pixel full mip-chain count changed");
    require(complete_mip_level_count(256U) == 9U,
            "256-pixel full mip-chain count changed");

    constexpr auto directdraw = texture_sampling_plan(
        128U, TextureSamplingMode::DirectDrawSingleLevelLinear);
    require(directdraw.mip_levels == 1U,
            "DirectDraw object texture unexpectedly gained mip levels");
    require(!directdraw.allow_anisotropy,
            "DirectDraw object texture unexpectedly allows anisotropy");
    require(!directdraw.blend_between_mip_levels,
            "DirectDraw object texture unexpectedly blends mip levels");

    constexpr auto terrain = texture_sampling_plan(
        256U, TextureSamplingMode::MipmappedAnisotropic);
    require(terrain.mip_levels == 9U,
            "terrain texture lost its separate full mip chain");
    require(terrain.allow_anisotropy && terrain.blend_between_mip_levels,
            "terrain sampling policy no longer preserves the established terrain path");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto renderer_header = read_text(root / "src/editor/terrain_renderer.hpp");
    const auto sampling_header = read_text(root / "src/editor/texture_sampling.hpp");

    require(renderer.find("TextureSamplingMode::DirectDrawSingleLevelLinear") !=
                std::string::npos,
            "object texture upload is not routed through the DirectDraw policy");
    require(renderer.find("TextureSamplingMode::MipmappedAnisotropic") !=
                std::string::npos,
            "terrain texture upload lost its separate mipmapped policy");
    require(renderer.find("sampling_plan.blend_between_mip_levels") !=
                std::string::npos &&
            renderer.find("VK_SAMPLER_MIPMAP_MODE_NEAREST") != std::string::npos,
            "single-level sampler does not explicitly disable mip-level interpolation");
    require(renderer.find("sampling_plan.allow_anisotropy") != std::string::npos &&
            renderer.find("use_anisotropy ? VK_TRUE : VK_FALSE") != std::string::npos,
            "anisotropy is not controlled by the selected sampling policy");
    require(renderer.find("destination.mip_levels = sampling_plan.mip_levels") !=
                std::string::npos,
            "image allocation ignores the selected mip count");
    require(renderer_header.find("TextureSamplingMode sampling_mode") != std::string::npos,
            "texture upload API does not require an explicit sampling mode");
    require(sampling_header.find("return {1U, false, false}") != std::string::npos,
            "DirectDraw single-level policy is not sealed in one independent helper");

    std::cout << "G345_DIRECTDRAW_SINGLE_LEVEL_OBJECT_TEXTURE_SAMPLING PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G345 DirectDraw single-level object texture sampling checks failed: "
              << error.what() << '\n';
    return 1;
}
