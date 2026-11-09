#include "thandor/retail_integer_sqrt.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/simulation.hpp"
#include "thandor/simulation_lab.hpp"
#include <bit>
namespace thandor {
namespace {
int32_t w(uint32_t v) { return std::bit_cast<int32_t>(v); }
int32_t add(int32_t a, int32_t b) { return w(uint32_t(a) + uint32_t(b)); }
int32_t sub(int32_t a, int32_t b) { return w(uint32_t(a) - uint32_t(b)); }
int32_t sine(uint32_t a) {
  return retail_placement_sine_table_entry(16384 + (a & 65535));
}
int32_t cosine(uint32_t a) {
  return retail_placement_sine_table_entry(32768 + (a & 65535));
}
int32_t mul(int32_t a, int32_t b, int shift) {
  return w(uint32_t((int64_t(a) * b) >> shift));
}
} // namespace
uint32_t retail_uint64_sqrt(uint64_t value) noexcept { return retail_integer_sqrt(value); }
std::array<int32_t, 3> retail_direction_scaled(uint32_t el, uint32_t az,
                                               int32_t scale) {
  return {mul(add(cosine(az + el), cosine(az - el)), scale, 29),
          mul(add(sine(az + el), sine(az - el)), scale, 29),
          mul(sine(el), scale, 28)};
}
std::array<int32_t, 9> retail_model_basis_q28(uint32_t a, uint32_t b,
                                              uint32_t c) {
  std::array<int32_t, 9> m{};
  // DirectionFromAnglesQ28 sums then shifts, without an intermediate rounded
  // multiply.
  const auto direction = [](uint32_t el, uint32_t az) {
    return std::array<int32_t, 3>{add(cosine(az + el), cosine(az - el)) >> 1,
                                  add(sine(az + el), sine(az - el)) >> 1,
                                  sine(el)};
  };
  const auto z = direction(b, c), back = direction(b, a - c);
  m[2] = z[0];
  m[5] = z[1];
  m[8] = z[2];
  m[6] = sub(0, back[0]);
  m[7] = back[1];
  const auto u = a - 2 * c;
  const auto d = sub(cosine(a), cosine(u)) >> 1,
             e = add(cosine(a), cosine(u)) >> 1;
  const auto s = add(sine(a), sine(u)) >> 1, t = sub(sine(u), sine(a)) >> 1,
             eb = sine(b);
  m[0] = add(d, mul(e, eb, 28));
  m[4] = add(mul(d, eb, 28), e);
  m[1] = sub(t, mul(s, eb, 28));
  m[3] = sub(s, mul(t, eb, 28));
  return m;
}
std::array<int32_t,9> retail_compose_model_basis_q28(
    const std::array<int32_t,9>& a,const std::array<int32_t,9>& b) {
  std::array<int32_t,9> m{};
  for (size_t row=0;row<3;++row)
    for (size_t col=0;col<3;++col)
      m[row*3+col]=static_cast<int32_t>(
          (int64_t(a[row*3])*b[col]+int64_t(a[row*3+1])*b[3+col]+
           int64_t(a[row*3+2])*b[6+col])>>28);
  return m;
}
std::array<uint32_t,3> retail_extract_model_angles(const std::array<int32_t,9>& m) {
  const auto horizontal=retail_uint64_sqrt(
      uint64_t(int64_t(m[2])*m[2])+uint64_t(int64_t(m[5])*m[5]));
  const auto elevation=fixed_atan2_angle16_compat(m[8],static_cast<int32_t>(horizontal));
  const auto azimuth=fixed_atan2_angle16_compat(m[5],m[2])&0xFFFFU;
  // Native keeps the signed atan result in the nonnegative-elevation branch.
  const auto roll=static_cast<int32_t>(elevation)<0
      ?(fixed_atan2_angle16_compat(m[3]+m[1],m[4]-m[0])+2U*azimuth)&0xFFFFU
      :fixed_atan2_angle16_compat(m[3]-m[1],m[4]+m[0]);
  return {roll,elevation,azimuth};
}
std::array<uint32_t,3> retail_compose_model_angles(
    const std::array<uint32_t,3>& input,const std::array<uint32_t,3>& basis) {
  // FixedTransform_ComposeEulerAngles builds basis * input.
  return retail_extract_model_angles(retail_compose_model_basis_q28(
      retail_model_basis_q28(basis[0],basis[1],basis[2]),
      retail_model_basis_q28(input[0],input[1],input[2])));
}
std::optional<uint32_t> retail_contact_normal_angles(const FieldGrid &f,
                                                     int32_t xq, int32_t yq) {
  const uint32_t half = uint32_t((int64_t(yq) * -0x20C8CC) >> 21);
  const uint32_t col = uint32_t((int64_t(xq) * 0x1C6E9C) >> 20) - half,
                 row = half * 2;
  const int32_t x = w(col) >> 12, y = w(row) >> 12;
  if (x < 0 || y < 0 || uint32_t(x) + 1 >= f.width() ||
      uint32_t(y) + 1 >= f.height())
    return {};
  if ((f.at(x, y).flags_and_material | f.at(x + 1, y + 1).flags_and_material) &
      0x88006000U)
    return {};
  const int32_t fx = col & 4095, fy = row & 4095, d = fx + fy - 4096;
  const auto n = [&](uint32_t cx, uint32_t cy, int32_t weight) {
    const auto packed = retail_placement_normal_angles(f, cx, cy, false);
    return retail_direction_scaled(uint32_t(w(packed) >> 16), packed & 65535,
                                   weight);
  };
  const auto a = d < 0 ? n(x + 1, y, fx) : n(x + 1, y + 1, d);
  const auto b = d < 0 ? n(x, y, d) : n(x, y + 1, fx - 4096);
  const auto c = d < 0 ? n(x, y + 1, fy) : n(x + 1, y, fy - 4096);
  std::array<int32_t, 3> sum{};
  for (size_t i = 0; i < 3; ++i)
    sum[i] = d < 0 ? add(sub(a[i], b[i]), c[i]) : sub(sub(a[i], b[i]), c[i]);
  const auto length = retail_uint64_sqrt(uint64_t(int64_t(sum[0]) * sum[0]) +
                                         uint64_t(int64_t(sum[1]) * sum[1]));
  return (fixed_atan2_angle16_compat(sum[2], w(length)) << 16) |
         (fixed_atan2_angle16_compat(sum[1], sum[0]) & 65535);
}
} // namespace thandor
