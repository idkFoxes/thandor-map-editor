#pragma once
#include <bit>
#include <cstdint>
#include <optional>

namespace thandor {
// runtimeUpdate slots 1, 2, 3 and the land branch of slot 18.
inline bool simulation_has_water_damage_fields(uint32_t runtime_class) noexcept {
    return runtime_class==1U||runtime_class==2U||runtime_class==3U||runtime_class==18U;
}
inline bool simulation_uses_water_damage_callback(uint32_t runtime_class,int32_t contact_kind) noexcept {
    return simulation_has_water_damage_fields(runtime_class)&&!(runtime_class==18U&&contact_kind==1);
}
// Strict MDL threshold, signed low-dword IMUL followed by SAR 7. A zero
// result is still an application in native code; negative results are ignored.
inline std::optional<int32_t> simulation_water_damage_amount(
    int32_t delta,int32_t threshold,int32_t multiplier) noexcept {
    if(delta<=threshold)return std::nullopt;
    const int32_t amount=std::bit_cast<int32_t>(uint32_t(delta)*uint32_t(multiplier))>>7;
    return amount<0?std::nullopt:std::optional<int32_t>{amount};
}
}
