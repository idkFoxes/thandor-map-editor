#include "thandor/simulation_lab_terminal_residency.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

static void check(bool yes, const char* msg) { if (!yes) throw std::runtime_error(msg); }

int main() {
    using namespace thandor;
    std::vector<ModelVisualNode> hierarchy(4);
    hierarchy[0].model_definition_id=100;
    hierarchy[0].simulation.construction_destruction_effect_id=310;
    hierarchy[0].simulation.terminal_attachment_effect_ids[0]=114;
    hierarchy[0].simulation.terminal_attachment_effect_ids[1]=113;
    hierarchy[0].simulation.terminal_attachment_effect_ids[2]=114;
    hierarchy[1].parent_index=0;
    hierarchy[1].model_definition_id=200;
    hierarchy[1].simulation.terminal_attachment_effect_ids[0]=117;
    hierarchy[2].parent_index=1;
    hierarchy[2].model_definition_id=200; // same-MDL child cannot own another runtime channel
    hierarchy[2].simulation.terminal_attachment_effect_ids[0]=901;
    hierarchy[3].parent_index=0;
    hierarchy[3].model_definition_id=300;
    hierarchy[3].flags=1; // not an instantiated runtime model
    hierarchy[3].simulation.terminal_attachment_effect_ids[0]=902;
    check(simulation_lab_terminal_residency_effect_ids(hierarchy)==
          std::vector<std::uint32_t>({113,114,117}),
          "combat residency must use terminal channels, not construction/dismantle +0x190");
    hierarchy[0].simulation.construction_destruction_effect_id=400;
    check(simulation_lab_terminal_residency_effect_ids(hierarchy)==
          std::vector<std::uint32_t>({113,114,117}),
          "changing the construction effect cannot introduce a combat death effect");
    hierarchy[1].model_definition_id=201; // technology changes the selected runtime-root boundary
    hierarchy[1].simulation.terminal_attachment_effect_ids[0]=118;
    check(simulation_lab_terminal_residency_effect_ids(hierarchy)==
          std::vector<std::uint32_t>({113,114,118,901}),
          "selected definition boundary did not change terminal effect residency");
    return 0;
}
