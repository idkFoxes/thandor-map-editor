#pragma once
#include <bit>
#include <cstdint>
#include <optional>
namespace thandor {
// AiResourceCandidate_AddPowerPlant. All faction/knowledge fields are raw dwords.
// Empty also contains zero divisors that would fault in the native implementation.
inline std::optional<uint32_t> retail_power_candidate_weight(uint32_t generation_q4,
    uint32_t baseline_q4,uint32_t extraction,uint32_t supplied,uint32_t unpowered,
    uint32_t numerator,uint32_t denominator) noexcept {
    const auto signed_word=[](uint32_t value){return std::bit_cast<int32_t>(value);};
    const int32_t capacity=signed_word(generation_q4)>>4;
    int32_t demand=signed_word(supplied+unpowered)>>4;
    if(demand==0)demand=1;
    const int32_t supply=signed_word(uint32_t(signed_word(baseline_q4)>>4)+extraction);
    const int32_t surplus=signed_word(uint32_t(supply)-uint32_t(capacity));
    if(surplus==0 || capacity>supply || capacity==0 || denominator==0)return {};
    // The original casts the first quotient to int BEFORE coefficient scaling.
    const auto intermediate=signed_word(uint32_t(int64_t(surplus)*demand/capacity));
    return uint32_t(int64_t(intermediate)*signed_word(numerator)/signed_word(denominator));
}
}
