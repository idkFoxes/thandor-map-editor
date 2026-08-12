#pragma once

#include "thandor/ai_knowledge.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace editor {

struct AiKnowledgeUiState {
    bool show_distance_overlay{};
    bool show_distance_for_all_placements{};
    bool show_parameter_table{};
    bool show_all_raw_parameters{};
    std::uint8_t distance_parameter_index{
        static_cast<std::uint8_t>(thandor::AiKnowledgeData::kPlacementClearancePaddingIndex)};
};

void draw_ai_knowledge_panel(const std::optional<thandor::AiKnowledgeData>& knowledge,
                             const std::string& load_error,
                             AiKnowledgeUiState& state);

} // namespace editor
