#include "thandor/fog_preview.hpp"
#include "thandor/retail_contact.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/retail_random.hpp"
#include <cstddef>
#include <iostream>
#include <memory>
#include <random>
#include <thandor/core/math/fixed_transform.h>
#include <thandor/core/math/fixed_trig.h>
#include <thandor/core/math/random.h>
#include <thandor/world/terrain/field_sampling.h>
#include <thandor/world/terrain/occupancy.h>
void Thandor_Ptr32Overflow(uintptr_t) { std::abort(); }
int main() {
  FixedMath_BuildSinCosTables();
  std::mt19937 random(0x648);
  size_t bases = 0, directions = 0, samples = 0;
  for (int i = 0; i < 50000; ++i) {
    const uint32_t a = random() & 65535, b = random() & 65535,
                   c = random() & 65535;
    GraphicsFixedMatrix3x4 m{};
    FixedTransform_BuildRotationBasis(&m, a, b, c);
    const auto actual = thandor::retail_model_basis_q28(a, b, c);
    const int32_t expected[9] = {
        m.basisRow0[0], m.basisRow0[1], m.basisRow0[2],
        m.basisRow1[0], m.basisRow1[1], m.basisRow1[2],
        m.basisRow2[0], m.basisRow2[1], m.basisRow2[2]};
    for (int j = 0; j < 9; ++j)
      if (actual[j] != expected[j]) {
        std::cerr << "basis mismatch " << i << ' ' << j << ' ' << actual[j]
                  << ' ' << expected[j] << '\n';
        return 1;
      }
    ++bases;
    const int32_t weight = int32_t(random() % 16385) - 8192;
    const auto d = FixedMath_DirectionFromAnglesScaled(b, c, weight);
    const auto v = thandor::retail_direction_scaled(b, c, weight);
    if (v != std::array<int32_t, 3>{int32_t(d.x), int32_t(d.y), int32_t(d.z)}) {
      std::cerr << "direction mismatch\n";
      return 2;
    }
    ++directions;
  }
  auto editor = thandor::FieldGrid::create(32, 32, 0, 0);
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      editor.at(x, y).terrain_height_q12 = int32_t(random() % 10000) - 5000;
      if (x == 0 || y == 0 || x == 31 || y == 31)
        editor.at(x, y).flags_and_material |= 0x2000U;
    }
  auto memory = std::make_unique<unsigned char[]>(
      offsetof(FieldGridAsset, cells) + sizeof(FieldGridCell) * 1024);
  auto *f = reinterpret_cast<FieldGridAsset *>(memory.get());
  f->gridWidth = f->gridHeight = 32;
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      const auto &e = editor.at(x, y);
      auto &n = f->cells[y * 32 + x];
      n.worldX = e.world_x_q12;
      n.worldY = e.world_y_q12;
      n.terrainHeight = e.terrain_height_q12;
      n.flagsAndMaterial =
          static_cast<FieldCellPackedFlagsAndMaterial>(e.flags_and_material);
      n.triangle0NormalAngles =
          thandor::retail_placement_normal_angles(editor, x, y, false);
    }
  for (int i = 0; i < 50000; ++i) {
    const int32_t x = int32_t(random() % 90000) - 5000,
                  y = -int32_t(random() % 55000) + 2000;
    Q12 height = 0;
    uint32_t expected = 0;
    const bool valid = FieldGrid_InterpolateTerrainHeightAndNormal(
        y, x, f, &height, &expected);
    const auto actual = thandor::retail_contact_normal_angles(editor, x, y);
    if (valid != actual.has_value() || (valid && *actual != expected)) {
      std::cerr << "normal mismatch " << i << ' ' << x << ' ' << y << ' '
                << expected << ' ' << actual.value_or(0) << '\n';
      return 3;
    }
    ++samples;
  }
  std::array<uint64_t, 1024> occupancy{};
  for (uint32_t y = 0; y < 32; ++y)
    for (uint32_t x = 0; x < 32; ++x) {
      const auto id = y * 32 + x;
      if ((random() % 8) == 0)
        occupancy[id] = uint64_t(std::array<uint8_t, 7>{0, 0x80, 2, 4, 1, 8,
                                                        0x81}[random() % 7])
                        << ((random() % 8) * 8);
      f->cells[id].occupancyMask = occupancy[id];
    }
  for (int i = 0; i < 50000; ++i) {
    const int32_t x = int32_t(random() % 90000) - 5000,
                  y = -int32_t(random() % 55000) + 2000,
                  radius = int32_t(random() % 20000);
    const auto expected =
        TerrainOccupancyMask_ClassifyNeighborhoodAtWorldPoint(radius, y, x, f);
    const auto actual = thandor::retail_model_neighborhood_mask(
        editor, occupancy, x, y, radius);
    if (expected != actual) {
      std::cerr << "occupancy mismatch " << i << ' ' << expected << ' '
                << actual << '\n';
      return 4;
    }
    const uint32_t history = random();
    const bool forgotten = random() & 1;
    const auto native = TerrainOccupancyMask_ResolveRuntimeClassFlags(
        static_cast<ModelRuntimeFlags>(forgotten ? 0x10U : 0U), history,
        expected, 1);
    const auto resolved =
        thandor::retail_resolve_model_occupancy(actual, history, forgotten);
    if (native.primaryOccupancyMask != resolved.primary ||
        native.secondaryOccupancyMask != resolved.history) {
      std::cerr << "history mismatch\n";
      return 5;
    }
  }
  for (uint32_t seed : {0U, 1U, 12345U, 1817130253U, 2638864529U, 0xFFFFFFFFU}) {
    g_RandomGeneratorState.primarySeed = seed;
    uint32_t editor_seed = seed;
    for (uint32_t draw = 0; draw < 200000; ++draw) {
      const auto native = Random_NextPrimary();
      const auto editor = thandor::retail_random_next(editor_seed);
      if (native != editor ||
          g_RandomGeneratorState.primarySeed != editor_seed) {
        std::cerr << "native RNG mismatch\n";
        return 6;
      }
    }
  }
  std::cout << "V648_NATIVE_MATH PASS exact_bases=" << bases
            << " exact_directions=" << directions
            << " native_sampling_cases=" << samples
            << " occupancy_and_history_cases=50000 native_rng_draws=1200000"
            << "; compiled supplied game math and field sampling without body "
               "changes\n";
}
