#pragma once
#include "thandor/model.hpp"
#include "thandor/binary.hpp"
namespace editor {
inline thandor::SpriteAsset missing_sprite_placeholder(){
    std::vector<std::byte> bytes(0x220U);
    thandor::write_u32(bytes,0,thandor::fourcc("spr"));
    thandor::write_u32(bytes,4,static_cast<uint32_t>(bytes.size()));
    thandor::write_u32(bytes,0x0C,0x00020007U);
    thandor::write_u32(bytes,0xB0,1U);
    thandor::write_u32(bytes,0x200,0x20U);
    return thandor::SpriteAsset::parse(bytes);
}
inline thandor::RgbaImage missing_texture_placeholder(){
    return {2,2,{255,0,255,255,32,32,32,255,32,32,32,255,255,0,255,255}};
}
}
