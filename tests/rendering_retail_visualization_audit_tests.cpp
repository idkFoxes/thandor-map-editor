#include "thandor/field.hpp"
#include "thandor/effect.hpp"
#include "thandor/terrain_shadow.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    using thandor::retail_field_marker_audit;

    auto xenite = retail_field_marker_audit(0x00000800U, 0U);
    require(xenite.projected_vertex_visible, "0x0800 cell unexpectedly suppressed by 0x200000 gate");
    require(xenite.selected_resource_support && !xenite.alternate_resource_support,
            "B1/B2 selected resource predicate drifted for 0x0800");
    require(xenite.selected_resource_support_mask == 0x00000800U &&
            xenite.alternate_resource_support_mask == 0x00001000U,
            "resource marker masks drifted for selector 0");

    auto tritium = retail_field_marker_audit(0x00001000U, 1U);
    require(tritium.selected_resource_support && !tritium.alternate_resource_support,
            "B1/B2 selected resource predicate drifted for 0x1000");
    require(tritium.selected_resource_support_mask == 0x00001000U &&
            tritium.alternate_resource_support_mask == 0x00000800U,
            "resource marker masks drifted for selector 1");

    auto both = retail_field_marker_audit(0x00001800U, 0U);
    require(both.selected_resource_support && both.alternate_resource_support,
            "B1/B2 did not expose both resource support bits");

    auto suppressed = retail_field_marker_audit(0x00209800U, 0U);
    require(!suppressed.projected_vertex_visible && !suppressed.selected_resource_support &&
            !suppressed.alternate_resource_support && !suppressed.flag_8000_marker,
            "0x200000 projected-vertex suppression gate drifted");

    auto fluid = retail_field_marker_audit(0x60000000U, 0U);
    require(fluid.fluid_marker_gate && fluid.fluid_receiver_excluded && fluid.fluid_source_excluded,
            "AF/B0 fluid-state predicates drifted");
    auto fluid_suppressed = retail_field_marker_audit(0x64000000U, 0U);
    require(!fluid_suppressed.fluid_marker_gate && !fluid_suppressed.fluid_receiver_excluded &&
            !fluid_suppressed.fluid_source_excluded,
            "0x04000000 fluid-marker suppression gate drifted");

    auto flag8000 = retail_field_marker_audit(0x00008000U, 0U);
    require(flag8000.flag_8000_marker, "0x8000 AF marker predicate drifted");

    require(thandor::retail_ui_state_tint_argb(0x04U) == 0xFFFFFFFFU,
            "UiNode_GetStateTintArgb bit-0x04 state drifted");
    require(thandor::retail_ui_state_tint_argb(0x00U) == 0x00000000U,
            "UiNode_GetStateTintArgb zero state drifted");
    require(thandor::retail_ui_state_tint_argb(0x08U) == 0xFF878787U,
            "UiNode_GetStateTintArgb gray state drifted");
    require(thandor::retail_ui_state_tint_argb(0x18U) == 0x00FFFFFFU,
            "UiNode_GetStateTintArgb transparent-white state drifted");
    require(thandor::retail_modulate_argb_mmx(0x80FFFFFFU, 0x80FF8040U) == 0x40FF8040U,
            "retail PMULHW/PACKUSWB ARGB modulation drifted");

    thandor::EffectDefinitionRecord tint_effect;
    tint_effect.transition_kind = 0U;
    tint_effect.runtime_value_0c = 10U;
    tint_effect.frame_advance_threshold_q4 = 16U;
    tint_effect.alpha_fade_in_ticks = 4U;
    tint_effect.alpha_fade_out_ticks = 2U;
    tint_effect.state_tint_argb = 0x80FF8040U;
    require(thandor::retail_effect_model_tint_argb(tint_effect, 2U, 0x04U) ==
                std::optional<uint32_t>{0x3FFF8040U},
            "retail effect runtime fade x EFF +0x60 tint composition drifted");
    require(thandor::retail_effect_model_tint_argb(tint_effect, 2U, 0x00U) ==
                std::optional<uint32_t>{0x00FFFFFFU},
            "retail effect runtime flag-0x04 visibility gate drifted");
    tint_effect.transition_kind = 4U;
    require(!thandor::retail_effect_model_tint_argb(tint_effect, 2U, 0x04U).has_value(),
            "transition-kind-4 contact alpha must remain on its dedicated runtime path");

    thandor::GeneratedShadowSettings shadow_defaults;
    require(shadow_defaults.texture_dimension == 64U && shadow_defaults.grid_half_size == 32U &&
            shadow_defaults.subresource_count == 16U,
            "generated-shadow defaults no longer match retail persistent-setting fallbacks");
    require(thandor::retail_generated_shadow_settings_valid(shadow_defaults),
            "retail generated-shadow defaults unexpectedly rejected");
    require(thandor::retail_generated_shadow_tiles_per_subresource(shadow_defaults) == 4U,
            "retail default generated-shadow tile count per subresource drifted");
    require(thandor::retail_generated_shadow_tile_capacity(shadow_defaults) == 64U,
            "retail default generated-shadow pass capacity drifted");

    std::cout << "V534_RETAIL_VISUALIZATION_AUDIT PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "V534_RETAIL_VISUALIZATION_AUDIT FAIL: " << error.what() << '\n';
    return 1;
}
