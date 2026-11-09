#pragma once
#include "thandor/field.hpp"
#include <array>
#include <optional>
namespace thandor {
uint32_t retail_uint64_sqrt(uint64_t value) noexcept;
std::array<int32_t, 3> retail_direction_scaled(uint32_t elevation,
                                               uint32_t azimuth, int32_t scale);
// Model field order: angle2 (heading), angle1 (elevation), angle0 (normal
// azimuth).
std::array<int32_t, 9> retail_model_basis_q28(uint32_t angle2, uint32_t angle1,
                                              uint32_t angle0);
std::array<int32_t,9> retail_compose_model_basis_q28(
    const std::array<int32_t,9>& outer,const std::array<int32_t,9>& inner);
std::array<uint32_t,3> retail_extract_model_angles(const std::array<int32_t,9>& basis);
std::array<uint32_t,3> retail_compose_model_angles(
    const std::array<uint32_t,3>& input,const std::array<uint32_t,3>& basis);
std::optional<uint32_t>
retail_contact_normal_angles(const FieldGrid &, int32_t x_q12, int32_t y_q12);
} // namespace thandor
