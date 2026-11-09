#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace thandor {
// ARM321/322/323 are "Research lab Ion laser", "Research lab Flak guns", and
// "Research lab Rocket artillery" (ENGINE.PCK texte/help.str, via root MDL).
// Coefficients consume hostile pressure channels 2/3/4, not TEC categories
// A/B/C. Each laboratory's research panel can contain multiple TEC categories.
struct RetailPressureBuildingSelection{uint32_t id{},existing{};};
inline RetailPressureBuildingSelection retail_pressure_building(
    const std::array<int32_t,3>& pressure,const std::array<std::array<uint32_t,3>,3>& coefficients,
    uint32_t random,const std::array<bool,3>& present,const std::array<bool,3>& enabled) noexcept {
    const uint32_t denominator=uint32_t(pressure[0])+uint32_t(pressure[1])+uint32_t(pressure[2])+1U;
    RetailPressureBuildingSelection out;
    if(!denominator)return out; // Invalid native divisor is outside admissible inputs.
    uint32_t best=0;constexpr std::array<unsigned,3> shifts{0,19,7};
    for(size_t i=0;i<3;++i){
        out.existing+=present[i];
        uint32_t sum=0;
        for(size_t j=0;j<3;++j)sum+=(uint32_t(pressure[j])+1U)*coefficients[i][j];
        const uint32_t score=sum/denominator+((random>>shifts[i])&127U);
        if(!present[i]&&enabled[i]&&score>best){best=score;out.id=321U+uint32_t(i);}
    }
    return out;
}
}
