#include "asset_preview.hpp"
#include "thandor/catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() {
    using namespace editor;
    AssetPreviewMesh first;
    AssetPreviewTriangle back{};
    back.positions = {{{0.0F,-1.0F,-1.0F},{0.0F,1.0F,-1.0F},{0.0F,0.0F,1.0F}}};
    back.packed_color_argb = 0xFF2040E0U;
    AssetPreviewTriangle front = back;
    front.positions = {{{1.0F,-1.0F,-1.0F},{1.0F,1.0F,-1.0F},{1.0F,0.0F,1.0F}}};
    front.packed_color_argb = 0xFFE04020U;
    first.triangles = {front, back};
    for (const auto& triangle : first.triangles)
        for (const auto point : triangle.positions) include_preview_bounds(first, point);
    rasterize_asset_preview(first, 96U, 72U,
        [](uint32_t, float, float) { return 0xFFFFFFFFU; }, 0.0F, 0.0F);
    require(first.covered_pixel_count > 500U, "depth preview raster covered too few pixels");
    const uint32_t center = first.raster_argb[36U * 96U + 48U];
    require(((center >> 16U) & 0xFFU) > (center & 0xFFU),
            "front triangle did not win the depth test");

    AssetPreviewMesh second;
    second.triangles = {back, front};
    for (const auto& triangle : second.triangles)
        for (const auto point : triangle.positions) include_preview_bounds(second, point);
    rasterize_asset_preview(second, 96U, 72U,
        [](uint32_t, float, float) { return 0xFFFFFFFFU; }, 0.0F, 0.0F);
    require(second.raster_argb == first.raster_argb,
            "preview raster depends on source triangle submission order");

#ifdef THANDOR_SOURCE_DIR
    const std::filesystem::path root = THANDOR_SOURCE_DIR;
#else
    const std::filesystem::path root = std::filesystem::current_path();
#endif
    const auto catalog = read_text(root / "src/core/catalog.cpp");
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto preview = read_text(root / "src/editor/asset_preview.hpp");
    require(catalog.find("resolve_arm_linked_model_hierarchy") != std::string::npos,
            "ARM linked model resolver is absent");
    require(catalog.find("base_arm_model_definition_id") != std::string::npos,
            "ARM base model-definition selector is absent");
    require(catalog.find("linked_arm_model_id") == std::string::npos,
            "obsolete ambiguous ARM linked-model selector name remains");
    require(editor.find("resolved_model_hierarchy") != std::string::npos,
            "editor does not render the resolved model hierarchy");
    require(editor.find("sample_wrapped_bilinear_argb") != std::string::npos,
            "preview does not use bilinear source texture sampling");
    require(editor.find("sample_preview_patch_argb(image,uv,luminous)") == std::string::npos,
            "obsolete four-sample microtriangle averaging remains active");
    require(preview.find("rasterize_asset_preview") != std::string::npos,
            "pixel-depth preview raster is absent");
    std::cout << "G351 ARM linked hierarchy and pixel-depth preview regression PASS\n";
}
