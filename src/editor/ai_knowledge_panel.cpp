#include "ai_knowledge_panel.hpp"

#include "thandor/binary.hpp"

#include "imgui.h"

#include <cstdint>

namespace editor {
namespace {

const thandor::AiKnowledgeParameterSemantic* selected_distance_semantic(
    const AiKnowledgeUiState& state) noexcept {
    const auto* semantic = thandor::find_ai_knowledge_semantic(state.distance_parameter_index);
    return semantic != nullptr && semantic->domain == thandor::AiKnowledgeValueDomain::Q12Distance
        ? semantic : nullptr;
}

void draw_parameter_row(const thandor::AiKnowledgeData& knowledge,
                        std::uint8_t index,
                        const thandor::AiKnowledgeParameterSemantic* semantic) {
    const std::uint32_t raw = knowledge.raw(index);
    const std::int32_t signed_value = static_cast<std::int32_t>(raw);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Text("0x%02X", index);
    ImGui::TableSetColumnIndex(1);
    if (semantic != nullptr) {
        ImGui::TextUnformatted(semantic->label.data(), semantic->label.data() + semantic->label.size());
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%.*s", static_cast<int>(semantic->identifier.size()),
                              semantic->identifier.data());
        }
    } else {
        ImGui::TextDisabled("unresolved");
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::Text("0x%08X", raw);
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(semantic != nullptr
        ? thandor::ai_knowledge_value_domain_name(semantic->domain) : "raw");
    ImGui::TableSetColumnIndex(4);
    if (semantic != nullptr && semantic->domain == thandor::AiKnowledgeValueDomain::Q12Distance) {
        ImGui::Text("%.3f", thandor::q12_to_float(signed_value));
    } else {
        ImGui::Text("%d", signed_value);
    }
}

} // namespace

void draw_ai_knowledge_panel(const std::optional<thandor::AiKnowledgeData>& knowledge,
                             const std::string& load_error,
                             AiKnowledgeUiState& state) {
    ImGui::SeparatorText("Retail KI.DAT diagnostics");
    if (!knowledge) {
        ImGui::TextDisabled("engine/ki.dat unavailable");
        if (!load_error.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", load_error.c_str());
        return;
    }

    const auto* selected = selected_distance_semantic(state);
    if (selected == nullptr) {
        state.distance_parameter_index =
            static_cast<std::uint8_t>(thandor::AiKnowledgeData::kPlacementClearancePaddingIndex);
        selected = selected_distance_semantic(state);
    }

    const char* preview = selected != nullptr ? selected->label.data() : "Q12 distance";
    if (ImGui::BeginCombo("Diagnostic distance", preview)) {
        for (const auto& semantic : thandor::known_ai_knowledge_semantics()) {
            if (semantic.domain != thandor::AiKnowledgeValueDomain::Q12Distance) continue;
            const bool active = semantic.index == state.distance_parameter_index;
            if (ImGui::Selectable(semantic.label.data(), active)) {
                state.distance_parameter_index = semantic.index;
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (selected != nullptr) {
        const std::int32_t value_q12 = knowledge->signed_raw(selected->index);
        ImGui::Text("KI[0x%02X] = %d Q12 = %.3f world units", selected->index, value_q12,
                    thandor::q12_to_float(value_q12));
    }
    ImGui::Checkbox("Show diagnostic distance ring", &state.show_distance_overlay);
    if (state.show_distance_overlay) {
        ImGui::SameLine();
        ImGui::Checkbox("All placements", &state.show_distance_for_all_placements);
    }
    ImGui::TextWrapped(
        "The ring visualizes one proven KI.DAT distance term only. It is not a universal LEV "
        "validity rule. Retail AI placement combines class-specific rules, candidate/runtime "
        "clearance, model radius, terrain and occupancy tests. Manual placement and export are "
        "never blocked by this diagnostic overlay.");

    ImGui::Checkbox("Show 43 proven KI parameters", &state.show_parameter_table);
    ImGui::SameLine();
    ImGui::Checkbox("Include unresolved raw entries", &state.show_all_raw_parameters);
    if (!state.show_parameter_table && !state.show_all_raw_parameters) return;

    if (ImGui::BeginTable("ki-parameters", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0.0F, 260.0F))) {
        ImGui::TableSetupColumn("Index");
        ImGui::TableSetupColumn("Semantic");
        ImGui::TableSetupColumn("Raw");
        ImGui::TableSetupColumn("Domain");
        ImGui::TableSetupColumn("Decoded");
        ImGui::TableHeadersRow();
        if (state.show_all_raw_parameters) {
            for (std::uint16_t index = 0; index < thandor::AiKnowledgeData::kParameterCount; ++index) {
                const auto* semantic = thandor::find_ai_knowledge_semantic(index);
                if (!state.show_parameter_table && semantic != nullptr) continue;
                draw_parameter_row(*knowledge, static_cast<std::uint8_t>(index), semantic);
            }
        } else {
            for (const auto& semantic : thandor::known_ai_knowledge_semantics()) {
                draw_parameter_row(*knowledge, semantic.index, &semantic);
            }
        }
        ImGui::EndTable();
    }
}

} // namespace editor
