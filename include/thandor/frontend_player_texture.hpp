#pragma once

#include "thandor/gfx.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace thandor {

struct FrontendPlayerPcx64 {
    std::array<std::array<uint8_t, 3>, 256> palette_rgb{};
    std::array<uint8_t, 4096> indexed_pixels{};
};

// Retail PersistentSettingsImage stores the local frontend player name as twenty UTF-16LE code
// units at byte offset 0x60. Empty/truncated images return an empty string.
std::u16string persistent_settings_player_name(std::span<const std::byte> settings_image);

// PcxPreview_Load64x64PaletteAndPixelsCf removes these filename characters from the UTF-16 player
// name before appending .pcx: * . ? / \\ < > " : |
std::u16string retail_frontend_player_pcx_leaf(std::u16string_view player_name);

// Decode the exact frontend preview domain accepted by retail: 8-bit indexed, one plane, 64x64.
// The PCX scanline padding is discarded and the trailing 256-entry RGB palette is retained.
FrontendPlayerPcx64 decode_frontend_player_pcx64(std::span<const std::byte> data);

// ArmyGraphics_CopyFrontendPlayerPaletteAndTexture converts RGB black to ARGB alpha 0 and every
// non-black palette entry to alpha 255, then uses the 4096 indices as the replacement 64x64 image.
RgbaImage render_retail_frontend_player_texture(const FrontendPlayerPcx64& preview);

} // namespace thandor
