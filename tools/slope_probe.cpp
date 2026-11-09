#include "thandor/map_document.hpp"
#include "thandor/retail_contact.hpp"
#include <bit>
#include <fstream>
#include <iostream>
using namespace thandor;
int main(int argc, char **argv) try {
  if (argc != 3)
    throw std::runtime_error("usage: slope_probe PCK_ROOT INITIAL.tsv");
  const auto catalog = AssetCatalog::scan_archive_root(argv[1]);
  const auto g = std::find_if(
      catalog.map_groups().begin(), catalog.map_groups().end(),
      [](const auto &g) { return g.level_path == "level/lucas03.lev"; });
  if (g == catalog.map_groups().end())
    throw std::runtime_error("map missing");
  const auto doc = MapDocument::open(catalog, *g);
  std::ifstream input(argv[2]);
  std::array<uint32_t, 41> w{};
  size_t checked = 0, different = 0;
  while (input >> w[0]) {
    for (size_t i = 1; i < 41; ++i)
      if (!(input >> w[i]))
        throw std::runtime_error("bad input");
    if (!w[0] || w[0] > doc.level().placements().size())
      throw std::runtime_error("bad id");
    const auto &p =
        doc.level().placements()[doc.level().placements().size() - w[0]];
    const auto *preset = catalog.army_preset(p.army_asset_id);
    if (!preset)
      continue;
    const auto nodes = resolve_faction_technology_hierarchy(
        *preset, doc.faction_technology_mask(p.faction_index));
    const auto root =
        std::find_if(nodes.begin(), nodes.end(), [](const auto &n) {
          return n.parent_index == UINT32_MAX;
        });
    if (root == nodes.end() || root->simulation.placement_contact_kind != 2)
      continue;
    const auto packed =
        retail_contact_normal_angles(doc.field(), std::bit_cast<int32_t>(w[4]),
                                     std::bit_cast<int32_t>(w[5]));
    if (!packed)
      throw std::runtime_error("invalid contact");
    ++checked;
    const bool mismatch = (*packed & 65535) != (w[7] & 65535) ||
                          (*packed >> 16) != (w[8] & 65535);
    different += mismatch;
    if (mismatch)
      std::cout << "SLOPE_DIFF id=" << w[0] << " arm=" << p.army_asset_id
                << " a0=" << (*packed & 65535) << " n0=" << (w[7] & 65535)
                << " a1=" << (*packed >> 16) << " n1=" << (w[8] & 65535)
                << '\n';
  }
  if (!input.eof() || !checked)
    throw std::runtime_error("no slope evidence");
  std::cout << "V648_SLOPE checked=" << checked
            << " exact=" << checked - different << " mismatches=" << different
            << '\n';
  return different ? 1 : 0;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
