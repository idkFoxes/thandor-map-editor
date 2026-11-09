#include "thandor/simulation_lab_ai_workspaces.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace thandor;

namespace {
struct Candidate {
  std::uint32_t id{};
  std::uint32_t kind{};
  std::int32_t packed_score{};
};
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
} // namespace

int main() try {
  std::vector<Candidate> workspace;
  workspace.reserve(kSimulationLabAiWorkspace13Capacity + 1U);
  for (std::uint32_t id = 0; id < kSimulationLabAiWorkspace13Capacity; ++id)
    require(simulation_lab_ai_candidate_add_or_accumulate(
                workspace, id, 1U, (id + 1U) << 4U),
            "native workspace13 rejected an in-capacity candidate");
  require(workspace.size() == kSimulationLabAiWorkspace13Capacity,
          "workspace13 did not reach its native 128-entry capacity");
  require(!simulation_lab_ai_candidate_add_or_accumulate(
              workspace, 900U, 2U, 0x100U),
          "workspace13 admitted a distinct 129th candidate");
  require(workspace.size() == kSimulationLabAiWorkspace13Capacity,
          "workspace13 size changed after a full-buffer rejection");

  workspace[10U] = {77U, 1U, 0x101U};
  workspace.back() = {77U, 1U, 0x201U};
  const auto old_earlier = workspace[10U].packed_score;
  const auto old_latest = workspace.back().packed_score;
  require(simulation_lab_ai_candidate_add_or_accumulate(
              workspace, 77U, 1U, 0x30U),
          "full workspace13 failed to accumulate an existing candidate");
  require(workspace[10U].packed_score == old_earlier &&
              workspace.back().packed_score ==
                  static_cast<std::int32_t>(
                      static_cast<std::uint32_t>(old_latest) + 0x30U),
          "workspace13 did not search duplicate candidates newest-first");

  require(kSimulationLabAiWorkspace11Capacity == 1024U,
          "workspace11 capacity constant drifted");
  std::cout << "V746 workspace11/13 capacities and reverse accumulation PASS\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
