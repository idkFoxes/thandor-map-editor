cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/thandor/editor_version.hpp"
    THANDOR_EDITOR_VERSION_LINE REGEX "kEditorVersion =")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" THANDOR_EDITOR_VERSION "${THANDOR_EDITOR_VERSION_LINE}")
project(ThandorMapEditor VERSION ${THANDOR_EDITOR_VERSION} LANGUAGES CXX)

option(THANDOR_EDITOR_BUILD_APP "Build the Vulkan/ImGui editor" ON)
option(THANDOR_EDITOR_BUILD_TESTS "Build core format tests" ON)
set(THANDOR_TEST_ARCHIVE_ROOT "" CACHE PATH "Optional original PCK directory for stock shadow tests; no game executable required")
option(THANDOR_ENFORCE_20HZ_BUDGET "Enforce machine-dependent 50 ms worker benchmark limit" OFF)
option(THANDOR_EDITOR_BUILD_PROBES "Include stock investigation probes in the default build" OFF)
option(THANDOR_EDITOR_COMPILER_CACHE "Use ccache or sccache when available" ON)
if(THANDOR_EDITOR_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(THANDOR_COMPILER_CACHE NAMES ccache sccache)
    if(THANDOR_COMPILER_CACHE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${THANDOR_COMPILER_CACHE}")
    endif()
endif()
option(THANDOR_EDITOR_FETCH_DEPS "Fetch GLFW and Dear ImGui when not supplied" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)

add_library(thandor_map_core
    src/core/binary.cpp
    src/core/campaign.cpp
    src/core/campaign_document.cpp
    src/core/audio.cpp
    src/core/simulation_lab_audio.cpp
    src/core/ai_knowledge.cpp
    src/core/pck.cpp
    src/core/field.cpp
    src/core/terrain_generator.cpp
    src/core/terrain_generation_quality.cpp
    src/core/new_map_terrain.cpp
    src/core/field_raycast.cpp
    src/core/fog_preview.cpp
    src/core/path_diagnostics.cpp
    src/core/v528_pathing.cpp
    src/core/level.cpp
    src/core/scenario_catalog.cpp
    src/core/text_resource.cpp
    src/core/technology.cpp
    src/core/gfx.cpp
    src/core/frontend_player_texture.cpp
    src/core/terrain_texture.cpp
    src/core/terrain_shadow.cpp
    src/core/model.cpp
    src/core/effect.cpp
    src/core/simulation_projectile_contacts.cpp
    src/core/simulation_projectile_runtime.cpp
    src/core/simulation.cpp
    src/core/simulation_lab_environment.cpp
    src/core/simulation_lab_shot_rays.cpp
    src/core/retail_terrain_scratch.cpp
    src/core/retail_grid_influence.cpp
    src/core/retail_placement.cpp
    src/core/retail_contact.cpp
    src/core/simulation_lab_site_placement.cpp
    src/core/simulation_lab_runtime.cpp
    src/core/simulation_lab_catalog.cpp
    src/core/simulation_lab.cpp
    src/core/simulation_lab_relations.cpp
    src/core/simulation_lab_airport.cpp
    src/core/simulation_lab_targeting.cpp
    src/core/simulation_lab_follow.cpp
    src/core/simulation_lab_worker.cpp
    src/core/catalog.cpp
    src/core/map_document.cpp
)
target_include_directories(thandor_map_core PUBLIC include)
target_link_libraries(thandor_map_core PUBLIC Threads::Threads)
add_executable(stock_navigation_grid_audit EXCLUDE_FROM_ALL tools/stock_navigation_grid_audit.cpp)
target_link_libraries(stock_navigation_grid_audit PRIVATE thandor_map_core)
add_executable(stock_movement_parity_probe EXCLUDE_FROM_ALL tools/stock_movement_parity_probe.cpp)
target_link_libraries(stock_movement_parity_probe PRIVATE thandor_map_core)
add_executable(thandor_campaign_inventory EXCLUDE_FROM_ALL tools/campaign_inventory.cpp)
target_link_libraries(thandor_campaign_inventory PRIVATE thandor_map_core)
add_executable(thandor_v638_stock_research_acquisition_probe tools/stock_research_acquisition_probe.cpp)
target_link_libraries(thandor_v638_stock_research_acquisition_probe PRIVATE thandor_map_core)
add_executable(thandor_v637_stock_timed_effect_census tools/stock_timed_effect_census.cpp)
target_link_libraries(thandor_v637_stock_timed_effect_census PRIVATE thandor_map_core)
add_executable(thandor_v637_stock_research_progression_probe tools/stock_research_progression_probe.cpp)
target_link_libraries(thandor_v637_stock_research_progression_probe PRIVATE thandor_map_core)
add_executable(thandor_v636_stock_research_audit tools/stock_research_audit.cpp)
target_link_libraries(thandor_v636_stock_research_audit PRIVATE thandor_map_core)
add_executable(thandor_v635_stock_turret_research_probe tools/stock_turret_research_probe.cpp)
target_link_libraries(thandor_v635_stock_turret_research_probe PRIVATE thandor_map_core)
add_executable(thandor_v634_stock_walker_turret_probe tools/stock_walker_turret_probe.cpp)
target_link_libraries(thandor_v634_stock_walker_turret_probe PRIVATE thandor_map_core)
add_executable(thandor_simlab_stock_replay tools/simlab_stock_replay.cpp)
target_link_libraries(thandor_simlab_stock_replay PRIVATE thandor_map_core)
add_executable(thandor_v620_stock_ground_combat_probe tools/stock_ground_combat_probe.cpp)
target_link_libraries(thandor_v620_stock_ground_combat_probe PRIVATE thandor_map_core)
add_executable(thandor_v620_stock_smoke_probe tools/stock_smoke_probe.cpp)
target_link_libraries(thandor_v620_stock_smoke_probe PRIVATE thandor_map_core)
add_executable(thandor_v619_stock_factory_probe tools/stock_factory_power_off_probe.cpp)
target_link_libraries(thandor_v619_stock_factory_probe PRIVATE thandor_map_core)
add_executable(thandor_v619_stock_base_probe tools/stock_base_walker_progression_probe.cpp)
target_link_libraries(thandor_v619_stock_base_probe PRIVATE thandor_map_core)
add_executable(thandor_v618_stock_base_probe tools/stock_base_entity_census.cpp)
target_link_libraries(thandor_v618_stock_base_probe PRIVATE thandor_map_core)
add_executable(thandor_v618_stock_combat_probe tools/stock_combat_probe.cpp)
target_link_libraries(thandor_v618_stock_combat_probe PRIVATE thandor_map_core)
add_executable(thandor_v617_stock_base_probe tools/stock_base_startup_probe.cpp)
target_link_libraries(thandor_v617_stock_base_probe PRIVATE thandor_map_core)
add_executable(thandor_v617_stock_factory_probe tools/stock_advanced_factory_queue_probe.cpp)
target_link_libraries(thandor_v617_stock_factory_probe PRIVATE thandor_map_core)
if(WIN32)
    target_link_libraries(thandor_map_core PRIVATE winmm)
elseif(UNIX)
    target_link_libraries(thandor_map_core PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MSVC)
    target_compile_options(thandor_map_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(thandor_map_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(thandor_v643_stock_audio_probe tools/stock_audio_probe.cpp)
target_link_libraries(thandor_v643_stock_audio_probe PRIVATE thandor_map_core)
add_executable(thandor_v645_stock_animation_audit tools/stock_animation_audit.cpp)
target_link_libraries(thandor_v645_stock_animation_audit PRIVATE thandor_map_core)
add_executable(thandor_v645_native_map_match tools/native_map_match.cpp)
target_link_libraries(thandor_v645_native_map_match PRIVATE thandor_map_core)
add_executable(thandor_v645_stock_sound_census tools/stock_sound_census.cpp)
target_link_libraries(thandor_v645_stock_sound_census PRIVATE thandor_map_core)
add_executable(thandor_v642_stock_pursuit_probe tools/stock_pursuit_trace.cpp)
target_link_libraries(thandor_v642_stock_pursuit_probe PRIVATE thandor_map_core)
add_executable(thandor_v641_stock_pursuit_probe tools/stock_weapon_range_pursuit_probe.cpp)
target_link_libraries(thandor_v641_stock_pursuit_probe PRIVATE thandor_map_core)
add_executable(thandor_v640_advanced_milestone_probe tools/advanced_milestone_probe.cpp)
target_link_libraries(thandor_v640_advanced_milestone_probe PRIVATE thandor_map_core)
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_retained_visibility_metrics_tests tests/ai_retained_visibility_metrics_tests.cpp)
    target_link_libraries(ai_retained_visibility_metrics_tests PRIVATE thandor_map_core)
    add_test(NAME ai_retained_visibility_metrics_tests COMMAND ai_retained_visibility_metrics_tests)
    if(UNIX AND NOT APPLE)
        add_test(NAME editor_movement_preview_execution_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_movement_preview_execution_tests.py
                --source ${CMAKE_CURRENT_SOURCE_DIR} --build ${CMAKE_BINARY_DIR})
    endif()
    add_executable(movement_native_grid_tests tests/movement_native_grid_tests.cpp)
    target_link_libraries(movement_native_grid_tests PRIVATE thandor_map_core)
    add_test(NAME movement_native_grid_tests COMMAND movement_native_grid_tests)
    add_executable(new_map_terrain_tests tests/new_map_terrain_tests.cpp)
    target_link_libraries(new_map_terrain_tests PRIVATE thandor_map_core)
    add_test(NAME new_map_terrain_tests COMMAND new_map_terrain_tests)
    add_executable(terrain_generator_tests tests/terrain_generator_tests.cpp)
    target_link_libraries(terrain_generator_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_generator_tests COMMAND terrain_generator_tests)
    add_executable(terrain_generation_quality_tests tests/terrain_generation_quality_tests.cpp)
    target_link_libraries(terrain_generation_quality_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_generation_quality_tests COMMAND terrain_generation_quality_tests)
    add_executable(campaign_document_tests tests/campaign_document_tests.cpp)
    target_link_libraries(campaign_document_tests PRIVATE thandor_map_core)
    add_test(NAME campaign_document_tests COMMAND campaign_document_tests)
    add_executable(scenario_campaign_tests tests/scenario_campaign_tests.cpp)
    target_link_libraries(scenario_campaign_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_campaign_tests COMMAND scenario_campaign_tests)
    add_executable(editor_ui_scale_tests tests/editor_ui_scale_tests.cpp)
    target_include_directories(editor_ui_scale_tests PRIVATE include src/editor)
    add_test(NAME editor_ui_scale_tests COMMAND editor_ui_scale_tests)
    add_test(NAME recording_inferred_inputs_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_inferred_inputs_tests.py)
    add_executable(rendering_owner_callback_order_tests tests/rendering_owner_callback_order_tests.cpp)
    target_link_libraries(rendering_owner_callback_order_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_owner_callback_order_tests COMMAND rendering_owner_callback_order_tests)
    add_executable(rendering_texture_dependencies_tests tests/rendering_texture_dependencies_tests.cpp)
    target_link_libraries(rendering_texture_dependencies_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_texture_dependencies_tests COMMAND rendering_texture_dependencies_tests)
    add_test(NAME build_incremental_scripts_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_incremental_scripts_tests.py)
    add_executable(assets_archive_root_tests tests/assets_archive_root_tests.cpp)
    target_link_libraries(assets_archive_root_tests PRIVATE thandor_map_core)
    add_test(NAME assets_archive_root_tests COMMAND assets_archive_root_tests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(editor_appimage_paths_tests tests/editor_appimage_paths_tests.cpp)
        target_include_directories(editor_appimage_paths_tests PRIVATE src/editor)
        add_test(NAME editor_appimage_paths_tests COMMAND editor_appimage_paths_tests)
    endif()
    add_test(NAME recording_human_world_constraints_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_human_world_constraints_tests.py)
    add_executable(recording_local_command_capture_tests tests/recording_local_command_capture_tests.cpp)
    target_link_libraries(recording_local_command_capture_tests PRIVATE thandor_map_core)
    add_test(NAME recording_local_command_capture_tests COMMAND recording_local_command_capture_tests)
    add_test(NAME recording_native_recording_parser_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_native_recording_parser_tests.py)
    add_executable(core_parity_regression_tests tests/core_parity_regression_tests.cpp)
    target_link_libraries(core_parity_regression_tests PRIVATE thandor_map_core)
    add_test(NAME core_parity_regression_tests COMMAND core_parity_regression_tests)
    add_executable(audio_simulation_audio_tests tests/audio_simulation_audio_tests.cpp)
    target_link_libraries(audio_simulation_audio_tests PRIVATE thandor_map_core)
    add_test(NAME audio_simulation_audio_tests COMMAND audio_simulation_audio_tests)
    add_executable(animation_walker_support_heading_tests tests/animation_walker_support_heading_tests.cpp)
    target_link_libraries(animation_walker_support_heading_tests PRIVATE thandor_map_core)
    add_test(NAME animation_walker_support_heading_tests COMMAND animation_walker_support_heading_tests)
    add_executable(animation_articulated_stride_tests tests/animation_articulated_stride_tests.cpp)
    target_link_libraries(animation_articulated_stride_tests PRIVATE thandor_map_core)
    add_test(NAME animation_articulated_stride_tests COMMAND animation_articulated_stride_tests)
    add_executable(movement_command_ownership_tests tests/movement_command_ownership_tests.cpp)
    target_link_libraries(movement_command_ownership_tests PRIVATE thandor_map_core)
    add_test(NAME movement_command_ownership_tests COMMAND movement_command_ownership_tests)
    add_executable(movement_ground_heading_gate_tests tests/movement_ground_heading_gate_tests.cpp)
    target_link_libraries(movement_ground_heading_gate_tests PRIVATE thandor_map_core)
    add_test(NAME movement_ground_heading_gate_tests COMMAND movement_ground_heading_gate_tests ${THANDOR_STOCK_ASSET_ROOT})
    add_executable(movement_ground_speed_tests tests/movement_ground_speed_tests.cpp)
    target_link_libraries(movement_ground_speed_tests PRIVATE thandor_map_core)
    add_test(NAME movement_ground_speed_tests COMMAND movement_ground_speed_tests ${THANDOR_STOCK_ASSET_ROOT})
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME editor_version_cli_tests COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_version_cli_tests.py
            --source ${CMAKE_CURRENT_SOURCE_DIR} --compiler ${CMAKE_CXX_COMPILER})
    endif()
    add_executable(placement_player_commands_tests tests/placement_player_commands_tests.cpp)
    target_link_libraries(placement_player_commands_tests PRIVATE thandor_map_core)
    add_test(NAME placement_player_commands_tests COMMAND placement_player_commands_tests)
    add_executable(movement_target_follow_tests tests/movement_target_follow_tests.cpp)
    target_link_libraries(movement_target_follow_tests PRIVATE thandor_map_core)
    add_test(NAME movement_target_follow_tests COMMAND movement_target_follow_tests)
    add_executable(combat_command_tests tests/combat_command_tests.cpp)
    target_link_libraries(combat_command_tests PRIVATE thandor_map_core)
    add_test(NAME combat_command_tests COMMAND combat_command_tests)
    add_executable(ai_planning_order_release_tests tests/ai_planning_order_release_tests.cpp)
    target_link_libraries(ai_planning_order_release_tests PRIVATE thandor_map_core)
    add_test(NAME ai_planning_order_release_tests COMMAND ai_planning_order_release_tests)
    add_executable(thandor_v639_stock_acquisition_trace tools/stock_acquisition_trace.cpp)
    target_link_libraries(thandor_v639_stock_acquisition_trace PRIVATE thandor_map_core)
    add_executable(research_catalog_cache_tests tests/research_catalog_cache_tests.cpp)
    target_link_libraries(research_catalog_cache_tests PRIVATE thandor_map_core)
    add_test(NAME research_catalog_cache_tests COMMAND research_catalog_cache_tests)
    add_executable(ai_purchase_decision_trace_tests tests/ai_purchase_decision_trace_tests.cpp)
    target_link_libraries(ai_purchase_decision_trace_tests PRIVATE thandor_map_core)
    add_test(NAME ai_purchase_decision_trace_tests COMMAND ai_purchase_decision_trace_tests)
    add_executable(research_product_metadata_tests tests/research_product_metadata_tests.cpp)
    target_link_libraries(research_product_metadata_tests PRIVATE thandor_map_core)
    add_test(NAME research_product_metadata_tests COMMAND research_product_metadata_tests)
    add_executable(research_owner_order_tests tests/research_owner_order_tests.cpp)
    target_link_libraries(research_owner_order_tests PRIVATE thandor_map_core)
    add_test(NAME research_owner_order_tests COMMAND research_owner_order_tests)
    add_executable(research_pipeline_tests tests/research_pipeline_tests.cpp)
    target_link_libraries(research_pipeline_tests PRIVATE thandor_map_core)
    add_test(NAME research_pipeline_tests COMMAND research_pipeline_tests)
    add_executable(research_unlock_tests tests/research_unlock_tests.cpp)
    target_link_libraries(research_unlock_tests PRIVATE thandor_map_core)
    add_test(NAME research_unlock_tests COMMAND research_unlock_tests)
    add_executable(combat_zero_shot_tests tests/combat_zero_shot_tests.cpp)
    target_link_libraries(combat_zero_shot_tests PRIVATE thandor_map_core)
    add_test(NAME combat_zero_shot_tests COMMAND combat_zero_shot_tests)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME build_linux_parallel_tests COMMAND bash
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/build_linux_parallel_tests.sh")
    endif()
    add_executable(editor_user_settings_tests tests/editor_user_settings_tests.cpp)
    target_include_directories(editor_user_settings_tests PRIVATE src/editor include)
    add_test(NAME editor_user_settings_tests COMMAND editor_user_settings_tests)
    add_executable(editor_performance_log_tests tests/editor_performance_log_tests.cpp)
    target_link_libraries(editor_performance_log_tests PRIVATE thandor_map_core)
    target_include_directories(editor_performance_log_tests PRIVATE src/editor)
    add_test(NAME editor_performance_log_tests COMMAND editor_performance_log_tests)
    add_executable(simlab_async_diagnostics_tests tests/simlab_async_diagnostics_tests.cpp)
    target_link_libraries(simlab_async_diagnostics_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_async_diagnostics_tests COMMAND simlab_async_diagnostics_tests)
    add_executable(movement_walker_contacts_tests tests/movement_walker_contacts_tests.cpp)
    target_link_libraries(movement_walker_contacts_tests PRIVATE thandor_map_core)
    add_test(NAME movement_walker_contacts_tests COMMAND movement_walker_contacts_tests)

    add_executable(production_effect_tests tests/production_effect_tests.cpp)
    target_link_libraries(production_effect_tests PRIVATE thandor_map_core)
    add_test(NAME production_effect_tests COMMAND production_effect_tests)
    add_executable(combat_live_aim_attachment_tests tests/combat_live_aim_attachment_tests.cpp)
    target_link_libraries(combat_live_aim_attachment_tests PRIVATE thandor_map_core)
    add_test(NAME combat_live_aim_attachment_tests COMMAND combat_live_aim_attachment_tests)
    add_test(NAME combat_live_aim_attachment_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/combat_live_aim_attachment_source_contract_tests.py)

    add_executable(combat_terminal_child_detachment_tests tests/combat_terminal_child_detachment_tests.cpp)
    target_link_libraries(combat_terminal_child_detachment_tests PRIVATE thandor_map_core)
    add_test(NAME combat_terminal_child_detachment_tests COMMAND combat_terminal_child_detachment_tests)
    add_executable(combat_moving_target_lead_tests tests/combat_moving_target_lead_tests.cpp)
    target_link_libraries(combat_moving_target_lead_tests PRIVATE thandor_map_core)
    add_test(NAME combat_moving_target_lead_tests COMMAND combat_moving_target_lead_tests)
    add_executable(combat_projectile_geometry_tests tests/combat_projectile_geometry_tests.cpp)
    target_link_libraries(combat_projectile_geometry_tests PRIVATE thandor_map_core)
    add_test(NAME combat_projectile_geometry_tests COMMAND combat_projectile_geometry_tests)
    add_executable(rendering_render_assets_tests tests/rendering_render_assets_tests.cpp)
    target_link_libraries(rendering_render_assets_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_render_assets_tests COMMAND rendering_render_assets_tests)
    add_executable(thandor_v621_stock_beam_probe tools/stock_beam_probe.cpp)
    target_link_libraries(thandor_v621_stock_beam_probe PRIVATE thandor_map_core)
    add_executable(combat_beam_damage_tests tests/combat_beam_damage_tests.cpp)
    target_link_libraries(combat_beam_damage_tests PRIVATE thandor_map_core)
    add_test(NAME combat_beam_damage_tests COMMAND combat_beam_damage_tests)
    add_executable(combat_sprite_triangle_contacts_tests tests/combat_sprite_triangle_contacts_tests.cpp)
    target_link_libraries(combat_sprite_triangle_contacts_tests PRIVATE thandor_map_core)
    add_test(NAME combat_sprite_triangle_contacts_tests COMMAND combat_sprite_triangle_contacts_tests)
    add_executable(combat_beam_contacts_tests tests/combat_beam_contacts_tests.cpp)
    target_link_libraries(combat_beam_contacts_tests PRIVATE thandor_map_core)
    add_test(NAME combat_beam_contacts_tests COMMAND combat_beam_contacts_tests)
    add_executable(combat_line_of_fire_tests tests/combat_line_of_fire_tests.cpp)
    target_link_libraries(combat_line_of_fire_tests PRIVATE thandor_map_core)
    add_test(NAME combat_line_of_fire_tests COMMAND combat_line_of_fire_tests)
    add_executable(movement_actual_route_tests tests/movement_actual_route_tests.cpp)
    target_link_libraries(movement_actual_route_tests PRIVATE thandor_map_core)
    add_test(NAME movement_actual_route_tests COMMAND movement_actual_route_tests)
    add_executable(production_factory_door_power_tests tests/production_factory_door_power_tests.cpp)
    target_link_libraries(production_factory_door_power_tests PRIVATE thandor_map_core)
    add_test(NAME production_factory_door_power_tests COMMAND production_factory_door_power_tests)
    add_executable(combat_targeting_tests tests/combat_targeting_tests.cpp)
    target_link_libraries(combat_targeting_tests PRIVATE thandor_map_core)
    add_test(NAME combat_targeting_tests COMMAND combat_targeting_tests)
    add_executable(combat_repair_targeting_tests tests/combat_repair_targeting_tests.cpp)
    target_link_libraries(combat_repair_targeting_tests PRIVATE thandor_map_core)
    add_test(NAME combat_repair_targeting_tests COMMAND combat_repair_targeting_tests)
    set_tests_properties(combat_repair_targeting_tests PROPERTIES LABELS "combat")
    add_executable(combat_repair_projectile_contact_tests tests/combat_repair_projectile_contact_tests.cpp)
    target_link_libraries(combat_repair_projectile_contact_tests PRIVATE thandor_map_core)
    add_test(NAME combat_repair_projectile_contact_tests COMMAND combat_repair_projectile_contact_tests)
    set_tests_properties(combat_repair_projectile_contact_tests PROPERTIES LABELS "combat")
    add_executable(combat_weapon_power_tests tests/combat_weapon_power_tests.cpp)
    target_link_libraries(combat_weapon_power_tests PRIVATE thandor_map_core)
    add_test(NAME combat_weapon_power_tests COMMAND combat_weapon_power_tests)
    add_executable(combat_target_release_tests tests/combat_target_release_tests.cpp)
    target_link_libraries(combat_target_release_tests PRIVATE thandor_map_core)
    add_test(NAME combat_target_release_tests COMMAND combat_target_release_tests)
    add_executable(research_energy_lifecycle_tests tests/research_energy_lifecycle_tests.cpp)
    target_link_libraries(research_energy_lifecycle_tests PRIVATE thandor_map_core)
    add_test(NAME research_energy_lifecycle_tests COMMAND research_energy_lifecycle_tests)
    add_executable(research_producer_research_exclusion_tests tests/research_producer_research_exclusion_tests.cpp)
    target_link_libraries(research_producer_research_exclusion_tests PRIVATE thandor_map_core)
    add_test(NAME research_producer_research_exclusion_tests COMMAND research_producer_research_exclusion_tests)
    add_executable(collision_owner_order_tests tests/collision_owner_order_tests.cpp)
    target_link_libraries(collision_owner_order_tests PRIVATE thandor_map_core)
    add_test(NAME collision_owner_order_tests COMMAND collision_owner_order_tests)
    add_executable(army_pool_lifecycle_tests tests/army_pool_lifecycle_tests.cpp)
    target_link_libraries(army_pool_lifecycle_tests PRIVATE thandor_map_core)
    add_test(NAME army_pool_lifecycle_tests COMMAND army_pool_lifecycle_tests)
    add_executable(combat_environment_projectile_tests tests/combat_environment_projectile_tests.cpp)
    target_link_libraries(combat_environment_projectile_tests PRIVATE thandor_map_core)
    add_test(NAME combat_environment_projectile_tests COMMAND combat_environment_projectile_tests)
    add_executable(placement_live_site_placement_tests tests/placement_live_site_placement_tests.cpp)
    target_link_libraries(placement_live_site_placement_tests PRIVATE thandor_map_core)
    add_test(NAME placement_live_site_placement_tests COMMAND placement_live_site_placement_tests)

    add_executable(production_destruction_production_tests tests/production_destruction_production_tests.cpp)
    target_link_libraries(production_destruction_production_tests PRIVATE thandor_map_core)
    add_test(NAME production_destruction_production_tests COMMAND production_destruction_production_tests)

    add_executable(ai_timer_perception_tests tests/ai_timer_perception_tests.cpp)
    target_link_libraries(ai_timer_perception_tests PRIVATE thandor_map_core)
    add_test(NAME ai_timer_perception_tests COMMAND ai_timer_perception_tests)
    add_executable(editor_selection_command_controls_tests tests/editor_selection_command_controls_tests.cpp)
    target_link_libraries(editor_selection_command_controls_tests PRIVATE thandor_map_core)
    target_include_directories(editor_selection_command_controls_tests PRIVATE src/editor)
    add_test(NAME editor_selection_command_controls_tests COMMAND editor_selection_command_controls_tests)
    add_executable(production_factory_routing_worker_tests tests/production_factory_routing_worker_tests.cpp)
    target_link_libraries(production_factory_routing_worker_tests PRIVATE thandor_map_core)
    add_test(NAME production_factory_routing_worker_tests COMMAND production_factory_routing_worker_tests)
    add_executable(production_walker_rally_completion_tests tests/production_walker_rally_completion_tests.cpp)
    target_link_libraries(production_walker_rally_completion_tests PRIVATE thandor_map_core)
    if(THANDOR_STOCK_ASSET_ROOT)
        add_test(NAME production_walker_rally_completion_stock_tests COMMAND production_walker_rally_completion_tests ${THANDOR_STOCK_ASSET_ROOT})
    endif()
    add_executable(combat_storage_weapon_progression_tests tests/combat_storage_weapon_progression_tests.cpp)
    target_link_libraries(combat_storage_weapon_progression_tests PRIVATE thandor_map_core)
    add_test(NAME combat_storage_weapon_progression_tests COMMAND combat_storage_weapon_progression_tests)
    add_executable(editor_ui_cadence_tests tests/editor_ui_cadence_tests.cpp)
    target_include_directories(editor_ui_cadence_tests PRIVATE src/editor)
    add_test(NAME editor_ui_cadence_tests COMMAND editor_ui_cadence_tests)
    add_test(NAME editor_ui_throttle_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_ui_throttle_source_contract_tests.py)
    add_executable(editor_workspace_viewport_tests tests/editor_workspace_viewport_tests.cpp)
    target_include_directories(editor_workspace_viewport_tests PRIVATE src/editor)
    target_link_libraries(editor_workspace_viewport_tests PRIVATE thandor_map_core)
    add_test(NAME editor_workspace_viewport_tests COMMAND editor_workspace_viewport_tests)
    add_executable(research_technology_runtime_tests tests/research_technology_runtime_tests.cpp)
    target_link_libraries(research_technology_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME research_technology_runtime_tests COMMAND research_technology_runtime_tests)
    add_executable(assets_retail_unit_family_tests tests/assets_retail_unit_family_tests.cpp)
    target_link_libraries(assets_retail_unit_family_tests PRIVATE thandor_map_core)
    add_test(NAME assets_retail_unit_family_tests COMMAND assets_retail_unit_family_tests)
    add_executable(ai_purchase_affordability_tests tests/ai_purchase_affordability_tests.cpp)
    target_include_directories(ai_purchase_affordability_tests PRIVATE include)
    add_test(NAME ai_purchase_affordability_tests COMMAND ai_purchase_affordability_tests)
    add_executable(production_strategic_factory_terrain_tests tests/production_strategic_factory_terrain_tests.cpp)
    target_include_directories(production_strategic_factory_terrain_tests PRIVATE include)
    add_test(NAME production_strategic_factory_terrain_tests COMMAND production_strategic_factory_terrain_tests)
    add_executable(movement_walker_collision_recovery_tests tests/movement_walker_collision_recovery_tests.cpp)
    target_link_libraries(movement_walker_collision_recovery_tests PRIVATE thandor_map_core)
    add_test(NAME movement_walker_collision_recovery_tests COMMAND movement_walker_collision_recovery_tests)
    add_executable(movement_walker_route_heading_tests tests/movement_walker_route_heading_tests.cpp)
    target_link_libraries(movement_walker_route_heading_tests PRIVATE thandor_map_core)
    add_test(NAME movement_walker_route_heading_tests COMMAND movement_walker_route_heading_tests)

    add_executable(movement_obstructed_destination_tests tests/movement_obstructed_destination_tests.cpp)
    target_link_libraries(movement_obstructed_destination_tests PRIVATE thandor_map_core)
    add_test(NAME movement_obstructed_destination_tests COMMAND movement_obstructed_destination_tests)

    add_executable(object_texture_source_reuse_tests tests/object_texture_source_reuse_tests.cpp)
    target_include_directories(object_texture_source_reuse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    target_link_libraries(object_texture_source_reuse_tests PRIVATE thandor_map_core)
    add_test(NAME object_texture_source_reuse_tests COMMAND object_texture_source_reuse_tests)

    add_executable(sprite_canvas_cache_tests tests/sprite_canvas_cache_tests.cpp)
    target_include_directories(sprite_canvas_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    target_link_libraries(sprite_canvas_cache_tests PRIVATE thandor_map_core)
    add_test(NAME sprite_canvas_cache_tests COMMAND sprite_canvas_cache_tests)

    add_executable(rendering_selected_simulation_residency_tests tests/rendering_selected_simulation_residency_tests.cpp)
    target_include_directories(rendering_selected_simulation_residency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    add_test(NAME rendering_selected_simulation_residency_tests COMMAND rendering_selected_simulation_residency_tests)
    add_test(NAME rendering_v695_runtime_residency_navbar_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_v695_runtime_residency_navbar_source_contract_tests.py
                     ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(simlab_product_cache_tests tests/simlab_product_cache_tests.cpp)
    target_include_directories(simlab_product_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    target_link_libraries(simlab_product_cache_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_product_cache_tests COMMAND simlab_product_cache_tests)
    add_executable(simlab_runtime_catalog_cache_tests tests/simlab_runtime_catalog_cache_tests.cpp)
    target_include_directories(simlab_runtime_catalog_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    target_link_libraries(simlab_runtime_catalog_cache_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_runtime_catalog_cache_tests COMMAND simlab_runtime_catalog_cache_tests)

    add_executable(rendering_shadow_receiver_compaction_tests tests/rendering_shadow_receiver_compaction_tests.cpp)
    target_include_directories(rendering_shadow_receiver_compaction_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    add_test(NAME rendering_shadow_receiver_compaction_tests COMMAND rendering_shadow_receiver_compaction_tests)
    add_executable(rendering_shadow_receiver_surface_guard_tests tests/rendering_shadow_receiver_surface_guard_tests.cpp)
    target_include_directories(rendering_shadow_receiver_surface_guard_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rendering_shadow_receiver_surface_guard_tests COMMAND rendering_shadow_receiver_surface_guard_tests)
    set_tests_properties(rendering_shadow_receiver_surface_guard_tests PROPERTIES LABELS "rendering")
    add_test(NAME rendering_shadow_receiver_surface_contract_tests COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_shadow_receiver_surface_contract_tests.py)
    set_tests_properties(rendering_shadow_receiver_surface_contract_tests PROPERTIES LABELS "rendering")
    add_executable(rendering_shadow_receiver_pose_cache_tests tests/rendering_shadow_receiver_pose_cache_tests.cpp)
    target_include_directories(rendering_shadow_receiver_pose_cache_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rendering_shadow_receiver_pose_cache_tests COMMAND rendering_shadow_receiver_pose_cache_tests)
    set_tests_properties(rendering_shadow_receiver_pose_cache_tests PROPERTIES LABELS "rendering")
    add_executable(rendering_generated_shadow_reuse_tests tests/rendering_generated_shadow_reuse_tests.cpp)
    target_include_directories(rendering_generated_shadow_reuse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/editor)
    target_link_libraries(rendering_generated_shadow_reuse_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_generated_shadow_reuse_tests COMMAND rendering_generated_shadow_reuse_tests)

    add_executable(rendering_object_texture_stable_slots_tests tests/rendering_object_texture_stable_slots_tests.cpp)
    target_link_libraries(rendering_object_texture_stable_slots_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_object_texture_stable_slots_tests COMMAND rendering_object_texture_stable_slots_tests)
    add_executable(rendering_frame_versioned_atlas_tests tests/rendering_frame_versioned_atlas_tests.cpp)
    target_link_libraries(rendering_frame_versioned_atlas_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_frame_versioned_atlas_tests COMMAND rendering_frame_versioned_atlas_tests)
    add_executable(rendering_object_texture_updates_tests tests/rendering_object_texture_updates_tests.cpp)
    target_link_libraries(rendering_object_texture_updates_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_object_texture_updates_tests COMMAND rendering_object_texture_updates_tests)
    add_executable(rendering_runtime_shadow_tests tests/rendering_runtime_shadow_tests.cpp)
    add_executable(combat_terrain_damage_tests tests/combat_terrain_damage_tests.cpp)
    add_executable(combat_map_boundary_effect_tests tests/combat_map_boundary_effect_tests.cpp)
    target_link_libraries(combat_map_boundary_effect_tests PRIVATE thandor_map_core)
    add_test(NAME combat_map_boundary_effect_tests COMMAND combat_map_boundary_effect_tests)
    target_link_libraries(combat_terrain_damage_tests PRIVATE thandor_map_core)
    add_test(NAME combat_terrain_damage_tests COMMAND combat_terrain_damage_tests)
    if(THANDOR_TEST_ARCHIVE_ROOT)
        add_test(NAME combat_stock_terrain_damage_tests COMMAND combat_terrain_damage_tests "${THANDOR_TEST_ARCHIVE_ROOT}")
        add_test(NAME combat_stock_lava_environment_tests COMMAND combat_environment_projectile_tests "${THANDOR_TEST_ARCHIVE_ROOT}")
    endif()
    target_link_libraries(rendering_runtime_shadow_tests PRIVATE thandor_map_core)
    target_include_directories(rendering_runtime_shadow_tests PRIVATE src/editor)
    add_test(NAME rendering_runtime_shadow_tests COMMAND rendering_runtime_shadow_tests)
    if(THANDOR_TEST_ARCHIVE_ROOT)
        add_test(NAME rendering_stock_unit_shadow_assets
            COMMAND rendering_runtime_shadow_tests "${THANDOR_TEST_ARCHIVE_ROOT}")
        set_tests_properties(rendering_stock_unit_shadow_assets PROPERTIES LABELS "rendering;assets")
    endif()

    add_executable(rendering_model_vertex_lighting_tests tests/rendering_model_vertex_lighting_tests.cpp)
    target_link_libraries(rendering_model_vertex_lighting_tests PRIVATE thandor_map_core)
    target_include_directories(rendering_model_vertex_lighting_tests PRIVATE src/editor)
    add_test(NAME rendering_model_vertex_lighting_tests COMMAND rendering_model_vertex_lighting_tests)

    add_executable(editor_asset_fallback_tests tests/editor_asset_fallback_tests.cpp)
    target_link_libraries(editor_asset_fallback_tests PRIVATE thandor_map_core)
    add_test(NAME editor_asset_fallback_tests COMMAND editor_asset_fallback_tests)
    add_executable(rendering_directional_shadow_math_tests tests/rendering_directional_shadow_math_tests.cpp)
    target_link_libraries(rendering_directional_shadow_math_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_directional_shadow_math_tests COMMAND rendering_directional_shadow_math_tests)
    add_executable(rendering_shadow_performance_equivalence_tests tests/rendering_shadow_performance_equivalence_tests.cpp)
    target_link_libraries(rendering_shadow_performance_equivalence_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_shadow_performance_equivalence_tests COMMAND rendering_shadow_performance_equivalence_tests)

    add_executable(movement_crowded_escape_tests tests/movement_crowded_escape_tests.cpp)
    target_link_libraries(movement_crowded_escape_tests PRIVATE thandor_map_core)
    add_test(NAME movement_crowded_escape_tests COMMAND movement_crowded_escape_tests)

    add_executable(ai_military_site_origin_tests tests/ai_military_site_origin_tests.cpp)
    target_link_libraries(ai_military_site_origin_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_site_origin_tests COMMAND ai_military_site_origin_tests)

    add_executable(ai_military_unseen_group_fallback_tests tests/ai_military_unseen_group_fallback_tests.cpp)
    target_link_libraries(ai_military_unseen_group_fallback_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_unseen_group_fallback_tests COMMAND ai_military_unseen_group_fallback_tests)
    set_tests_properties(ai_military_unseen_group_fallback_tests PROPERTIES LABELS "ai;combat")
    add_executable(ai_owner_list_admission_v718_tests tests/ai_owner_list_admission_v718_tests.cpp)
    target_link_libraries(ai_owner_list_admission_v718_tests PRIVATE thandor_map_core)
    add_test(NAME ai_owner_list_admission_v718_tests COMMAND ai_owner_list_admission_v718_tests)

    add_executable(ai_phase7_owner_v726_tests tests/ai_phase7_owner_v726_tests.cpp)
    target_include_directories(ai_phase7_owner_v726_tests PRIVATE include)
    add_test(NAME ai_phase7_owner_v726_tests COMMAND ai_phase7_owner_v726_tests)
    add_test(NAME ai_phase7_owner_v726_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_phase7_owner_v726_source_contract_tests.py)
    add_executable(movement_v731_selection_stop_tests tests/movement_v731_selection_stop_tests.cpp)
    target_link_libraries(movement_v731_selection_stop_tests PRIVATE thandor_map_core)
    add_test(NAME movement_v731_selection_stop_tests COMMAND movement_v731_selection_stop_tests)
    add_executable(movement_v730_explicit_stop_tests tests/movement_v730_explicit_stop_tests.cpp)
    target_link_libraries(movement_v730_explicit_stop_tests PRIVATE thandor_map_core)
    add_test(NAME movement_v730_explicit_stop_tests COMMAND movement_v730_explicit_stop_tests)
    add_executable(movement_v729_arrival_anchor_tests tests/movement_v729_arrival_anchor_tests.cpp)
    target_link_libraries(movement_v729_arrival_anchor_tests PRIVATE thandor_map_core)
    add_test(NAME movement_v729_arrival_anchor_tests COMMAND movement_v729_arrival_anchor_tests)
    add_test(NAME movement_v729_arrival_anchor_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/movement_v729_arrival_anchor_source_contract_tests.py)
    add_test(NAME ai_phase7_world_job_v732_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_phase7_world_job_v732_source_contract_tests.py)
    add_test(NAME ai_phase7_world_job_v733_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_phase7_world_job_v733_source_contract_tests.py)
    add_executable(ai_phase7_world_job_v732_predicate_tests tests/ai_phase7_world_job_v732_predicate_tests.cpp)
    target_include_directories(ai_phase7_world_job_v732_predicate_tests PRIVATE include)
    add_test(NAME ai_phase7_world_job_v732_predicate_tests COMMAND ai_phase7_world_job_v732_predicate_tests)
    add_test(NAME ai_terrain_refresh_v735_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_terrain_refresh_v735_source_contract_tests.py)
    add_executable(ai_terrain_refresh_v735_helper_tests tests/ai_terrain_refresh_v735_helper_tests.cpp)
    target_include_directories(ai_terrain_refresh_v735_helper_tests PRIVATE include)
    add_test(NAME ai_terrain_refresh_v735_helper_tests COMMAND ai_terrain_refresh_v735_helper_tests)
    add_executable(ai_shot_tint_v737_tests tests/ai_shot_tint_v737_tests.cpp)
    target_link_libraries(ai_shot_tint_v737_tests PRIVATE thandor_map_core)
    add_test(NAME ai_shot_tint_v737_tests COMMAND ai_shot_tint_v737_tests)
    add_executable(ai_shot_terrain_v736_tests tests/ai_shot_terrain_v736_tests.cpp)
    target_link_libraries(ai_shot_terrain_v736_tests PRIVATE thandor_map_core)
    add_test(NAME ai_shot_terrain_v736_tests COMMAND ai_shot_terrain_v736_tests)
    add_executable(ai_terrain_state_v735_tests tests/ai_terrain_state_v735_tests.cpp)
    target_link_libraries(ai_terrain_state_v735_tests PRIVATE thandor_map_core)
    add_test(NAME ai_terrain_state_v735_tests COMMAND ai_terrain_state_v735_tests)
    add_executable(ai_phase7_preview_v734_tests tests/ai_phase7_preview_v734_tests.cpp)
    target_link_libraries(ai_phase7_preview_v734_tests PRIVATE thandor_map_core)
    add_test(NAME ai_phase7_preview_v734_tests COMMAND ai_phase7_preview_v734_tests)
    add_test(NAME ai_phase7_preview_v734_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_phase7_preview_v734_source_contract_tests.py)
    add_executable(ai_phase7_world_job_v732_tests tests/ai_phase7_world_job_v732_tests.cpp)
    target_link_libraries(ai_phase7_world_job_v732_tests PRIVATE thandor_map_core)
    add_test(NAME ai_phase7_world_job_v732_tests COMMAND ai_phase7_world_job_v732_tests)
    add_executable(ai_phase7_persistence_v728_tests tests/ai_phase7_persistence_v728_tests.cpp)
    target_include_directories(ai_phase7_persistence_v728_tests PRIVATE include)
    add_test(NAME ai_phase7_persistence_v728_tests COMMAND ai_phase7_persistence_v728_tests)
    add_test(NAME ai_phase7_persistence_v728_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_phase7_persistence_v728_source_contract_tests.py)
    add_executable(ai_phase7_occlusion_v727_tests tests/ai_phase7_occlusion_v727_tests.cpp)
    target_include_directories(ai_phase7_occlusion_v727_tests PRIVATE include)
    add_test(NAME ai_phase7_occlusion_v727_tests COMMAND ai_phase7_occlusion_v727_tests)
    add_executable(ai_phase7_model_owner_callbacks_v745_tests
                   tests/ai_phase7_model_owner_callbacks_v745_tests.cpp)
    target_link_libraries(ai_phase7_model_owner_callbacks_v745_tests PRIVATE thandor_map_core)
    add_test(NAME ai_phase7_model_owner_callbacks_v745_tests
             COMMAND ai_phase7_model_owner_callbacks_v745_tests)
    add_test(NAME ai_phase7_model_owner_callbacks_v745_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tests/ai_phase7_model_owner_callbacks_v745_source_contract_tests.py)

    add_executable(ai_workspace01_grid_mask_v725_tests tests/ai_workspace01_grid_mask_v725_tests.cpp)
    target_link_libraries(ai_workspace01_grid_mask_v725_tests PRIVATE thandor_map_core)
    add_test(NAME ai_workspace01_grid_mask_v725_tests COMMAND ai_workspace01_grid_mask_v725_tests)
    add_executable(ai_workspace_base_extents_v724_tests tests/ai_workspace_base_extents_v724_tests.cpp)
    target_link_libraries(ai_workspace_base_extents_v724_tests PRIVATE thandor_map_core)
    add_test(NAME ai_workspace_base_extents_v724_tests COMMAND ai_workspace_base_extents_v724_tests)
    add_test(NAME ai_workspace_base_extents_v724_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/ai_workspace_base_extents_v724_source_contract_tests.py)
    add_executable(ai_owner_workspace_geometry_v719_tests tests/ai_owner_workspace_geometry_v719_tests.cpp)
    target_link_libraries(ai_owner_workspace_geometry_v719_tests PRIVATE thandor_map_core)
    add_test(NAME ai_owner_workspace_geometry_v719_tests COMMAND ai_owner_workspace_geometry_v719_tests)
    add_executable(ai_military_anchor_tests tests/ai_military_anchor_tests.cpp)
    target_link_libraries(ai_military_anchor_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_anchor_tests COMMAND ai_military_anchor_tests)

    add_executable(research_creation_technology_progression_tests tests/research_creation_technology_progression_tests.cpp)
    target_link_libraries(research_creation_technology_progression_tests PRIVATE thandor_map_core)
    add_test(NAME research_creation_technology_progression_tests COMMAND research_creation_technology_progression_tests)
    add_executable(rendering_model_health_water_tests tests/rendering_model_health_water_tests.cpp)
    target_link_libraries(rendering_model_health_water_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_model_health_water_tests COMMAND rendering_model_health_water_tests)
    add_executable(placement_pioneer_ai_dispatch_tests tests/placement_pioneer_ai_dispatch_tests.cpp)
    target_link_libraries(placement_pioneer_ai_dispatch_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_ai_dispatch_tests COMMAND placement_pioneer_ai_dispatch_tests)
    add_executable(placement_pioneer_reset_workspace_tests
                   tests/placement_pioneer_reset_workspace_tests.cpp)
    target_include_directories(placement_pioneer_reset_workspace_tests PRIVATE include)
    add_test(NAME placement_pioneer_reset_workspace_tests
             COMMAND placement_pioneer_reset_workspace_tests)
    add_executable(placement_pioneer_gate_trace_tests
                   tests/placement_pioneer_gate_trace_tests.cpp)
    target_include_directories(placement_pioneer_gate_trace_tests PRIVATE include)
    add_test(NAME placement_pioneer_gate_trace_tests
             COMMAND placement_pioneer_gate_trace_tests)
    add_executable(placement_pioneer_site_selector_trace_tests
                   tests/placement_pioneer_site_selector_trace_tests.cpp)
    target_include_directories(placement_pioneer_site_selector_trace_tests PRIVATE include)
    add_test(NAME placement_pioneer_site_selector_trace_tests
             COMMAND placement_pioneer_site_selector_trace_tests)
    add_executable(placement_pioneer_class14_placement_tests
                   tests/placement_pioneer_class14_placement_tests.cpp)
    target_link_libraries(placement_pioneer_class14_placement_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_class14_placement_tests
             COMMAND placement_pioneer_class14_placement_tests)
    add_executable(placement_pioneer_site_reservation_tests
                   tests/placement_pioneer_site_reservation_tests.cpp)
    target_include_directories(placement_pioneer_site_reservation_tests PRIVATE include)
    add_test(NAME placement_pioneer_site_reservation_tests
             COMMAND placement_pioneer_site_reservation_tests)
    add_executable(placement_pioneer_site_bucket_tests
                   tests/placement_pioneer_site_bucket_tests.cpp)
    target_link_libraries(placement_pioneer_site_bucket_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_site_bucket_tests
             COMMAND placement_pioneer_site_bucket_tests)
    add_executable(movement_articulated_walker_contact_parity_tests
                   tests/movement_articulated_walker_contact_parity_tests.cpp)
    target_include_directories(movement_articulated_walker_contact_parity_tests PRIVATE src/editor)
    target_link_libraries(movement_articulated_walker_contact_parity_tests PRIVATE thandor_map_core)
    add_test(NAME movement_articulated_walker_contact_parity_tests
             COMMAND movement_articulated_walker_contact_parity_tests)
    add_test(NAME movement_fps_walker_footprint_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_fps_walker_footprint_source_contract_tests.py
                     ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(audio_collision_audio_completion_tests
                   tests/audio_collision_audio_completion_tests.cpp)
    target_link_libraries(audio_collision_audio_completion_tests PRIVATE thandor_map_core)
    target_compile_definitions(audio_collision_audio_completion_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME audio_collision_audio_completion_tests
             COMMAND audio_collision_audio_completion_tests)
    add_executable(audio_terrain_walker_sound_movement_tests
                   tests/audio_terrain_walker_sound_movement_tests.cpp)
    target_include_directories(audio_terrain_walker_sound_movement_tests PRIVATE src/editor)
    target_link_libraries(audio_terrain_walker_sound_movement_tests PRIVATE thandor_map_core)
    target_compile_definitions(audio_terrain_walker_sound_movement_tests PRIVATE
                               THANDOR_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")
    add_test(NAME audio_terrain_walker_sound_movement_tests
             COMMAND audio_terrain_walker_sound_movement_tests)

    add_executable(movement_walker_start_stop_tests
                   tests/movement_walker_start_stop_tests.cpp)
    target_include_directories(movement_walker_start_stop_tests PRIVATE src/editor)
    target_link_libraries(movement_walker_start_stop_tests PRIVATE thandor_map_core)
    target_compile_definitions(movement_walker_start_stop_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME movement_walker_start_stop_tests
             COMMAND movement_walker_start_stop_tests)

    add_executable(animation_unit_building_attachment_pose_tests
                   tests/animation_unit_building_attachment_pose_tests.cpp)
    target_link_libraries(animation_unit_building_attachment_pose_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_unit_building_attachment_pose_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_unit_building_attachment_pose_tests
             COMMAND animation_unit_building_attachment_pose_tests)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_library(audio_linux_fake_alsa SHARED tests/audio_linux_fake_alsa.cpp)
        set_target_properties(audio_linux_fake_alsa PROPERTIES OUTPUT_NAME asound SOVERSION 2
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/audio_test_backend")
        add_executable(audio_linux_mixer_tests tests/audio_linux_mixer_tests.cpp)
        target_link_libraries(audio_linux_mixer_tests PRIVATE thandor_map_core ${CMAKE_DL_LIBS})
        add_dependencies(audio_linux_mixer_tests audio_linux_fake_alsa)
        add_test(NAME audio_linux_mixer_tests COMMAND ${CMAKE_COMMAND} -E env
            "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:audio_linux_fake_alsa>"
            $<TARGET_FILE:audio_linux_mixer_tests>)
    endif()

    add_executable(audio_continuous_runtime_audio_tests
                   tests/audio_continuous_runtime_audio_tests.cpp)
    target_link_libraries(audio_continuous_runtime_audio_tests PRIVATE thandor_map_core)
    target_compile_definitions(audio_continuous_runtime_audio_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME audio_continuous_runtime_audio_tests
             COMMAND audio_continuous_runtime_audio_tests)

    add_executable(audio_spatial_audio_special_effect_tests
                   tests/audio_spatial_audio_special_effect_tests.cpp)
    target_link_libraries(audio_spatial_audio_special_effect_tests PRIVATE thandor_map_core)
    target_compile_definitions(audio_spatial_audio_special_effect_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME audio_spatial_audio_special_effect_tests
             COMMAND audio_spatial_audio_special_effect_tests)

    add_executable(movement_walker_arrival_resource_field_regression_tests
                   tests/movement_walker_arrival_resource_field_regression_tests.cpp)
    target_include_directories(movement_walker_arrival_resource_field_regression_tests PRIVATE src/editor)
    target_link_libraries(movement_walker_arrival_resource_field_regression_tests PRIVATE thandor_map_core)
    target_compile_definitions(movement_walker_arrival_resource_field_regression_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME movement_walker_arrival_resource_field_regression_tests
             COMMAND movement_walker_arrival_resource_field_regression_tests)

    add_executable(combat_laser_coil_direct_line_regression_tests
                   tests/combat_laser_coil_direct_line_regression_tests.cpp)
    target_link_libraries(combat_laser_coil_direct_line_regression_tests PRIVATE thandor_map_core)
    target_compile_definitions(combat_laser_coil_direct_line_regression_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_laser_coil_direct_line_regression_tests
             COMMAND combat_laser_coil_direct_line_regression_tests)

    add_executable(movement_ground_mark_history_tests
                   tests/movement_ground_mark_history_tests.cpp)
    target_link_libraries(movement_ground_mark_history_tests PRIVATE thandor_map_core)
    target_compile_definitions(movement_ground_mark_history_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME movement_ground_mark_history_tests
             COMMAND movement_ground_mark_history_tests)
    enable_testing()
    add_executable(ai_path_ai_diagnostics_tests
                   tests/ai_path_ai_diagnostics_tests.cpp)
    target_link_libraries(ai_path_ai_diagnostics_tests PRIVATE thandor_map_core)
    target_compile_definitions(ai_path_ai_diagnostics_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ai_path_ai_diagnostics_tests
             COMMAND ai_path_ai_diagnostics_tests)

    add_executable(simlab_tests
                   tests/simlab_tests.cpp)
    target_link_libraries(simlab_tests PRIVATE thandor_map_core)
    target_compile_definitions(simlab_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME simlab_tests
             COMMAND simlab_tests)

    add_executable(rendering_retail_visualization_audit_tests
                   tests/rendering_retail_visualization_audit_tests.cpp)
    target_link_libraries(rendering_retail_visualization_audit_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_retail_visualization_audit_tests
             COMMAND rendering_retail_visualization_audit_tests)

    add_executable(rendering_generated_shadow_receiver_packet_tests
                   tests/rendering_generated_shadow_receiver_packet_tests.cpp)
    target_link_libraries(rendering_generated_shadow_receiver_packet_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_generated_shadow_receiver_packet_tests
             COMMAND rendering_generated_shadow_receiver_packet_tests)

    add_executable(rendering_generated_shadow_contact_solver_tests
                   tests/rendering_generated_shadow_contact_solver_tests.cpp)
    target_link_libraries(rendering_generated_shadow_contact_solver_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_generated_shadow_contact_solver_tests
             COMMAND rendering_generated_shadow_contact_solver_tests)

    add_test(NAME rendering_lab_damage_emitter_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_lab_damage_emitter_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(rendering_lab_terminal_residency_tests
                   tests/rendering_lab_terminal_residency_tests.cpp)
    target_include_directories(rendering_lab_terminal_residency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rendering_lab_terminal_residency_tests
             COMMAND rendering_lab_terminal_residency_tests)

    add_executable(rendering_lab_component_damage_visual_tests
                   tests/rendering_lab_component_damage_visual_tests.cpp)
    target_include_directories(rendering_lab_component_damage_visual_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    add_test(NAME rendering_lab_component_damage_visual_tests
             COMMAND rendering_lab_component_damage_visual_tests)

    add_executable(combat_component_damage_propagation_tests
                   tests/combat_component_damage_propagation_tests.cpp)
    target_link_libraries(combat_component_damage_propagation_tests PRIVATE thandor_map_core)
    add_test(NAME combat_component_damage_propagation_tests
             COMMAND combat_component_damage_propagation_tests)
    add_executable(simlab_worker_tests
                   tests/simlab_worker_tests.cpp)
    target_link_libraries(simlab_worker_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_worker_tests
             COMMAND simlab_worker_tests)

    add_executable(visibility_live_reveal_timing_tests
                   tests/visibility_live_reveal_timing_tests.cpp)
    target_link_libraries(visibility_live_reveal_timing_tests PRIVATE thandor_map_core)
    add_test(NAME visibility_live_reveal_timing_tests
             COMMAND visibility_live_reveal_timing_tests)

    add_test(NAME rendering_visual_timing_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_visual_timing_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(simlab_parallel_worker_tests
                   tests/simlab_parallel_worker_tests.cpp)
    target_link_libraries(simlab_parallel_worker_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_parallel_worker_tests
             COMMAND simlab_parallel_worker_tests)
    add_executable(simlab_worker_pool_tests tests/simlab_worker_pool_tests.cpp)
    target_link_libraries(simlab_worker_pool_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_worker_pool_tests COMMAND simlab_worker_pool_tests)

    add_test(NAME simlab_parallel_worker_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/simlab_parallel_worker_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(ai_construction_evidence_tests
                   tests/ai_construction_evidence_tests.cpp)
    target_link_libraries(ai_construction_evidence_tests PRIVATE thandor_map_core)
    add_test(NAME ai_construction_evidence_tests
             COMMAND ai_construction_evidence_tests)

    add_test(NAME rendering_selected_construction_renderer_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_selected_construction_renderer_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(ai_construction_economy_pending_tests
                   tests/ai_construction_economy_pending_tests.cpp)
    target_link_libraries(ai_construction_economy_pending_tests PRIVATE thandor_map_core)
    add_test(NAME ai_construction_economy_pending_tests
             COMMAND ai_construction_economy_pending_tests)

    add_test(NAME ai_construction_economy_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_construction_economy_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(production_producer_pipeline_tests
                   tests/production_producer_pipeline_tests.cpp)
    target_link_libraries(production_producer_pipeline_tests PRIVATE thandor_map_core)
    add_test(NAME production_producer_pipeline_tests
             COMMAND production_producer_pipeline_tests)

    add_test(NAME production_producer_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/production_producer_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(research_points_combat_tests
                   tests/research_points_combat_tests.cpp)
    target_link_libraries(research_points_combat_tests PRIVATE thandor_map_core)
    add_test(NAME research_points_combat_tests
             COMMAND research_points_combat_tests)

    add_executable(combat_direct_beam_lifetime_damage_tests
                   tests/combat_direct_beam_lifetime_damage_tests.cpp)
    target_link_libraries(combat_direct_beam_lifetime_damage_tests PRIVATE thandor_map_core)
    add_test(NAME combat_direct_beam_lifetime_damage_tests
             COMMAND combat_direct_beam_lifetime_damage_tests)

    add_executable(simlab_retail_initialization_tests
                   tests/simlab_retail_initialization_tests.cpp)
    target_link_libraries(simlab_retail_initialization_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_retail_initialization_tests
             COMMAND simlab_retail_initialization_tests)
    add_test(NAME simlab_retail_initialization_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/simlab_retail_initialization_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(simlab_retail_initialization_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(visibility_worker_snapshot_timing_tests
                   tests/visibility_worker_snapshot_timing_tests.cpp)
    target_link_libraries(visibility_worker_snapshot_timing_tests PRIVATE thandor_map_core)
    add_test(NAME visibility_worker_snapshot_timing_tests
             COMMAND visibility_worker_snapshot_timing_tests)
    # Keep this worker workload isolated within CTest. Correctness is portable;
    # the opt-in wall-clock benchmark additionally requires a controlled host.
    set_tests_properties(visibility_worker_snapshot_timing_tests
                         PROPERTIES RUN_SERIAL TRUE)
    if(THANDOR_ENFORCE_20HZ_BUDGET)
        set_tests_properties(visibility_worker_snapshot_timing_tests
                             PROPERTIES ENVIRONMENT "THANDOR_ENFORCE_20HZ_BUDGET=1")
    endif()
    add_test(NAME visibility_timing_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/visibility_timing_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(visibility_timing_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME placement_pioneer_render_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_pioneer_render_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(placement_pioneer_render_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(animation_class14_runtime_hierarchy_tests
                   tests/animation_class14_runtime_hierarchy_tests.cpp)
    target_link_libraries(animation_class14_runtime_hierarchy_tests PRIVATE thandor_map_core)
    add_test(NAME animation_class14_runtime_hierarchy_tests
             COMMAND animation_class14_runtime_hierarchy_tests)

    add_executable(placement_pioneer_special_site_movement_tests
                   tests/placement_pioneer_special_site_movement_tests.cpp)
    target_link_libraries(placement_pioneer_special_site_movement_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_special_site_movement_tests
             COMMAND placement_pioneer_special_site_movement_tests)

    add_executable(placement_pioneer_conversion_tests
                   tests/placement_pioneer_conversion_tests.cpp)
    target_link_libraries(placement_pioneer_conversion_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_conversion_tests
             COMMAND placement_pioneer_conversion_tests)

    add_executable(production_bootstrap_production_tests
                   tests/production_bootstrap_production_tests.cpp)
    target_link_libraries(production_bootstrap_production_tests PRIVATE thandor_map_core)
    add_test(NAME production_bootstrap_production_tests
             COMMAND production_bootstrap_production_tests)
    add_test(NAME production_bootstrap_producer_eligibility_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/production_bootstrap_producer_eligibility_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(production_bootstrap_producer_eligibility_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(placement_pioneer_build_area_tests
                   tests/placement_pioneer_build_area_tests.cpp)
    target_link_libraries(placement_pioneer_build_area_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_build_area_tests
             COMMAND placement_pioneer_build_area_tests)

    add_test(NAME placement_pioneer_motion_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_pioneer_motion_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(simlab_primary_queue_override_tests
                   tests/simlab_primary_queue_override_tests.cpp)
    target_link_libraries(simlab_primary_queue_override_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_primary_queue_override_tests
             COMMAND simlab_primary_queue_override_tests)

    add_test(NAME build_primary_queue_override_source_contract_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_primary_queue_override_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(simlab_autonomous_unit_planner_tests
                   tests/simlab_autonomous_unit_planner_tests.cpp)
    target_link_libraries(simlab_autonomous_unit_planner_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_autonomous_unit_planner_tests
             COMMAND simlab_autonomous_unit_planner_tests)
    add_test(NAME ai_army_planner_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_army_planner_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(ai_army_planner_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME combat_synthetic_terminal_event_render_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_synthetic_terminal_event_render_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    add_executable(combat_synthetic_terminal_world_pose_tests
                   tests/combat_synthetic_terminal_world_pose_tests.cpp)
    target_link_libraries(combat_synthetic_terminal_world_pose_tests PRIVATE thandor_map_core)
    add_test(NAME combat_synthetic_terminal_world_pose_tests
             COMMAND combat_synthetic_terminal_world_pose_tests)

    add_executable(combat_terminal_root_destruction_effect_tests
                   tests/combat_terminal_root_destruction_effect_tests.cpp)
    target_link_libraries(combat_terminal_root_destruction_effect_tests PRIVATE thandor_map_core)
    add_test(NAME combat_terminal_root_destruction_effect_tests
             COMMAND combat_terminal_root_destruction_effect_tests)

    add_executable(rendering_terminal_attachment_effect_tests
                   tests/rendering_terminal_attachment_effect_tests.cpp)
    target_link_libraries(rendering_terminal_attachment_effect_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_terminal_attachment_effect_tests
             COMMAND rendering_terminal_attachment_effect_tests)

    add_executable(rendering_force_shield_timed_effect_gate_tests
                   tests/rendering_force_shield_timed_effect_gate_tests.cpp)
    target_link_libraries(rendering_force_shield_timed_effect_gate_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_force_shield_timed_effect_gate_tests
             COMMAND rendering_force_shield_timed_effect_gate_tests)

    add_test(NAME build_force_shield_stock_chain_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_force_shield_stock_chain_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(build_force_shield_stock_chain_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME placement_pioneer_conversion_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_pioneer_conversion_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(placement_pioneer_conversion_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(placement_simlab_retail_placement_tests
                   tests/placement_simlab_retail_placement_tests.cpp)
    target_link_libraries(placement_simlab_retail_placement_tests PRIVATE thandor_map_core)
    add_test(NAME placement_simlab_retail_placement_tests
             COMMAND placement_simlab_retail_placement_tests)
    add_test(NAME placement_retail_placement_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_retail_placement_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(placement_retail_placement_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME research_points_combat_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/research_points_combat_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(ai_candidate_weight_randomization_tests
                   tests/ai_candidate_weight_randomization_tests.cpp)
    target_link_libraries(ai_candidate_weight_randomization_tests PRIVATE thandor_map_core)
    add_test(NAME ai_candidate_weight_randomization_tests
             COMMAND ai_candidate_weight_randomization_tests)

    add_executable(ai_workspace10_anchor_scoring_tests
                   tests/ai_workspace10_anchor_scoring_tests.cpp)
    target_link_libraries(ai_workspace10_anchor_scoring_tests PRIVATE thandor_map_core)
    add_test(NAME ai_workspace10_anchor_scoring_tests
             COMMAND ai_workspace10_anchor_scoring_tests)

    add_executable(simlab_presentation_parity_tests
                   tests/simlab_presentation_parity_tests.cpp)
    target_link_libraries(simlab_presentation_parity_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_presentation_parity_tests
             COMMAND simlab_presentation_parity_tests)

    add_test(NAME simlab_presentation_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/simlab_presentation_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(combat_one_shot_event_transport_tests
                   tests/combat_one_shot_event_transport_tests.cpp)
    target_link_libraries(combat_one_shot_event_transport_tests PRIVATE thandor_map_core)
    add_test(NAME combat_one_shot_event_transport_tests
             COMMAND combat_one_shot_event_transport_tests)

    add_test(NAME combat_one_shot_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_one_shot_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(movement_articulated_contact_geometry_tests
                   tests/movement_articulated_contact_geometry_tests.cpp)
    target_link_libraries(movement_articulated_contact_geometry_tests PRIVATE thandor_map_core)
    add_test(NAME movement_articulated_contact_geometry_tests
             COMMAND movement_articulated_contact_geometry_tests)

    add_executable(aviation_class22_airport_linked_child_state_tests
                   tests/aviation_class22_airport_linked_child_state_tests.cpp)
    target_link_libraries(aviation_class22_airport_linked_child_state_tests PRIVATE thandor_map_core)
    add_test(NAME aviation_class22_airport_linked_child_state_tests
             COMMAND aviation_class22_airport_linked_child_state_tests)

    add_executable(scenario_faction_graphics_family_tests
                   tests/scenario_faction_graphics_family_tests.cpp)
    target_link_libraries(scenario_faction_graphics_family_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_faction_graphics_family_tests
             COMMAND scenario_faction_graphics_family_tests)

    add_test(NAME scenario_faction_graphics_family_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario_faction_graphics_family_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME aviation_class21_airport_sortie_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/aviation_class21_airport_sortie_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME aviation_class22_airport_editor_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/aviation_class22_airport_editor_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(assets_binary_oracle_compare_tests
                   tests/assets_binary_oracle_compare_tests.cpp)
    target_link_libraries(assets_binary_oracle_compare_tests PRIVATE thandor_map_core)

    add_executable(core_class21_pitch_oracle_compare_tests
                   tests/core_class21_pitch_oracle_compare_tests.cpp)
    target_link_libraries(core_class21_pitch_oracle_compare_tests PRIVATE thandor_map_core)

    add_executable(animation_class21_pitch_tests tests/animation_class21_pitch_tests.cpp)
    target_link_libraries(animation_class21_pitch_tests PRIVATE thandor_map_core)
    add_test(NAME animation_class21_pitch_tests COMMAND animation_class21_pitch_tests)
    add_executable(animation_class21_state5_tests tests/animation_class21_state5_tests.cpp)
    target_link_libraries(animation_class21_state5_tests PRIVATE thandor_map_core)
    add_test(NAME animation_class21_state5_tests COMMAND animation_class21_state5_tests)

    add_executable(combat_projectile_runtime_collision_contract_tests
                   tests/combat_projectile_runtime_collision_contract_tests.cpp)
    target_link_libraries(combat_projectile_runtime_collision_contract_tests PRIVATE thandor_map_core)
    target_compile_definitions(combat_projectile_runtime_collision_contract_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_projectile_runtime_collision_contract_tests
             COMMAND combat_projectile_runtime_collision_contract_tests)

    add_test(NAME combat_model_collision_hull_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_model_collision_hull_source_contract_tests.py)

    add_test(NAME assets_destroyed_model_raycast_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets_destroyed_model_raycast_source_contract_tests.py)

    add_test(NAME combat_destruction_collision_substep_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_destruction_collision_substep_source_contract_tests.py)

    add_test(NAME combat_offmap_linked_child_targeting_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_offmap_linked_child_targeting_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME build_visible_runtime_feedback_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_visible_runtime_feedback_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(rendering_frontend_player_texture_tests
                   tests/rendering_frontend_player_texture_tests.cpp)
    target_link_libraries(rendering_frontend_player_texture_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_frontend_player_texture_tests
             COMMAND rendering_frontend_player_texture_tests)

    add_test(NAME scenario_frontend_player_color_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario_frontend_player_color_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME combat_transient_runtime_damage_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_transient_runtime_damage_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(editor_stock_in_place_save_tests
                   tests/editor_stock_in_place_save_tests.cpp)
    target_link_libraries(editor_stock_in_place_save_tests PRIVATE thandor_map_core)
    add_test(NAME editor_stock_in_place_save_tests
             COMMAND editor_stock_in_place_save_tests)

    add_test(NAME editor_save_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_save_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(combat_lava_linked_shot_runtime_tests
                   tests/combat_lava_linked_shot_runtime_tests.cpp)
    target_link_libraries(combat_lava_linked_shot_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME combat_lava_linked_shot_runtime_tests
             COMMAND combat_lava_linked_shot_runtime_tests)

    add_executable(combat_environmental_shot_owner_tests
                   tests/combat_environmental_shot_owner_tests.cpp)
    target_link_libraries(combat_environmental_shot_owner_tests PRIVATE thandor_map_core)
    add_test(NAME combat_environmental_shot_owner_tests
             COMMAND combat_environmental_shot_owner_tests)
    add_test(NAME combat_environmental_shot_runtime_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_environmental_shot_runtime_source_contract_tests.py)

    add_executable(animation_environmental_effect_pose_history_tests
                   tests/animation_environmental_effect_pose_history_tests.cpp)
    target_link_libraries(animation_environmental_effect_pose_history_tests PRIVATE thandor_map_core)
    add_test(NAME animation_environmental_effect_pose_history_tests
             COMMAND animation_environmental_effect_pose_history_tests)
    add_test(NAME animation_environmental_effect_pose_history_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/animation_environmental_effect_pose_history_source_contract_tests.py)

    add_executable(rendering_ground_surface_texture_parity_tests
                   tests/rendering_ground_surface_texture_parity_tests.cpp)
    target_link_libraries(rendering_ground_surface_texture_parity_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_ground_surface_texture_parity_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_ground_surface_texture_parity_tests
             COMMAND rendering_ground_surface_texture_parity_tests)

    add_test(NAME build_class21_state5_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_class21_state5_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_test(NAME build_transient_owner_los_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_transient_owner_los_source_contract_tests.py)

    add_test(NAME production_factory_production_editor_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/production_factory_production_editor_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME simlab_threading_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/simlab_threading_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME editor_simulation_ui_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_simulation_ui_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME editor_translation_unit_compile_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_translation_unit_compile_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR}
                     --compiler ${CMAKE_CXX_COMPILER})
    set_tests_properties(editor_translation_unit_compile_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(ai_path_collision_los_ai_tests
                   tests/ai_path_collision_los_ai_tests.cpp)
    target_link_libraries(ai_path_collision_los_ai_tests PRIVATE thandor_map_core)
    target_compile_definitions(ai_path_collision_los_ai_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ai_path_collision_los_ai_tests
             COMMAND ai_path_collision_los_ai_tests)
    add_test(NAME editor_path_preview_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_path_preview_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_path_preview_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(placement_resource_extractor_placement_rotation_assembly_tests
                   tests/placement_resource_extractor_placement_rotation_assembly_tests.cpp)
    target_include_directories(placement_resource_extractor_placement_rotation_assembly_tests PRIVATE src/editor)
    target_link_libraries(placement_resource_extractor_placement_rotation_assembly_tests PRIVATE thandor_map_core)
    target_compile_definitions(placement_resource_extractor_placement_rotation_assembly_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME placement_resource_extractor_placement_rotation_assembly_tests
             COMMAND placement_resource_extractor_placement_rotation_assembly_tests)
    add_executable(rendering_world_render_terrain_packet_tests
                   tests/rendering_world_render_terrain_packet_tests.cpp)
    target_include_directories(rendering_world_render_terrain_packet_tests PRIVATE src/editor)
    target_link_libraries(rendering_world_render_terrain_packet_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_world_render_terrain_packet_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_world_render_terrain_packet_tests
             COMMAND rendering_world_render_terrain_packet_tests)
    add_executable(visibility_campaign_script_fog_shadow_tests
                   tests/visibility_campaign_script_fog_shadow_tests.cpp)
    target_link_libraries(visibility_campaign_script_fog_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(visibility_campaign_script_fog_shadow_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_campaign_script_fog_shadow_tests
             COMMAND visibility_campaign_script_fog_shadow_tests)
    add_executable(placement_resource_extractor_context_order_tests
                   tests/placement_resource_extractor_context_order_tests.cpp)
    target_include_directories(placement_resource_extractor_context_order_tests PRIVATE src/editor)
    target_link_libraries(placement_resource_extractor_context_order_tests PRIVATE thandor_map_core)
    target_compile_definitions(placement_resource_extractor_context_order_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME placement_resource_extractor_context_order_tests
             COMMAND placement_resource_extractor_context_order_tests)
    add_executable(visibility_fog_surface_object_visibility_regression_tests
                   tests/visibility_fog_surface_object_visibility_regression_tests.cpp)
    target_link_libraries(visibility_fog_surface_object_visibility_regression_tests PRIVATE thandor_map_core)
    target_compile_definitions(visibility_fog_surface_object_visibility_regression_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_fog_surface_object_visibility_regression_tests
             COMMAND visibility_fog_surface_object_visibility_regression_tests)
    add_executable(visibility_retail_projected_fog_of_war_tests
                   tests/visibility_retail_projected_fog_of_war_tests.cpp)
    target_link_libraries(visibility_retail_projected_fog_of_war_tests PRIVATE thandor_map_core)
    add_test(NAME visibility_retail_projected_fog_of_war_tests
             COMMAND visibility_retail_projected_fog_of_war_tests)

    add_executable(research_build_technology_tracks_radar_fog_tests
                   tests/research_build_technology_tracks_radar_fog_tests.cpp)
    target_link_libraries(research_build_technology_tracks_radar_fog_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(research_build_technology_tracks_radar_fog_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME research_build_technology_tracks_radar_fog_tests
             COMMAND research_build_technology_tracks_radar_fog_tests)
    add_test(NAME editor_integrated_compile_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_integrated_compile_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_integrated_compile_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME movement_walker_preview_callsite_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_walker_preview_callsite_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(movement_walker_preview_callsite_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME editor_regression_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_regression_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_regression_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(animation_visible_building_animation_rate_tests
                   tests/animation_visible_building_animation_rate_tests.cpp)
    target_link_libraries(animation_visible_building_animation_rate_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(animation_visible_building_animation_rate_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_visible_building_animation_rate_tests
             COMMAND animation_visible_building_animation_rate_tests)

    add_executable(animation_game_runtime_building_animation_pipeline_tests
                   tests/animation_game_runtime_building_animation_pipeline_tests.cpp)
    target_link_libraries(animation_game_runtime_building_animation_pipeline_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(animation_game_runtime_building_animation_pipeline_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_game_runtime_building_animation_pipeline_tests
             COMMAND animation_game_runtime_building_animation_pipeline_tests)

    add_executable(movement_tank_track_articulated_walker_unit_class_tests
                   tests/movement_tank_track_articulated_walker_unit_class_tests.cpp)
    target_include_directories(movement_tank_track_articulated_walker_unit_class_tests PRIVATE src/editor)
    target_link_libraries(movement_tank_track_articulated_walker_unit_class_tests PRIVATE thandor_map_core)
    target_compile_definitions(movement_tank_track_articulated_walker_unit_class_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME movement_tank_track_articulated_walker_unit_class_tests
             COMMAND movement_tank_track_articulated_walker_unit_class_tests)

    add_executable(animation_articulated_turn_hornet_deployment_tests
                   tests/animation_articulated_turn_hornet_deployment_tests.cpp)
    target_include_directories(animation_articulated_turn_hornet_deployment_tests PRIVATE src/editor)
    target_link_libraries(animation_articulated_turn_hornet_deployment_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_articulated_turn_hornet_deployment_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_articulated_turn_hornet_deployment_tests
             COMMAND animation_articulated_turn_hornet_deployment_tests)
    add_test(NAME build_unit_class_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_unit_class_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(build_unit_class_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(terrain_raycast_lookup_domain_tests
                   tests/terrain_raycast_lookup_domain_tests.cpp)
    target_include_directories(terrain_raycast_lookup_domain_tests PRIVATE src/editor)
    target_link_libraries(terrain_raycast_lookup_domain_tests PRIVATE thandor_map_core)
    target_compile_definitions(terrain_raycast_lookup_domain_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME terrain_raycast_lookup_domain_tests
             COMMAND terrain_raycast_lookup_domain_tests)
    add_executable(core_map_core_tests tests/core_tests.cpp)
    target_link_libraries(core_map_core_tests PRIVATE thandor_map_core)
    add_test(NAME core_map_core_tests COMMAND core_map_core_tests)

    add_executable(assets_game_executable_name_tests tests/assets_game_executable_name_tests.cpp)
    target_link_libraries(assets_game_executable_name_tests PRIVATE thandor_map_core)
    add_test(NAME assets_game_executable_name_tests COMMAND assets_game_executable_name_tests)

    add_executable(editor_math_tests tests/editor_math_tests.cpp)
    target_include_directories(editor_math_tests PRIVATE src/editor)
    target_link_libraries(editor_math_tests PRIVATE thandor_map_core)
    add_test(NAME editor_math_tests COMMAND editor_math_tests)

    add_executable(rendering_terrain_render_tests tests/rendering_terrain_render_tests.cpp)
    target_link_libraries(rendering_terrain_render_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_terrain_render_tests COMMAND rendering_terrain_render_tests)

    add_executable(rendering_object_visual_tests tests/rendering_object_visual_tests.cpp)
    target_link_libraries(rendering_object_visual_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_object_visual_tests COMMAND rendering_object_visual_tests)

    add_executable(rendering_resource_glow_water_tests tests/rendering_resource_glow_water_tests.cpp)
    target_link_libraries(rendering_resource_glow_water_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_resource_glow_water_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_resource_glow_water_tests COMMAND rendering_resource_glow_water_tests)

    add_executable(assets_asset_browser_transform_tests tests/assets_asset_browser_transform_tests.cpp)
    target_include_directories(assets_asset_browser_transform_tests PRIVATE src/editor)
    target_link_libraries(assets_asset_browser_transform_tests PRIVATE thandor_map_core)
    add_test(NAME assets_asset_browser_transform_tests COMMAND assets_asset_browser_transform_tests)

    add_executable(rendering_asset_browser_category_texture_tests tests/rendering_asset_browser_category_texture_tests.cpp)
    target_include_directories(rendering_asset_browser_category_texture_tests PRIVATE src/editor)
    target_link_libraries(rendering_asset_browser_category_texture_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_asset_browser_category_texture_tests COMMAND rendering_asset_browser_category_texture_tests)

    add_executable(editor_map_workflow_tests tests/editor_map_workflow_tests.cpp)
    target_link_libraries(editor_map_workflow_tests PRIVATE thandor_map_core)
    add_test(NAME editor_map_workflow_tests COMMAND editor_map_workflow_tests)

    add_executable(scenario_game_level_package_tests tests/scenario_game_level_package_tests.cpp)
    target_link_libraries(scenario_game_level_package_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_game_level_package_tests COMMAND scenario_game_level_package_tests)

    add_executable(movement_game_package_path_tests tests/movement_game_package_path_tests.cpp)
    target_link_libraries(movement_game_package_path_tests PRIVATE thandor_map_core)
    add_test(NAME movement_game_package_path_tests COMMAND movement_game_package_path_tests)

    add_executable(scenario_frontend_catalog_runtime_tests tests/scenario_frontend_catalog_runtime_tests.cpp)
    target_link_libraries(scenario_frontend_catalog_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_frontend_catalog_runtime_tests COMMAND scenario_frontend_catalog_runtime_tests)

    add_executable(assets_custom_alias_fallback_tests tests/assets_custom_alias_fallback_tests.cpp)
    target_link_libraries(assets_custom_alias_fallback_tests PRIVATE thandor_map_core)
    add_test(NAME assets_custom_alias_fallback_tests COMMAND assets_custom_alias_fallback_tests)

    add_executable(scenario_player_faction_begin_contract_tests
                   tests/scenario_player_faction_begin_contract_tests.cpp)
    target_link_libraries(scenario_player_faction_begin_contract_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_player_faction_begin_contract_tests
             COMMAND scenario_player_faction_begin_contract_tests)

    add_executable(editor_field_header_build_tests
                   tests/editor_field_header_build_tests.cpp)
    target_link_libraries(editor_field_header_build_tests PRIVATE thandor_map_core)
    add_test(NAME editor_field_header_build_tests
             COMMAND editor_field_header_build_tests)

    add_executable(scenario_required_level_movie_contract_tests
                   tests/scenario_required_level_movie_contract_tests.cpp)
    target_link_libraries(scenario_required_level_movie_contract_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_required_level_movie_contract_tests
             COMMAND scenario_required_level_movie_contract_tests)

    add_executable(editor_resource_camera_classification_tests
                   tests/editor_resource_camera_classification_tests.cpp)
    target_link_libraries(editor_resource_camera_classification_tests PRIVATE thandor_map_core)
    add_test(NAME editor_resource_camera_classification_tests
             COMMAND editor_resource_camera_classification_tests)

    add_executable(terrain_game_ready_object_terrain_compatibility_tests
                   tests/terrain_game_ready_object_terrain_compatibility_tests.cpp)
    target_link_libraries(terrain_game_ready_object_terrain_compatibility_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_game_ready_object_terrain_compatibility_tests
             COMMAND terrain_game_ready_object_terrain_compatibility_tests)

    add_executable(rendering_dry_water_starting_resource_contract_tests
                   tests/rendering_dry_water_starting_resource_contract_tests.cpp)
    target_link_libraries(rendering_dry_water_starting_resource_contract_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_dry_water_starting_resource_contract_tests
             COMMAND rendering_dry_water_starting_resource_contract_tests)

    add_executable(terrain_resource_field_occupancy_contract_tests
                   tests/terrain_resource_field_occupancy_contract_tests.cpp)
    target_link_libraries(terrain_resource_field_occupancy_contract_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_resource_field_occupancy_contract_tests
             COMMAND terrain_resource_field_occupancy_contract_tests)

    add_executable(ai_players_ai_renderer_correspondence_tests
                   tests/ai_players_ai_renderer_correspondence_tests.cpp)
    target_link_libraries(ai_players_ai_renderer_correspondence_tests PRIVATE thandor_map_core)
    target_compile_definitions(ai_players_ai_renderer_correspondence_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME ai_players_ai_renderer_correspondence_tests
             COMMAND ai_players_ai_renderer_correspondence_tests)

    add_executable(rendering_terrain_transition_shadow_tests
                   tests/rendering_terrain_transition_shadow_tests.cpp)
    target_link_libraries(rendering_terrain_transition_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_terrain_transition_shadow_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_terrain_transition_shadow_tests
             COMMAND rendering_terrain_transition_shadow_tests)

    add_executable(terrain_exact_soil_packet_resource_field_tests
                   tests/terrain_exact_soil_packet_resource_field_tests.cpp)
    target_link_libraries(terrain_exact_soil_packet_resource_field_tests PRIVATE thandor_map_core)
    target_compile_definitions(terrain_exact_soil_packet_resource_field_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME terrain_exact_soil_packet_resource_field_tests
             COMMAND terrain_exact_soil_packet_resource_field_tests)

    add_executable(rendering_primitive_render_tests tests/rendering_primitive_render_tests.cpp)
    target_include_directories(rendering_primitive_render_tests PRIVATE src/editor)
    add_test(NAME rendering_primitive_render_tests COMMAND rendering_primitive_render_tests)

    add_executable(animation_directdraw_resource_animation_tests
                   tests/animation_directdraw_resource_animation_tests.cpp)
    target_link_libraries(animation_directdraw_resource_animation_tests PRIVATE thandor_map_core)
    target_include_directories(animation_directdraw_resource_animation_tests PRIVATE src/editor)
    add_test(NAME animation_directdraw_resource_animation_tests
             COMMAND animation_directdraw_resource_animation_tests)

    add_executable(rendering_directdraw_vertex_aura_tests
                   tests/rendering_directdraw_vertex_aura_tests.cpp)
    target_link_libraries(rendering_directdraw_vertex_aura_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_directdraw_vertex_aura_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_directdraw_vertex_aura_tests
             COMMAND rendering_directdraw_vertex_aura_tests)

    add_executable(rendering_directdraw_resource_tint_bypass_tests
                   tests/rendering_directdraw_resource_tint_bypass_tests.cpp)
    target_link_libraries(rendering_directdraw_resource_tint_bypass_tests PRIVATE thandor_map_core)
    target_include_directories(rendering_directdraw_resource_tint_bypass_tests PRIVATE src/editor)
    target_compile_definitions(rendering_directdraw_resource_tint_bypass_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_directdraw_resource_tint_bypass_tests
             COMMAND rendering_directdraw_resource_tint_bypass_tests)

    add_executable(rendering_directdraw_encoded_framebuffer_blend_tests
                   tests/rendering_directdraw_encoded_framebuffer_blend_tests.cpp)
    target_link_libraries(rendering_directdraw_encoded_framebuffer_blend_tests PRIVATE thandor_map_core)
    target_include_directories(rendering_directdraw_encoded_framebuffer_blend_tests PRIVATE src/editor)
    target_compile_definitions(rendering_directdraw_encoded_framebuffer_blend_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_directdraw_encoded_framebuffer_blend_tests
             COMMAND rendering_directdraw_encoded_framebuffer_blend_tests)

    add_executable(rendering_directdraw_resource_non_bypass_lighting_tests
                   tests/rendering_directdraw_resource_non_bypass_lighting_tests.cpp)
    target_link_libraries(rendering_directdraw_resource_non_bypass_lighting_tests PRIVATE thandor_map_core)
    target_include_directories(rendering_directdraw_resource_non_bypass_lighting_tests PRIVATE src/editor)
    target_compile_definitions(rendering_directdraw_resource_non_bypass_lighting_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_directdraw_resource_non_bypass_lighting_tests
             COMMAND rendering_directdraw_resource_non_bypass_lighting_tests)

    add_executable(rendering_directdraw_single_level_object_texture_sampling_tests
                   tests/rendering_directdraw_single_level_object_texture_sampling_tests.cpp)
    target_include_directories(rendering_directdraw_single_level_object_texture_sampling_tests
                               PRIVATE src/editor)
    target_compile_definitions(rendering_directdraw_single_level_object_texture_sampling_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_directdraw_single_level_object_texture_sampling_tests
             COMMAND rendering_directdraw_single_level_object_texture_sampling_tests)

    add_executable(animation_directdraw_water_surface_animation_color_tests
                   tests/animation_directdraw_water_surface_animation_color_tests.cpp)
    target_link_libraries(animation_directdraw_water_surface_animation_color_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_directdraw_water_surface_animation_color_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_directdraw_water_surface_animation_color_tests
             COMMAND animation_directdraw_water_surface_animation_color_tests)

    add_executable(animation_brush_water_texture_faction_animation_tests
                   tests/animation_brush_water_texture_faction_animation_tests.cpp)
    target_link_libraries(animation_brush_water_texture_faction_animation_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_brush_water_texture_faction_animation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_brush_water_texture_faction_animation_tests
             COMMAND animation_brush_water_texture_faction_animation_tests)

    add_executable(rendering_flat_water_alpha_faction_scope_tests
                   tests/rendering_flat_water_alpha_faction_scope_tests.cpp)
    target_link_libraries(rendering_flat_water_alpha_faction_scope_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_flat_water_alpha_faction_scope_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_flat_water_alpha_faction_scope_tests
             COMMAND rendering_flat_water_alpha_faction_scope_tests)

    add_executable(animation_animated_primary_surface_tests
                   tests/animation_animated_primary_surface_tests.cpp)
    target_link_libraries(animation_animated_primary_surface_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_animated_primary_surface_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_animated_primary_surface_tests
             COMMAND animation_animated_primary_surface_tests)

    add_executable(rendering_presentation_timeline_lava_effect_tests
                   tests/rendering_presentation_timeline_lava_effect_tests.cpp)
    target_link_libraries(rendering_presentation_timeline_lava_effect_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_presentation_timeline_lava_effect_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_presentation_timeline_lava_effect_tests
             COMMAND rendering_presentation_timeline_lava_effect_tests)

    add_executable(rendering_stock_lava_effect_census
                   tests/rendering_stock_lava_effect_census.cpp)
    target_link_libraries(rendering_stock_lava_effect_census PRIVATE thandor_map_core)

    add_executable(animation_view_scoped_animation_performance_tests
                   tests/animation_view_scoped_animation_performance_tests.cpp)
    target_compile_definitions(animation_view_scoped_animation_performance_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_view_scoped_animation_performance_tests
             COMMAND animation_view_scoped_animation_performance_tests)

    add_executable(assets_recursive_model_hierarchy_preview_tests
                   tests/assets_recursive_model_hierarchy_preview_tests.cpp)
    target_link_libraries(assets_recursive_model_hierarchy_preview_tests PRIVATE thandor_map_core)
    target_compile_definitions(assets_recursive_model_hierarchy_preview_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME assets_recursive_model_hierarchy_preview_tests
             COMMAND assets_recursive_model_hierarchy_preview_tests)

    add_executable(editor_stock_building_hierarchy_census
                   tests/editor_stock_building_hierarchy_census.cpp)
    target_link_libraries(editor_stock_building_hierarchy_census PRIVATE thandor_map_core)

    add_executable(assets_pck_asset_name_propagation_tests
                   tests/assets_pck_asset_name_propagation_tests.cpp)
    target_link_libraries(assets_pck_asset_name_propagation_tests PRIVATE thandor_map_core)
    target_compile_definitions(assets_pck_asset_name_propagation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME assets_pck_asset_name_propagation_tests
             COMMAND assets_pck_asset_name_propagation_tests)

    add_executable(assets_stock_asset_name_census
                   tests/assets_stock_asset_name_census.cpp)
    target_link_libraries(assets_stock_asset_name_census PRIVATE thandor_map_core)


    add_executable(assets_arm_linked_model_preview_raster_tests
                   tests/assets_arm_linked_model_preview_raster_tests.cpp)
    target_include_directories(assets_arm_linked_model_preview_raster_tests PRIVATE src/editor)
    target_link_libraries(assets_arm_linked_model_preview_raster_tests PRIVATE thandor_map_core)
    target_compile_definitions(assets_arm_linked_model_preview_raster_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME assets_arm_linked_model_preview_raster_tests
             COMMAND assets_arm_linked_model_preview_raster_tests)

    add_executable(assets_stock_linked_army_model_census
                   tests/assets_stock_linked_army_model_census.cpp)
    target_link_libraries(assets_stock_linked_army_model_census PRIVATE thandor_map_core)

    add_executable(rendering_placed_model_rotation_receiver_shadow_tests
                   tests/rendering_placed_model_rotation_receiver_shadow_tests.cpp)
    target_include_directories(rendering_placed_model_rotation_receiver_shadow_tests PRIVATE src/editor)
    target_link_libraries(rendering_placed_model_rotation_receiver_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_placed_model_rotation_receiver_shadow_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_placed_model_rotation_receiver_shadow_tests
             COMMAND rendering_placed_model_rotation_receiver_shadow_tests)

    add_executable(rendering_sprite_plane_normal_winding_tests
                   tests/rendering_sprite_plane_normal_winding_tests.cpp)
    target_link_libraries(rendering_sprite_plane_normal_winding_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_sprite_plane_normal_winding_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_sprite_plane_normal_winding_tests
             COMMAND rendering_sprite_plane_normal_winding_tests)

    add_executable(editor_game_cursor_mouse_camera_tests
                   tests/editor_game_cursor_mouse_camera_tests.cpp)
    target_include_directories(editor_game_cursor_mouse_camera_tests PRIVATE src/editor)
    target_link_libraries(editor_game_cursor_mouse_camera_tests PRIVATE thandor_map_core)
    target_compile_definitions(editor_game_cursor_mouse_camera_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_game_cursor_mouse_camera_tests
             COMMAND editor_game_cursor_mouse_camera_tests)

    add_executable(rendering_binary_generated_shadow_tests
                   tests/rendering_binary_generated_shadow_tests.cpp)
    target_link_libraries(rendering_binary_generated_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_binary_generated_shadow_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_binary_generated_shadow_tests
             COMMAND rendering_binary_generated_shadow_tests)

    add_executable(rendering_dedicated_shadow_table_tests
                   tests/rendering_dedicated_shadow_table_tests.cpp)
    target_link_libraries(rendering_dedicated_shadow_table_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_dedicated_shadow_table_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_dedicated_shadow_table_tests
             COMMAND rendering_dedicated_shadow_table_tests)

    add_executable(rendering_stock_shadow_table_census
                   tests/rendering_stock_shadow_table_census.cpp)
    target_link_libraries(rendering_stock_shadow_table_census PRIVATE thandor_map_core)

    add_executable(rendering_lev_light_shadow_direction_tests
                   tests/rendering_lev_light_shadow_direction_tests.cpp)
    target_link_libraries(rendering_lev_light_shadow_direction_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_lev_light_shadow_direction_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_lev_light_shadow_direction_tests
             COMMAND rendering_lev_light_shadow_direction_tests)

    add_executable(visibility_full_visibility_directional_lut_tests
                   tests/visibility_full_visibility_directional_lut_tests.cpp)
    target_link_libraries(visibility_full_visibility_directional_lut_tests PRIVATE thandor_map_core)
    target_compile_definitions(visibility_full_visibility_directional_lut_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_full_visibility_directional_lut_tests
             COMMAND visibility_full_visibility_directional_lut_tests)

    add_executable(assets_stock_composition_census tests/assets_stock_composition_census.cpp)
    target_include_directories(assets_stock_composition_census PRIVATE src/editor)
    target_link_libraries(assets_stock_composition_census PRIVATE thandor_map_core)
    add_test(NAME assets_stock_composition_census COMMAND assets_stock_composition_census)

    add_test(NAME editor_imgui_vulkan_api_detector_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_imgui_vulkan_api_detector_tests.py)
    set_tests_properties(editor_imgui_vulkan_api_detector_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(editor_general_camera_start_cards_tests
                   tests/editor_general_camera_start_cards_tests.cpp)
    target_link_libraries(editor_general_camera_start_cards_tests PRIVATE thandor_map_core)
    target_compile_definitions(editor_general_camera_start_cards_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_general_camera_start_cards_tests
             COMMAND editor_general_camera_start_cards_tests)

    add_executable(animation_headquarters_render_animation_tests
                   tests/animation_headquarters_render_animation_tests.cpp)
    target_include_directories(animation_headquarters_render_animation_tests PRIVATE src/editor)
    target_link_libraries(animation_headquarters_render_animation_tests PRIVATE thandor_map_core)
    target_compile_definitions(animation_headquarters_render_animation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_headquarters_render_animation_tests
             COMMAND animation_headquarters_render_animation_tests)

    add_executable(assets_stock_headquarters_census
                   tests/assets_stock_headquarters_census.cpp)
    target_include_directories(assets_stock_headquarters_census PRIVATE src/editor)
    target_link_libraries(assets_stock_headquarters_census PRIVATE thandor_map_core)
    add_test(NAME assets_stock_headquarters_census
             COMMAND assets_stock_headquarters_census)

    add_executable(rendering_effect_transition2_smoke_tests
                   tests/rendering_effect_transition2_smoke_tests.cpp)
    target_link_libraries(rendering_effect_transition2_smoke_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_effect_transition2_smoke_tests
             COMMAND rendering_effect_transition2_smoke_tests)

    add_executable(rendering_headquarters_fidelity_tests
                   tests/rendering_headquarters_fidelity_tests.cpp)
    target_include_directories(rendering_headquarters_fidelity_tests PRIVATE src/editor)
    target_link_libraries(rendering_headquarters_fidelity_tests PRIVATE thandor_map_core)
    target_compile_definitions(rendering_headquarters_fidelity_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_headquarters_fidelity_tests
             COMMAND rendering_headquarters_fidelity_tests)

    add_executable(editor_viewport_picking_brush_tests
                   tests/editor_viewport_picking_brush_tests.cpp)
    target_include_directories(editor_viewport_picking_brush_tests PRIVATE src/editor)
    target_link_libraries(editor_viewport_picking_brush_tests PRIVATE thandor_map_core)
    target_compile_definitions(editor_viewport_picking_brush_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_viewport_picking_brush_tests
             COMMAND editor_viewport_picking_brush_tests)

    add_executable(combat_simulation_weapon_selection_tests
                   tests/combat_simulation_weapon_selection_tests.cpp)
    target_include_directories(combat_simulation_weapon_selection_tests PRIVATE src/editor)
    target_link_libraries(combat_simulation_weapon_selection_tests PRIVATE thandor_map_core)
    target_compile_definitions(combat_simulation_weapon_selection_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_simulation_weapon_selection_tests
             COMMAND combat_simulation_weapon_selection_tests)

    add_executable(simlab_stock_simulation_census
                   tests/simlab_stock_simulation_census.cpp)
    target_link_libraries(simlab_stock_simulation_census PRIVATE thandor_map_core)
    add_test(NAME simlab_stock_simulation_census
             COMMAND simlab_stock_simulation_census)

    add_test(NAME combat_target_toolchain_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_target_toolchain_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(combat_target_toolchain_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME ai_knowledge_panel_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_ai_knowledge_panel_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(ai_knowledge_panel_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME build_vector_domain_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_vector_domain_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(build_vector_domain_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(rendering_simulation_static_composition_tests
                   tests/rendering_simulation_static_composition_tests.cpp)
    target_link_libraries(rendering_simulation_static_composition_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_simulation_static_composition_tests
             COMMAND rendering_simulation_static_composition_tests)

    add_test(NAME rendering_simulation_render_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_simulation_render_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(rendering_simulation_render_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(editor_contextual_order_marker_tests
                   tests/editor_contextual_order_marker_tests.cpp)
    target_link_libraries(editor_contextual_order_marker_tests PRIVATE thandor_map_core)
    target_compile_definitions(editor_contextual_order_marker_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_contextual_order_marker_tests
             COMMAND editor_contextual_order_marker_tests)

    add_executable(animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                   tests/animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests.cpp)
    target_link_libraries(animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
             COMMAND animation_persistent_drive_weapon_hierarchy_effect_unit_animation_tests)
    add_executable(animation_live_pose_effect_culling_hover_regression_tests
                   tests/animation_live_pose_effect_culling_hover_regression_tests.cpp)
    target_link_libraries(animation_live_pose_effect_culling_hover_regression_tests
                          PRIVATE thandor_map_core)
    target_include_directories(animation_live_pose_effect_culling_hover_regression_tests
                               PRIVATE src/editor)
    target_compile_definitions(animation_live_pose_effect_culling_hover_regression_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME animation_live_pose_effect_culling_hover_regression_tests
             COMMAND animation_live_pose_effect_culling_hover_regression_tests)

    add_executable(rendering_attack_runtime_rotor_shadow_pipeline_tests
                   tests/rendering_attack_runtime_rotor_shadow_pipeline_tests.cpp)
    target_link_libraries(rendering_attack_runtime_rotor_shadow_pipeline_tests
                          PRIVATE thandor_map_core)
    target_include_directories(rendering_attack_runtime_rotor_shadow_pipeline_tests
                               PRIVATE src/editor)
    target_compile_definitions(rendering_attack_runtime_rotor_shadow_pipeline_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME rendering_attack_runtime_rotor_shadow_pipeline_tests
             COMMAND rendering_attack_runtime_rotor_shadow_pipeline_tests)

    add_executable(combat_exact_shot_effect_impact_texture_capacity_tests
                   tests/combat_exact_shot_effect_impact_texture_capacity_tests.cpp)
    target_link_libraries(combat_exact_shot_effect_impact_texture_capacity_tests
                          PRIVATE thandor_map_core)
    target_include_directories(combat_exact_shot_effect_impact_texture_capacity_tests
                               PRIVATE src/editor)
    target_compile_definitions(combat_exact_shot_effect_impact_texture_capacity_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_exact_shot_effect_impact_texture_capacity_tests
             COMMAND combat_exact_shot_effect_impact_texture_capacity_tests)

    add_executable(combat_shot_axis_beam_class9_reload_tests
                   tests/combat_shot_axis_beam_class9_reload_tests.cpp)
    target_link_libraries(combat_shot_axis_beam_class9_reload_tests
                          PRIVATE thandor_map_core)
    target_include_directories(combat_shot_axis_beam_class9_reload_tests
                               PRIVATE src/editor)
    target_compile_definitions(combat_shot_axis_beam_class9_reload_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_shot_axis_beam_class9_reload_tests
             COMMAND combat_shot_axis_beam_class9_reload_tests)

    add_executable(visibility_mode3_direct_path_launch_reload_visibility_tests
                   tests/visibility_mode3_direct_path_launch_reload_visibility_tests.cpp)
    target_link_libraries(visibility_mode3_direct_path_launch_reload_visibility_tests
                          PRIVATE thandor_map_core)
    target_include_directories(visibility_mode3_direct_path_launch_reload_visibility_tests
                               PRIVATE src/editor)
    target_compile_definitions(visibility_mode3_direct_path_launch_reload_visibility_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_mode3_direct_path_launch_reload_visibility_tests
             COMMAND visibility_mode3_direct_path_launch_reload_visibility_tests)


    add_executable(combat_class9_pivot_rack_reload_tests
                   tests/combat_class9_pivot_rack_reload_tests.cpp)
    target_link_libraries(combat_class9_pivot_rack_reload_tests
                          PRIVATE thandor_map_core)
    target_include_directories(combat_class9_pivot_rack_reload_tests
                               PRIVATE src/editor)
    target_compile_definitions(combat_class9_pivot_rack_reload_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME combat_class9_pivot_rack_reload_tests
             COMMAND combat_class9_pivot_rack_reload_tests)

    add_executable(assets_export_resolver_carry_selector_retry_tests
                   tests/assets_export_resolver_carry_selector_retry_tests.cpp)
    target_link_libraries(assets_export_resolver_carry_selector_retry_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(assets_export_resolver_carry_selector_retry_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME assets_export_resolver_carry_selector_retry_tests
             COMMAND assets_export_resolver_carry_selector_retry_tests)

    add_executable(editor_semantic_export_map_editor_logic_audit_tests
                   tests/editor_semantic_export_map_editor_logic_audit_tests.cpp)
    target_link_libraries(editor_semantic_export_map_editor_logic_audit_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(editor_semantic_export_map_editor_logic_audit_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_semantic_export_map_editor_logic_audit_tests
             COMMAND editor_semantic_export_map_editor_logic_audit_tests)

    add_executable(placement_exact_placement_contact_surface_bounds_tests
                   tests/placement_exact_placement_contact_surface_bounds_tests.cpp)
    target_link_libraries(placement_exact_placement_contact_surface_bounds_tests
                          PRIVATE thandor_map_core)
    target_include_directories(placement_exact_placement_contact_surface_bounds_tests
                               PRIVATE src/editor)
    target_compile_definitions(placement_exact_placement_contact_surface_bounds_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME placement_exact_placement_contact_surface_bounds_tests
             COMMAND placement_exact_placement_contact_surface_bounds_tests)

    add_executable(terrain_semantic_field_rename_incremental_closure_tests
                   tests/terrain_semantic_field_rename_incremental_closure_tests.cpp)
    target_link_libraries(terrain_semantic_field_rename_incremental_closure_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(terrain_semantic_field_rename_incremental_closure_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME terrain_semantic_field_rename_incremental_closure_tests
             COMMAND terrain_semantic_field_rename_incremental_closure_tests)

    add_executable(research_technology_variant_asset_name_effect_pipeline_tests
                   tests/research_technology_variant_asset_name_effect_pipeline_tests.cpp)
    target_link_libraries(research_technology_variant_asset_name_effect_pipeline_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(research_technology_variant_asset_name_effect_pipeline_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME research_technology_variant_asset_name_effect_pipeline_tests
             COMMAND research_technology_variant_asset_name_effect_pipeline_tests)

    add_executable(visibility_fog_economy_integrity_damage_effect_transparency_tests
                   tests/visibility_fog_economy_integrity_damage_effect_transparency_tests.cpp)
    target_link_libraries(visibility_fog_economy_integrity_damage_effect_transparency_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(visibility_fog_economy_integrity_damage_effect_transparency_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_fog_economy_integrity_damage_effect_transparency_tests
             COMMAND visibility_fog_economy_integrity_damage_effect_transparency_tests)

    add_executable(assets_retail_pck_ui_statistic_parity_tests
                   tests/assets_retail_pck_ui_statistic_parity_tests.cpp)
    target_link_libraries(assets_retail_pck_ui_statistic_parity_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(assets_retail_pck_ui_statistic_parity_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME assets_retail_pck_ui_statistic_parity_tests
             COMMAND assets_retail_pck_ui_statistic_parity_tests)

    add_executable(research_stock_faction_technology_damage_state_census
                   tests/research_stock_faction_technology_damage_state_census.cpp)
    target_link_libraries(research_stock_faction_technology_damage_state_census
                          PRIVATE thandor_map_core)

    add_executable(research_faction_technology_damage_state_preview_tests
                   tests/research_faction_technology_damage_state_preview_tests.cpp)
    target_link_libraries(research_faction_technology_damage_state_preview_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(research_faction_technology_damage_state_preview_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME research_faction_technology_damage_state_preview_tests
             COMMAND research_faction_technology_damage_state_preview_tests)
    add_test(NAME editor_late_runtime_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_late_runtime_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_late_runtime_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(core_stock_post_state_census
                   tests/core_stock_post_state_census.cpp)
    target_link_libraries(core_stock_post_state_census PRIVATE thandor_map_core)

    add_executable(editor_post_editor_state_tests
                   tests/editor_post_editor_state_tests.cpp)
    target_link_libraries(editor_post_editor_state_tests PRIVATE thandor_map_core)
    target_compile_definitions(editor_post_editor_state_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME editor_post_editor_state_tests
             COMMAND editor_post_editor_state_tests)

    add_test(NAME build_post_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_post_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(build_post_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME assets_asset_tooltip_compact_two_column_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/assets_asset_tooltip_compact_two_column_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(assets_asset_tooltip_compact_two_column_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME editor_debug_menu_collapsible_panel_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_debug_menu_collapsible_panel_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_debug_menu_collapsible_panel_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(production_resource_fuel_energy_semantic_closure_tests
                   tests/production_resource_fuel_energy_semantic_closure_tests.cpp)
    target_link_libraries(production_resource_fuel_energy_semantic_closure_tests PRIVATE thandor_map_core)
    target_compile_definitions(production_resource_fuel_energy_semantic_closure_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME production_resource_fuel_energy_semantic_closure_tests
             COMMAND production_resource_fuel_energy_semantic_closure_tests)

    add_executable(terrain_retail_field_save_preview_tests
                   tests/terrain_retail_field_save_preview_tests.cpp)
    target_link_libraries(terrain_retail_field_save_preview_tests PRIVATE thandor_map_core)
    target_compile_definitions(terrain_retail_field_save_preview_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME terrain_retail_field_save_preview_tests
             COMMAND terrain_retail_field_save_preview_tests)

    add_test(NAME editor_gui_starting_resource_semantic_rename_compile_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_gui_starting_resource_semantic_rename_compile_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(editor_gui_starting_resource_semantic_rename_compile_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(visibility_retail_terrain_fog_surface_tests
                   tests/visibility_retail_terrain_fog_surface_tests.cpp)
    target_link_libraries(visibility_retail_terrain_fog_surface_tests PRIVATE thandor_map_core)
    target_include_directories(visibility_retail_terrain_fog_surface_tests PRIVATE src/editor)
    target_compile_definitions(visibility_retail_terrain_fog_surface_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_retail_terrain_fog_surface_tests
             COMMAND visibility_retail_terrain_fog_surface_tests)

    add_executable(visibility_fog_effect_attachment_camera_damage_tests
                   tests/visibility_fog_effect_attachment_camera_damage_tests.cpp)
    target_link_libraries(visibility_fog_effect_attachment_camera_damage_tests PRIVATE thandor_map_core)
    target_compile_definitions(visibility_fog_effect_attachment_camera_damage_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME visibility_fog_effect_attachment_camera_damage_tests
             COMMAND visibility_fog_effect_attachment_camera_damage_tests)

    add_test(NAME build_drive_rotation_destruction_smoke_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/build_drive_rotation_destruction_smoke_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(build_drive_rotation_destruction_smoke_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME assets_serialized_metadata_validation
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_serialized_asset_metadata.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(assets_serialized_metadata_validation
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
endif()

if(THANDOR_EDITOR_BUILD_APP)
    find_package(Vulkan REQUIRED)
    include(FetchContent)
    if(THANDOR_EDITOR_FETCH_DEPS)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(glfw)
        FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.90.9 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(imgui)
        set(IMGUI_DIR ${imgui_SOURCE_DIR})
    else()
        find_package(glfw3 CONFIG REQUIRED)
        if(NOT DEFINED IMGUI_DIR)
            message(FATAL_ERROR "Set IMGUI_DIR to a local Dear ImGui checkout or enable THANDOR_EDITOR_FETCH_DEPS")
        endif()
    endif()

    message(STATUS "Dear ImGui source directory: ${IMGUI_DIR}")
    if(DEFINED glfw_SOURCE_DIR)
        message(STATUS "GLFW source directory: ${glfw_SOURCE_DIR}")
    endif()
    file(STRINGS "${IMGUI_DIR}/imgui.h" THANDOR_IMGUI_VERSION_LINE REGEX "^#define IMGUI_VERSION ")
    if(THANDOR_IMGUI_VERSION_LINE)
        message(STATUS "Dear ImGui version declaration: ${THANDOR_IMGUI_VERSION_LINE}")
    endif()
    set(THANDOR_IMGUI_VULKAN_DETECTION_CMAKE "${CMAKE_CURRENT_BINARY_DIR}/thandor_imgui_vulkan_api.cmake")
    set(THANDOR_IMGUI_VULKAN_DETECTION_JSON "${CMAKE_CURRENT_BINARY_DIR}/thandor_imgui_vulkan_api.json")
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/detect_imgui_vulkan_api.py
                --header ${IMGUI_DIR}/backends/imgui_impl_vulkan.h
                --cmake-output ${THANDOR_IMGUI_VULKAN_DETECTION_CMAKE}
                --json-output ${THANDOR_IMGUI_VULKAN_DETECTION_JSON}
        RESULT_VARIABLE THANDOR_IMGUI_VULKAN_DETECTION_RESULT
        OUTPUT_VARIABLE THANDOR_IMGUI_VULKAN_DETECTION_STDOUT
        ERROR_VARIABLE THANDOR_IMGUI_VULKAN_DETECTION_STDERR)
    if(NOT THANDOR_IMGUI_VULKAN_DETECTION_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Dear ImGui Vulkan backend API detection failed for ${IMGUI_DIR}/backends/imgui_impl_vulkan.h\n"
            "${THANDOR_IMGUI_VULKAN_DETECTION_STDOUT}${THANDOR_IMGUI_VULKAN_DETECTION_STDERR}")
    endif()
    include(${THANDOR_IMGUI_VULKAN_DETECTION_CMAKE})
    message(STATUS "Dear ImGui Vulkan backend API: ${THANDOR_IMGUI_VULKAN_API_FAMILY}")
    message(STATUS "Dear ImGui Vulkan font upload API: ${THANDOR_IMGUI_VULKAN_FONT_UPLOAD_FAMILY}")
    message(STATUS "Dear ImGui Vulkan header SHA-256: ${THANDOR_IMGUI_VULKAN_HEADER_SHA256}")

    add_library(thandor_imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
        ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
    )
    target_include_directories(thandor_imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_compile_definitions(thandor_imgui PRIVATE GLFW_INCLUDE_NONE)
    target_link_libraries(thandor_imgui PUBLIC glfw Vulkan::Vulkan)
    if(THANDOR_EDITOR_BUILD_TESTS)
        add_executable(editor_theme_scale_tests tests/editor_theme_scale_tests.cpp)
        target_include_directories(editor_theme_scale_tests PRIVATE src/editor)
        target_link_libraries(editor_theme_scale_tests PRIVATE thandor_imgui)
        add_test(NAME editor_theme_scale_tests COMMAND editor_theme_scale_tests)
    endif()

    add_executable(thandor-map-editor
        src/editor/main.cpp
        src/editor/editor_app.cpp
        src/editor/editor_workspace_ui.cpp
        src/editor/editor_campaign_ui.cpp
        src/editor/editor_terrain_generator.cpp
        src/editor/editor_ui_scale.cpp
        src/editor/ai_knowledge_panel.cpp
        src/editor/vulkan_context.cpp
        src/editor/terrain_renderer.cpp
    )
    target_compile_definitions(thandor-map-editor PRIVATE
        THANDOR_UI_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf")
    add_custom_command(TARGET thandor-map-editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:thandor-map-editor>/assets/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf"
            "$<TARGET_FILE_DIR:thandor-map-editor>/assets/fonts/DejaVuSans.ttf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/LICENSE"
            "$<TARGET_FILE_DIR:thandor-map-editor>/assets/fonts/LICENSE")
    if(MSVC)
        target_sources(thandor-map-editor PRIVATE src/editor/editor.manifest)
    endif()
    target_include_directories(thandor-map-editor PRIVATE src/editor)
    target_link_libraries(thandor-map-editor PRIVATE thandor_map_core thandor_imgui glfw Vulkan::Vulkan)
    target_compile_definitions(thandor-map-editor PRIVATE GLFW_INCLUDE_NONE)
    target_compile_definitions(thandor-map-editor PRIVATE ${THANDOR_IMGUI_VULKAN_API_COMPILE_DEFINITION})
    if(THANDOR_IMGUI_VULKAN_HAS_API_VERSION)
        target_compile_definitions(thandor-map-editor PRIVATE THANDOR_IMGUI_VULKAN_HAS_API_VERSION=1)
    endif()
    if(THANDOR_IMGUI_VULKAN_MANUAL_FONT_UPLOAD)
        target_compile_definitions(thandor-map-editor PRIVATE THANDOR_IMGUI_VULKAN_MANUAL_FONT_UPLOAD=1)
    endif()
    if(MSVC)
        target_compile_options(thandor-map-editor PRIVATE /W4 /permissive-)
    else()
        target_compile_options(thandor-map-editor PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(Vulkan_GLSLC_EXECUTABLE)
        set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
        set(SHADER_NAMES directional_shadow.vert directional_shadow.frag terrain.vert terrain.frag surface.vert surface.frag water.vert water.frag markers.vert markers.frag
                         model.vert model.frag shadow.frag resource_glow.vert resource_glow.frag)
        set(SHADER_SOURCES)
        set(SHADER_BINARIES)
        foreach(SHADER IN LISTS SHADER_NAMES)
            list(APPEND SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER})
            list(APPEND SHADER_BINARIES ${SHADER_OUTPUT_DIR}/${SHADER}.spv)
        endforeach()
        add_custom_command(
            OUTPUT ${SHADER_BINARIES}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_shaders.py
                    --shader-dir ${CMAKE_CURRENT_SOURCE_DIR}/shaders
                    --output-dir ${SHADER_OUTPUT_DIR}
                    --glslc ${Vulkan_GLSLC_EXECUTABLE}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_shaders.py ${SHADER_SOURCES}
            COMMENT "Validating and compiling all GLSL shaders"
            VERBATIM)
        add_custom_target(thandor_shaders ALL DEPENDS ${SHADER_BINARIES})
        add_dependencies(thandor-map-editor thandor_shaders)
        target_compile_definitions(thandor-map-editor PRIVATE
            THANDOR_SHADER_BUILD_DIR="${SHADER_OUTPUT_DIR}"
            THANDOR_SHADER_DIR_NAME="shaders")
        if(THANDOR_EDITOR_BUILD_TESTS)
            # Explicit developer probe: real offscreen Vulkan, no window or swapchain.
            add_executable(rendering_directional_shadow_gpu_probe EXCLUDE_FROM_ALL
                           tools/rendering_directional_shadow_gpu_probe.cpp src/editor/terrain_renderer.cpp)
            target_include_directories(rendering_directional_shadow_gpu_probe PRIVATE src/editor)
            target_link_libraries(rendering_directional_shadow_gpu_probe PRIVATE thandor_map_core Vulkan::Vulkan glfw)
            target_compile_definitions(rendering_directional_shadow_gpu_probe PRIVATE GLFW_INCLUDE_NONE
                THANDOR_SHADER_BUILD_DIR="${SHADER_OUTPUT_DIR}" THANDOR_SHADER_DIR_NAME="shaders")
            add_dependencies(rendering_directional_shadow_gpu_probe thandor_shaders)
        endif()
    else()
        message(FATAL_ERROR "glslc was not found. Install the Vulkan SDK so shaders can be compiled.")
    endif()
endif()

add_executable(production_factory_production_state_tests tests/production_factory_production_state_tests.cpp)
target_link_libraries(production_factory_production_state_tests PRIVATE thandor_map_core)
add_test(NAME production_factory_production_state_tests COMMAND production_factory_production_state_tests)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(editor_map_editor_semantics_tests
                   tests/editor_map_editor_semantics_tests.cpp)
    target_link_libraries(editor_map_editor_semantics_tests PRIVATE thandor_map_core)
    add_test(NAME editor_map_editor_semantics_tests
             COMMAND editor_map_editor_semantics_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(core_runtime_closure_tests
                   tests/core_runtime_closure_tests.cpp)
    target_link_libraries(core_runtime_closure_tests PRIVATE thandor_map_core)
    add_test(NAME core_runtime_closure_tests
             COMMAND core_runtime_closure_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(scenario_level_runtime_tests
                   tests/scenario_level_runtime_tests.cpp)
    target_link_libraries(scenario_level_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_level_runtime_tests
             COMMAND scenario_level_runtime_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(placement_building_placement_effect_tests
                   tests/placement_building_placement_effect_tests.cpp)
    target_link_libraries(placement_building_placement_effect_tests PRIVATE thandor_map_core)
    add_test(NAME placement_building_placement_effect_tests
             COMMAND placement_building_placement_effect_tests)

    add_executable(ai_expansion_navigation_tests
                   tests/ai_expansion_navigation_tests.cpp)
    target_link_libraries(ai_expansion_navigation_tests PRIVATE thandor_map_core)
    add_test(NAME ai_expansion_navigation_tests
             COMMAND ai_expansion_navigation_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_test(NAME placement_retail_placement_effect_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/placement_retail_placement_effect_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(terrain_cp12_authored_field_reference_tests
                   tests/terrain_cp12_authored_field_reference_tests.cpp)
    target_link_libraries(terrain_cp12_authored_field_reference_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_cp12_authored_field_reference_tests
             COMMAND terrain_cp12_authored_field_reference_tests)

    add_test(NAME rendering_cp12_runtime_texture_residency_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_cp12_runtime_texture_residency_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_test(NAME editor_cp12_family8_ui_swatch_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_cp12_family8_ui_swatch_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(scenario_cp13_faction_family_color_tests
                   tests/scenario_cp13_faction_family_color_tests.cpp)
    target_link_libraries(scenario_cp13_faction_family_color_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_cp13_faction_family_color_tests
             COMMAND scenario_cp13_faction_family_color_tests)
    add_test(NAME scenario_cp13_faction_family_color_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario_cp13_faction_family_color_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(scenario_cp14_single_player_faction_identity_tests
                   tests/scenario_cp14_single_player_faction_identity_tests.cpp)
    target_link_libraries(scenario_cp14_single_player_faction_identity_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_cp14_single_player_faction_identity_tests
             COMMAND scenario_cp14_single_player_faction_identity_tests)
    add_test(NAME scenario_cp14_single_player_faction_ui_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario_cp14_single_player_faction_ui_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(simlab_research_catalog_audit EXCLUDE_FROM_ALL tools/simlab_research_catalog_audit.cpp)
    add_executable(visibility_movement_visibility_tests tests/visibility_movement_visibility_tests.cpp)
    target_link_libraries(visibility_movement_visibility_tests PRIVATE thandor_map_core)
    add_test(NAME visibility_movement_visibility_tests COMMAND visibility_movement_visibility_tests)
    target_link_libraries(simlab_research_catalog_audit PRIVATE thandor_map_core)
    add_executable(research_planning_tests tests/research_planning_tests.cpp)
    target_link_libraries(research_planning_tests PRIVATE thandor_map_core)
    add_test(NAME research_planning_tests COMMAND research_planning_tests)
    add_executable(ai_military_runtime_tests tests/ai_military_runtime_tests.cpp)
    target_link_libraries(ai_military_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_runtime_tests COMMAND ai_military_runtime_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(combat_collision_presentation_tests tests/combat_collision_presentation_tests.cpp)
    target_link_libraries(combat_collision_presentation_tests PRIVATE thandor_map_core)
    add_test(NAME combat_collision_presentation_tests COMMAND combat_collision_presentation_tests)
endif()
add_executable(thandor_v622_stock_collision_ui_probe tools/stock_collision_ui_probe.cpp)
target_link_libraries(thandor_v622_stock_collision_ui_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(rendering_selection_tint_tests tests/rendering_selection_tint_tests.cpp)
    target_link_libraries(rendering_selection_tint_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_selection_tint_tests COMMAND rendering_selection_tint_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(placement_factory_exit_and_region_tests tests/placement_factory_exit_and_region_tests.cpp)
    target_link_libraries(placement_factory_exit_and_region_tests PRIVATE thandor_map_core)
    if(NOT MSVC)
        target_compile_options(placement_factory_exit_and_region_tests PRIVATE -fwrapv)
    endif()
    add_test(NAME placement_factory_exit_and_region_tests COMMAND placement_factory_exit_and_region_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(placement_inputs_tests tests/placement_inputs_tests.cpp)
    target_link_libraries(placement_inputs_tests PRIVATE thandor_map_core)
    if(NOT MSVC)
        target_compile_options(placement_inputs_tests PRIVATE -fwrapv)
    endif()
    add_test(NAME placement_inputs_tests COMMAND placement_inputs_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(production_callback_timing_energy_tests tests/production_callback_timing_energy_tests.cpp)
    target_link_libraries(production_callback_timing_energy_tests PRIVATE thandor_map_core)
    add_test(NAME production_callback_timing_energy_tests COMMAND production_callback_timing_energy_tests)
    add_executable(placement_extended_placement_tests tests/placement_extended_placement_tests.cpp)
    target_link_libraries(placement_extended_placement_tests PRIVATE thandor_map_core)
    if(NOT MSVC)
        target_compile_options(placement_extended_placement_tests PRIVATE -fwrapv)
    endif()
    add_test(NAME placement_extended_placement_tests COMMAND placement_extended_placement_tests)
    add_executable(combat_projectile_runtime_tests tests/combat_projectile_runtime_tests.cpp)
    target_link_libraries(combat_projectile_runtime_tests PRIVATE thandor_map_core)
    if(NOT MSVC)
        target_compile_options(combat_projectile_runtime_tests PRIVATE -fwrapv)
    endif()
    add_test(NAME combat_projectile_runtime_tests COMMAND combat_projectile_runtime_tests)
endif()

# Bounded stock-asset diagnostic; supply GAME_ROOT explicitly when running.
add_executable(thandor_v628_stock_extended_probe tools/stock_extended_probe.cpp)
target_link_libraries(thandor_v628_stock_extended_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(combat_collision_lifetime_tests tests/combat_collision_lifetime_tests.cpp)
    target_link_libraries(combat_collision_lifetime_tests PRIVATE thandor_map_core)
    add_test(NAME combat_collision_lifetime_tests COMMAND combat_collision_lifetime_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(simlab_worker_lifetime_tests tests/simlab_worker_lifetime_tests.cpp)
    target_link_libraries(simlab_worker_lifetime_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_worker_lifetime_tests COMMAND simlab_worker_lifetime_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(simlab_performance_diagnostics_tests tests/simlab_performance_diagnostics_tests.cpp)
    target_link_libraries(simlab_performance_diagnostics_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_performance_diagnostics_tests COMMAND simlab_performance_diagnostics_tests)
    set_tests_properties(simlab_performance_diagnostics_tests PROPERTIES TIMEOUT 15)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(editor_command_marker_stream_tests tests/editor_command_marker_stream_tests.cpp)
    target_link_libraries(editor_command_marker_stream_tests PRIVATE thandor_map_core)
    add_test(NAME editor_command_marker_stream_tests COMMAND editor_command_marker_stream_tests)
    set_tests_properties(editor_command_marker_stream_tests PROPERTIES TIMEOUT 15)
endif()

if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND NOT MSVC)
    add_test(NAME recording_recorder_reader_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_recorder_reader_tests.py)
endif()

add_executable(thandor_v647_legacy_state_probe tools/legacy_state_replay_probe.cpp)
target_link_libraries(thandor_v647_legacy_state_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(core_legacy_parity_tests tests/core_legacy_parity_tests.cpp)
    target_link_libraries(core_legacy_parity_tests PRIVATE thandor_map_core)
    add_test(NAME core_legacy_parity_tests COMMAND core_legacy_parity_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME scenario_legacy_admission_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/scenario_legacy_admission_tests.py)
endif()

add_executable(thandor_v648_slope_probe tools/slope_probe.cpp)
target_link_libraries(thandor_v648_slope_probe PRIVATE thandor_map_core)
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(core_deep_parity_tests tests/core_deep_parity_tests.cpp)
    target_link_libraries(core_deep_parity_tests PRIVATE thandor_map_core)
    add_test(NAME core_deep_parity_tests COMMAND core_deep_parity_tests)
    add_executable(terrain_lighting_simulation_tests tests/terrain_lighting_simulation_tests.cpp)
    target_link_libraries(terrain_lighting_simulation_tests PRIVATE thandor_map_core)
    add_test(NAME terrain_lighting_simulation_tests COMMAND terrain_lighting_simulation_tests)
endif()
add_executable(thandor_v648_stock_airport_probe tools/stock_airport_probe.cpp)
target_link_libraries(thandor_v648_stock_airport_probe PRIVATE thandor_map_core)

add_executable(thandor_v648_legacy_state_probe tools/legacy_faction_pressure_probe.cpp)
target_link_libraries(thandor_v648_legacy_state_probe PRIVATE thandor_map_core)

set(THANDOR_NATIVE_SOURCE_ROOT "" CACHE PATH "Optional supplied open-thandor source for native differential oracles")
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME core_native_differential_oracles
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_oracles.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_oracles_v648)
    set_tests_properties(core_native_differential_oracles PROPERTIES TIMEOUT 60)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_purchase_workspace_tests tests/ai_purchase_workspace_tests.cpp)
    target_link_libraries(ai_purchase_workspace_tests PRIVATE thandor_map_core)
    add_test(NAME ai_purchase_workspace_tests COMMAND ai_purchase_workspace_tests)
endif()

add_executable(thandor_v649_legacy_purchase_probe tools/legacy_purchase_queue_probe.cpp)
target_link_libraries(thandor_v649_legacy_purchase_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME ai_native_purchase_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_purchase.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_purchase_v649)
    set_tests_properties(ai_native_purchase_oracle PROPERTIES TIMEOUT 90)
endif()

if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME core_native_timeline_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/core_native_timeline_tests.py)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(aviation_airport_alert_tests tests/aviation_airport_alert_tests.cpp)
    target_link_libraries(aviation_airport_alert_tests PRIVATE thandor_map_core)
    add_test(NAME aviation_airport_alert_tests COMMAND aviation_airport_alert_tests)
    add_executable(ai_exploration_tests tests/ai_exploration_tests.cpp)
    target_link_libraries(ai_exploration_tests PRIVATE thandor_map_core)
    add_test(NAME ai_exploration_tests COMMAND ai_exploration_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME aviation_native_airport_alert_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_airport_alert.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_alert_v650)
    set_tests_properties(aviation_native_airport_alert_oracle PROPERTIES TIMEOUT 90)
endif()

add_executable(thandor_v650_stock_airport_alert_probe tools/stock_airport_alert_probe.cpp)
target_link_libraries(thandor_v650_stock_airport_alert_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(placement_special_site_tests tests/placement_special_site_tests.cpp)
    target_link_libraries(placement_special_site_tests PRIVATE thandor_map_core)
    add_test(NAME placement_special_site_tests COMMAND placement_special_site_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME placement_native_special_site_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_special_site.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/native_special_site_v651)
    set_tests_properties(placement_native_special_site_oracle PROPERTIES TIMEOUT 90)
endif()

add_executable(thandor_v651_legacy_purchase_probe tools/legacy_special_site_purchase_probe.cpp)
target_link_libraries(thandor_v651_legacy_purchase_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(research_workspace_tests tests/research_workspace_tests.cpp)
    target_link_libraries(research_workspace_tests PRIVATE thandor_map_core)
    add_test(NAME research_workspace_tests COMMAND research_workspace_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME research_native_research_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_research.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_research_v652)
    set_tests_properties(research_native_research_oracle PROPERTIES TIMEOUT 90)
endif()

add_executable(thandor_v652_legacy_purchase_probe tools/legacy_research_seed_probe.cpp)
target_link_libraries(thandor_v652_legacy_purchase_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(production_resource_owner_tests tests/production_resource_owner_tests.cpp)
    target_link_libraries(production_resource_owner_tests PRIVATE thandor_map_core)
    add_test(NAME production_resource_owner_tests COMMAND production_resource_owner_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME placement_native_resource_placement_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_resource_placement.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_resource_placement_v653)
    set_tests_properties(placement_native_resource_placement_oracle PROPERTIES TIMEOUT 90)
endif()
add_executable(thandor_v653_legacy_purchase_probe tools/legacy_resource_placement_probe.cpp)
target_link_libraries(thandor_v653_legacy_purchase_probe PRIVATE thandor_map_core)
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(simlab_worker_wakeup_tests tests/simlab_worker_wakeup_tests.cpp)
    target_link_libraries(simlab_worker_wakeup_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_worker_wakeup_tests COMMAND simlab_worker_wakeup_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(scenario_level_condition_tests tests/scenario_level_condition_tests.cpp)
    target_link_libraries(scenario_level_condition_tests PRIVATE thandor_map_core)
    add_test(NAME scenario_level_condition_tests COMMAND scenario_level_condition_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME scenario_native_condition_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_conditions.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_conditions_v654)
    set_tests_properties(scenario_native_condition_oracle PROPERTIES TIMEOUT 90)
endif()
add_executable(thandor_v654_legacy_purchase_probe tools/legacy_placement_relation_guard_probe.cpp)
target_link_libraries(thandor_v654_legacy_purchase_probe PRIVATE thandor_map_core)

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(research_lifecycle_tests tests/research_lifecycle_tests.cpp)
    target_link_libraries(research_lifecycle_tests PRIVATE thandor_map_core)
    add_test(NAME research_lifecycle_tests COMMAND research_lifecycle_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME research_native_research_lifecycle_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_research_lifecycle.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_research_lifecycle_v655)
    set_tests_properties(research_native_research_lifecycle_oracle PROPERTIES TIMEOUT 90)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(thandor_v656_legacy_purchase_probe tools/legacy_relation_and_emitter_probe.cpp)
    target_link_libraries(thandor_v656_legacy_purchase_probe PRIVATE thandor_map_core)
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME diplomacy_native_relations_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_relations.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_relations_v656)
    set_tests_properties(diplomacy_native_relations_oracle PROPERTIES TIMEOUT 90)
endif()

if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME rendering_native_emitter_gate_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_emitter_gate.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/native_emitter_gate_v656)
    set_tests_properties(rendering_native_emitter_gate_oracle PROPERTIES TIMEOUT 90)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(rendering_timed_callback_tests tests/rendering_timed_callback_tests.cpp)
    target_link_libraries(rendering_timed_callback_tests PRIVATE thandor_map_core)
    add_test(NAME rendering_timed_callback_tests COMMAND rendering_timed_callback_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(thandor_v657_tec201_forensic_probe tools/tec201_forensic_probe.cpp)
    target_link_libraries(thandor_v657_tec201_forensic_probe PRIVATE thandor_map_core)
    add_executable(ai_planner_dispatch_filter_tests tests/ai_planner_dispatch_filter_tests.cpp)
    target_link_libraries(ai_planner_dispatch_filter_tests PRIVATE thandor_map_core)
    add_test(NAME ai_planner_dispatch_filter_tests COMMAND ai_planner_dispatch_filter_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME recording_planner_argument_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_planner_argument_tests.py)
    endif()
endif()

# Keep diagnostic targets explicitly buildable, without rebuilding dozens of
# historical probes for an ordinary editor/test build.
add_executable(render_texture_residency_audit tools/render_texture_residency_audit.cpp)
target_link_libraries(render_texture_residency_audit PRIVATE thandor_map_core)
add_executable(emitter_owner_order_probe tools/emitter_owner_order_probe.cpp)
target_link_libraries(emitter_owner_order_probe PRIVATE thandor_map_core)
add_executable(simlab_worker_benchmark tools/simlab_worker_benchmark.cpp)
target_link_libraries(simlab_worker_benchmark PRIVATE thandor_map_core)
# Optional asset-backed native walker comparison; ordinary builds need no PCK input.
set(THANDOR_STOCK_ASSET_ROOT "" CACHE PATH "Optional stock game/PCK root for asset-backed validation")
# Stock PCK evidence tests do not require game.exe. The user supplies a directory
# with the original archives; no archive is included in source releases.
if(THANDOR_EDITOR_BUILD_TESTS AND THANDOR_STOCK_ASSET_ROOT)
    add_executable(combat_stock_pck_repair_tests tests/combat_stock_pck_repair_tests.cpp)
    target_link_libraries(combat_stock_pck_repair_tests PRIVATE thandor_map_core)
    add_test(NAME combat_stock_pck_repair_tests
             COMMAND combat_stock_pck_repair_tests ${THANDOR_STOCK_ASSET_ROOT})
    add_executable(combat_stock_pck_destruction_tests tests/combat_stock_pck_destruction_tests.cpp)
    target_link_libraries(combat_stock_pck_destruction_tests PRIVATE thandor_map_core)
    add_test(NAME combat_stock_pck_destruction_tests
             COMMAND combat_stock_pck_destruction_tests ${THANDOR_STOCK_ASSET_ROOT})
endif()
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND THANDOR_STOCK_ASSET_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME animation_native_walker_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_walker.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR} ${THANDOR_STOCK_ASSET_ROOT}
            ${CMAKE_CURRENT_BINARY_DIR}/native_walker)
    set_tests_properties(animation_native_walker_oracle PROPERTIES TIMEOUT 120)
endif()
get_property(thandor_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
if(NOT THANDOR_EDITOR_BUILD_PROBES)
    foreach(target IN LISTS thandor_targets)
        get_target_property(target_sources ${target} SOURCES)
        get_target_property(target_type ${target} TYPE)
        if(target_type STREQUAL "EXECUTABLE" AND "${target_sources}" MATCHES "(^|;)tools/")
            set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endif()
    endforeach()
endif()
if(THANDOR_EDITOR_BUILD_TESTS)
    get_property(thandor_tests DIRECTORY PROPERTY TESTS)
    foreach(test IN LISTS thandor_tests)
        string(REGEX REPLACE "_.*" "" submodule "${test}")
        set_property(TEST ${test} APPEND PROPERTY LABELS "${submodule}")
    endforeach()
endif()

# V680 performance/backpressure contract: bounded queue with explicit acceptance.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(simlab_worker_backpressure_tests tests/simlab_worker_backpressure_tests.cpp)
    target_link_libraries(simlab_worker_backpressure_tests PRIVATE thandor_map_core)
    add_test(NAME simlab_worker_backpressure_tests COMMAND simlab_worker_backpressure_tests)
    set_tests_properties(simlab_worker_backpressure_tests PROPERTIES TIMEOUT 15)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_test(NAME performance_compare_tests COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/performance_compare_tests.py)
endif()

# V689: each in-flight swapchain image owns its depth and present semaphore.
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME rendering_swapchain_image_lifetime_source_contract_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_swapchain_image_lifetime_source_contract_tests.py)
endif()
# V689: only the selected Simulation Lab run uses the full 3D rendering path.
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME rendering_simulation_run_switcher_source_contract_tests
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_simulation_run_switcher_source_contract_tests.py)
endif()

# V702: scene-scoped cached faction material lookups avoid per-actor key allocation.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_test(NAME rendering_scene_family_fast_lookup_source_contract_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_scene_family_fast_lookup_source_contract_tests.py
            --source ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_effect_terrain_v738_tests tests/ai_effect_terrain_v738_tests.cpp)
    target_link_libraries(ai_effect_terrain_v738_tests PRIVATE thandor_map_core)
    add_test(NAME ai_effect_terrain_v738_tests COMMAND ai_effect_terrain_v738_tests)
endif()

# V741: native 32-bit workspace-05 scoring and full row scan.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_workspace05_score_v741_tests tests/ai_workspace05_score_v741_tests.cpp)
    target_include_directories(ai_workspace05_score_v741_tests PRIVATE include)
    add_test(NAME ai_workspace05_score_v741_tests COMMAND ai_workspace05_score_v741_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ai_v741_scope_contract_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_v741_scope_contract_tests.py)
    endif()
endif()

# V742: military AI dispatch must use bounded, newest-first native owner WS01.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_military_workspace_dispatch_v742_tests
        tests/ai_military_workspace_dispatch_v742_tests.cpp)
    target_link_libraries(ai_military_workspace_dispatch_v742_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_workspace_dispatch_v742_tests
        COMMAND ai_military_workspace_dispatch_v742_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ai_v742_dispatch_source_contract_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_v742_dispatch_source_contract_tests.py)
    endif()
endif()

# V743: Pioneer AI consumes bounded WS01, but its model movement continues.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_pioneer_workspace_dispatch_v743_tests
        tests/ai_pioneer_workspace_dispatch_v743_tests.cpp)
    target_link_libraries(ai_pioneer_workspace_dispatch_v743_tests PRIVATE thandor_map_core)
    add_test(NAME ai_pioneer_workspace_dispatch_v743_tests
        COMMAND ai_pioneer_workspace_dispatch_v743_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ai_v743_pioneer_scope_contract_tests
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_v743_pioneer_scope_contract_tests.py)
    endif()
endif()

# Routed AI workspace/anchor commands resolve and publish native route state at issue time.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_routed_command_tests tests/movement_routed_command_tests.cpp)
    target_link_libraries(movement_routed_command_tests PRIVATE thandor_map_core)
    add_test(NAME movement_routed_command_tests COMMAND movement_routed_command_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME movement_routed_command_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_routed_command_source_contract_tests.py
                --source ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# V746: class-18 uses the canonical WS00/04 image and complete live site admission.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_pioneer_workspace_counts_v746_tests
        tests/ai_pioneer_workspace_counts_v746_tests.cpp)
    target_link_libraries(ai_pioneer_workspace_counts_v746_tests PRIVATE thandor_map_core)
    add_test(NAME ai_pioneer_workspace_counts_v746_tests
        COMMAND ai_pioneer_workspace_counts_v746_tests)

    add_executable(placement_pioneer_live_admission_v746_tests
        tests/placement_pioneer_live_admission_v746_tests.cpp)
    target_link_libraries(placement_pioneer_live_admission_v746_tests PRIVATE thandor_map_core)
    add_test(NAME placement_pioneer_live_admission_v746_tests
        COMMAND placement_pioneer_live_admission_v746_tests)

    add_executable(ai_workspace_capacity_v746_tests
        tests/ai_workspace_capacity_v746_tests.cpp)
    target_link_libraries(ai_workspace_capacity_v746_tests PRIVATE thandor_map_core)
    add_test(NAME ai_workspace_capacity_v746_tests
        COMMAND ai_workspace_capacity_v746_tests)

    add_executable(movement_group_target_route_v746_tests
        tests/movement_group_target_route_v746_tests.cpp)
    target_link_libraries(movement_group_target_route_v746_tests PRIVATE thandor_map_core)
    add_test(NAME movement_group_target_route_v746_tests
        COMMAND movement_group_target_route_v746_tests)

    # V748: complete the source-backed move-writer inventory used by factory
    # exits and player selections, preserve exact AI placement headings, and
    # record a controlled TEC201 assignment/payment/completion lifecycle.
    add_executable(movement_factory_auxiliary_route_v747_tests
        tests/movement_factory_auxiliary_route_v747_tests.cpp)
    target_link_libraries(movement_factory_auxiliary_route_v747_tests PRIVATE thandor_map_core)
    add_test(NAME movement_factory_auxiliary_route_v747_tests
        COMMAND movement_factory_auxiliary_route_v747_tests)

    add_executable(movement_selection_commands_v747_tests
        tests/movement_selection_commands_v747_tests.cpp)
    target_link_libraries(movement_selection_commands_v747_tests PRIVATE thandor_map_core)
    add_test(NAME movement_selection_commands_v747_tests
        COMMAND movement_selection_commands_v747_tests)

    add_executable(placement_ai_factory_orientation_v747_tests
        tests/placement_ai_factory_orientation_v747_tests.cpp)
    target_link_libraries(placement_ai_factory_orientation_v747_tests PRIVATE thandor_map_core)
    add_test(NAME placement_ai_factory_orientation_v747_tests
        COMMAND placement_ai_factory_orientation_v747_tests)

    add_executable(research_tec201_lifecycle_v747_tests
        tests/research_tec201_lifecycle_v747_tests.cpp)
    target_link_libraries(research_tec201_lifecycle_v747_tests PRIVATE thandor_map_core)
    add_test(NAME research_tec201_lifecycle_v747_tests
        COMMAND research_tec201_lifecycle_v747_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME movement_writer_inventory_v747_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_writer_inventory_v747_source_contract_tests.py
                    --source ${CMAKE_CURRENT_SOURCE_DIR})
        add_test(NAME ai_v746_pioneer_admission_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_v746_pioneer_admission_source_contract_tests.py)
    endif()
endif()

# V751: a diplomacy merge repaints live authored models from the surviving runtime
# faction, and runtime shadow templates disappear with their destroyed owner.
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME rendering_runtime_faction_merge_shadow_v751_source_contract_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/rendering_runtime_faction_merge_shadow_v751_source_contract_tests.py
            --source ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# V751: retail V1 AIPH cadence honors TICK.humanFactionMask. Faction 1/Ares
# is excluded when bit 1 is set; ACTR's known-bad owner labels remain unused.
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME recording_v751_ai_oracle_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_v751_ai_oracle_tests.py)
endif()

# V752: combat-target lifetime/ordering, capability admission, bounded AI
# workspaces and active-faction planning follow the recovered owner-list image.
if(THANDOR_EDITOR_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME combat_workspace_v752_source_contract_tests
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_workspace_v752_source_contract_tests.py)
endif()

# V753: recovered player selection target-army/target-position writers and
# captured world-owner callback mutation scheduling.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_selection_target_commands_v753_tests
        tests/movement_selection_target_commands_v753_tests.cpp)
    target_link_libraries(movement_selection_target_commands_v753_tests PRIVATE thandor_map_core)
    add_test(NAME movement_selection_target_commands_v753_tests
        COMMAND movement_selection_target_commands_v753_tests)

    add_executable(ai_owner_callback_schedule_v753_tests
        tests/ai_owner_callback_schedule_v753_tests.cpp)
    target_link_libraries(ai_owner_callback_schedule_v753_tests PRIVATE thandor_map_core)
    add_test(NAME ai_owner_callback_schedule_v753_tests
        COMMAND ai_owner_callback_schedule_v753_tests)

    if(Python3_Interpreter_FOUND)
        add_test(NAME combat_owner_selection_v753_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/combat_owner_selection_v753_source_contract_tests.py)
    endif()
endif()

# V754: native bounded overlapping-group route priority image and represented
# issue-time coordination for grouped military commands.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_overlapping_group_route_v754_tests
        tests/movement_overlapping_group_route_v754_tests.cpp)
    target_link_libraries(movement_overlapping_group_route_v754_tests PRIVATE thandor_map_core)
    add_test(NAME movement_overlapping_group_route_v754_tests
        COMMAND movement_overlapping_group_route_v754_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME movement_overlapping_group_route_v754_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_overlapping_group_route_v754_source_contract_tests.py
                --source ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# V755: bounded military command-issue evidence and streaming retail command audit.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_military_order_trace_v755_tests
        tests/ai_military_order_trace_v755_tests.cpp)
    target_link_libraries(ai_military_order_trace_v755_tests PRIVATE thandor_map_core)
    add_test(NAME ai_military_order_trace_v755_tests
        COMMAND ai_military_order_trace_v755_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ai_military_order_trace_v755_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_military_order_trace_v755_source_contract_tests.py)
        add_test(NAME recording_v755_command_stream_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/recording_v755_command_stream_tests.py)
    endif()
endif()

# V756: static authored MODEL owners use full ArmyRuntime-style hierarchy
# aggregates, and static/live owner callbacks share native head-insertion order.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(ai_static_owner_aggregate_v756_tests
        tests/ai_static_owner_aggregate_v756_tests.cpp)
    target_link_libraries(ai_static_owner_aggregate_v756_tests PRIVATE thandor_map_core)
    add_test(NAME ai_static_owner_aggregate_v756_tests
        COMMAND ai_static_owner_aggregate_v756_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME ai_static_owner_aggregate_v756_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/ai_static_owner_aggregate_v756_source_contract_tests.py
                --source ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# V757: overlapping-group route admission follows the selected MDL
# accelerationPerTick gate, and linked unfinished MODEL owners remain in the
# issue-time obstacle image until their root owner is actually unlinked.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_overlapping_group_owner_admission_v757_tests
        tests/movement_overlapping_group_owner_admission_v757_tests.cpp)
    target_link_libraries(movement_overlapping_group_owner_admission_v757_tests PRIVATE thandor_map_core)
    add_test(NAME movement_overlapping_group_owner_admission_v757_tests
        COMMAND movement_overlapping_group_owner_admission_v757_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME movement_overlapping_group_owner_admission_v757_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_overlapping_group_owner_admission_v757_source_contract_tests.py
                --source ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# V758: runtime class 17 banking movement consumes its current route point
# when the callback travel reaches half the remaining distance, without
# changing the full-distance arrival rule of other constant-speed classes.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_native_walker_tests tests/movement_native_walker_tests.cpp)
    target_link_libraries(movement_native_walker_tests PRIVATE thandor_map_core)
    add_test(NAME movement_native_walker_tests COMMAND movement_native_walker_tests)

    add_executable(movement_class17_banking_arrival_v758_tests
        tests/movement_class17_banking_arrival_v758_tests.cpp)
    target_link_libraries(movement_class17_banking_arrival_v758_tests PRIVATE thandor_map_core)
    add_test(NAME movement_class17_banking_arrival_v758_tests
        COMMAND movement_class17_banking_arrival_v758_tests)
    if(Python3_Interpreter_FOUND)
        add_test(NAME movement_class17_banking_arrival_v758_source_contract_tests
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/movement_class17_banking_arrival_v758_source_contract_tests.py
                ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# Native codec negative cases, through the editor's public PCK archive boundary.
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(assets_native_codec_guard_tests tests/assets_native_codec_guard_tests.cpp)
    target_link_libraries(assets_native_codec_guard_tests PRIVATE thandor_map_core)
    add_test(NAME assets_native_codec_guard_tests COMMAND assets_native_codec_guard_tests)
endif()
if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(assets_native_pcx_pattern_tests tests/assets_native_pcx_pattern_tests.cpp)
    target_link_libraries(assets_native_pcx_pattern_tests PRIVATE thandor_map_core)
    add_test(NAME assets_native_pcx_pattern_tests COMMAND assets_native_pcx_pattern_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_border_reachability_v775_tests tests/movement_border_reachability_v775_tests.cpp)
    target_link_libraries(movement_border_reachability_v775_tests PRIVATE thandor_map_core)
    add_test(NAME movement_border_reachability_v775_tests COMMAND movement_border_reachability_v775_tests)
    if(Python3_Interpreter_FOUND AND THANDOR_NATIVE_SOURCE_ROOT AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME movement_native_reachability_oracle
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/verify_native_reachability.py
            ${THANDOR_NATIVE_SOURCE_ROOT} ${CMAKE_CURRENT_BINARY_DIR})
        set_tests_properties(movement_native_reachability_oracle PROPERTIES TIMEOUT 120)
    endif()
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(model_geometry_world_order_v776_tests tests/model_geometry_world_order_v776_tests.cpp)
    target_link_libraries(model_geometry_world_order_v776_tests PRIVATE thandor_map_core)
    add_test(NAME model_geometry_world_order_v776_tests COMMAND model_geometry_world_order_v776_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(production_build_energy_v777_tests tests/production_build_energy_v777_tests.cpp)
    target_link_libraries(production_build_energy_v777_tests PRIVATE thandor_map_core)
    add_test(NAME production_build_energy_v777_tests COMMAND production_build_energy_v777_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(movement_ai_path_coordinates_v777_tests tests/movement_ai_path_coordinates_v777_tests.cpp)
    target_link_libraries(movement_ai_path_coordinates_v777_tests PRIVATE thandor_map_core)
    add_test(NAME movement_ai_path_coordinates_v777_tests COMMAND movement_ai_path_coordinates_v777_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(model_dismantle_lifecycle_tests tests/model_dismantle_lifecycle_tests.cpp)
    target_link_libraries(model_dismantle_lifecycle_tests PRIVATE thandor_map_core)
    add_test(NAME model_dismantle_lifecycle_tests COMMAND model_dismantle_lifecycle_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(editor_resource_flag_validation_tests tests/editor_resource_flag_validation_tests.cpp)
    target_include_directories(editor_resource_flag_validation_tests PRIVATE src/editor)
    target_link_libraries(editor_resource_flag_validation_tests PRIVATE thandor_map_core)
    add_test(NAME editor_resource_flag_validation_tests COMMAND editor_resource_flag_validation_tests)
    add_executable(editor_faction_text_contrast_tests tests/editor_faction_text_contrast_tests.cpp)
    target_include_directories(editor_faction_text_contrast_tests PRIVATE src/editor)
    target_link_libraries(editor_faction_text_contrast_tests PRIVATE thandor_map_core)
    add_test(NAME editor_faction_text_contrast_tests COMMAND editor_faction_text_contrast_tests)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    add_executable(editor_ai_profiles_tests tests/editor_ai_profiles_tests.cpp)
    target_include_directories(editor_ai_profiles_tests PRIVATE src/editor)
    target_link_libraries(editor_ai_profiles_tests PRIVATE thandor_map_core)
    add_test(NAME editor_ai_profiles_tests COMMAND editor_ai_profiles_tests)
endif()
