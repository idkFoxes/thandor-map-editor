cmake_minimum_required(VERSION 3.24)
project(ThandorMapEditor LANGUAGES CXX)

option(THANDOR_EDITOR_BUILD_APP "Build the Vulkan/ImGui editor" ON)
option(THANDOR_EDITOR_BUILD_TESTS "Build core format tests" ON)
option(THANDOR_EDITOR_FETCH_DEPS "Fetch GLFW and Dear ImGui when not supplied" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(thandor_map_core
    src/core/binary.cpp
    src/core/pck.cpp
    src/core/field.cpp
    src/core/field_raycast.cpp
    src/core/fog_preview.cpp
    src/core/level.cpp
    src/core/scenario_catalog.cpp
    src/core/text_resource.cpp
    src/core/technology.cpp
    src/core/gfx.cpp
    src/core/terrain_texture.cpp
    src/core/terrain_shadow.cpp
    src/core/model.cpp
    src/core/effect.cpp
    src/core/simulation.cpp
    src/core/catalog.cpp
    src/core/map_document.cpp
)
target_include_directories(thandor_map_core PUBLIC include)
if(MSVC)
    target_compile_options(thandor_map_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(thandor_map_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

if(THANDOR_EDITOR_BUILD_TESTS)
    enable_testing()
    add_executable(thandor_g393_build_technology_tracks_radar_fog_tests
                   tests/g393_build_technology_tracks_radar_fog_tests.cpp)
    target_link_libraries(thandor_g393_build_technology_tracks_radar_fog_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g393_build_technology_tracks_radar_fog_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g393_build_technology_tracks_radar_fog_tests
             COMMAND thandor_g393_build_technology_tracks_radar_fog_tests)
    add_test(NAME thandor_g393_editor_source_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g393_editor_source_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g393_editor_source_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME thandor_walker_preview_callsite_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/walker_preview_callsite_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_walker_preview_callsite_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    add_test(NAME thandor_g395_editor_regression_contract_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g395_editor_regression_contract_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g395_editor_regression_contract_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(thandor_g397_visible_building_animation_rate_tests
                   tests/g397_visible_building_animation_rate_tests.cpp)
    target_link_libraries(thandor_g397_visible_building_animation_rate_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g397_visible_building_animation_rate_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g397_visible_building_animation_rate_tests
             COMMAND thandor_g397_visible_building_animation_rate_tests)

    add_executable(thandor_g398_game_runtime_building_animation_pipeline_tests
                   tests/g398_game_runtime_building_animation_pipeline_tests.cpp)
    target_link_libraries(thandor_g398_game_runtime_building_animation_pipeline_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g398_game_runtime_building_animation_pipeline_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g398_game_runtime_building_animation_pipeline_tests
             COMMAND thandor_g398_game_runtime_building_animation_pipeline_tests)

    add_executable(thandor_g391_tank_track_articulated_walker_unit_class_tests
                   tests/g391_tank_track_articulated_walker_unit_class_tests.cpp)
    target_include_directories(thandor_g391_tank_track_articulated_walker_unit_class_tests PRIVATE src/editor)
    target_link_libraries(thandor_g391_tank_track_articulated_walker_unit_class_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g391_tank_track_articulated_walker_unit_class_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g391_tank_track_articulated_walker_unit_class_tests
             COMMAND thandor_g391_tank_track_articulated_walker_unit_class_tests)

    add_executable(thandor_g392_articulated_turn_hornet_deployment_tests
                   tests/g392_articulated_turn_hornet_deployment_tests.cpp)
    target_include_directories(thandor_g392_articulated_turn_hornet_deployment_tests PRIVATE src/editor)
    target_link_libraries(thandor_g392_articulated_turn_hornet_deployment_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g392_articulated_turn_hornet_deployment_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g392_articulated_turn_hornet_deployment_tests
             COMMAND thandor_g392_articulated_turn_hornet_deployment_tests)
    add_test(NAME thandor_g391_unit_class_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g391_unit_class_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g391_unit_class_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(thandor_g389_v338_raycast_lookup_domain_tests
                   tests/g389_v338_raycast_lookup_domain_tests.cpp)
    target_include_directories(thandor_g389_v338_raycast_lookup_domain_tests PRIVATE src/editor)
    target_link_libraries(thandor_g389_v338_raycast_lookup_domain_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g389_v338_raycast_lookup_domain_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g389_v338_raycast_lookup_domain_tests
             COMMAND thandor_g389_v338_raycast_lookup_domain_tests)
    add_executable(thandor_map_core_tests tests/core_tests.cpp)
    target_link_libraries(thandor_map_core_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_map_core_tests COMMAND thandor_map_core_tests)

    add_executable(thandor_game_executable_name_tests tests/game_executable_name_tests.cpp)
    target_link_libraries(thandor_game_executable_name_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_game_executable_name_tests COMMAND thandor_game_executable_name_tests)

    add_executable(thandor_editor_math_tests tests/editor_math_tests.cpp)
    target_include_directories(thandor_editor_math_tests PRIVATE src/editor)
    target_link_libraries(thandor_editor_math_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_editor_math_tests COMMAND thandor_editor_math_tests)

    add_executable(thandor_terrain_render_tests tests/terrain_render_tests.cpp)
    target_link_libraries(thandor_terrain_render_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_terrain_render_tests COMMAND thandor_terrain_render_tests)

    add_executable(thandor_object_visual_tests tests/object_visual_tests.cpp)
    target_link_libraries(thandor_object_visual_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_object_visual_tests COMMAND thandor_object_visual_tests)

    add_executable(thandor_resource_glow_water_tests tests/resource_glow_water_tests.cpp)
    target_link_libraries(thandor_resource_glow_water_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_resource_glow_water_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_resource_glow_water_tests COMMAND thandor_resource_glow_water_tests)

    add_executable(thandor_asset_browser_transform_tests tests/asset_browser_transform_tests.cpp)
    target_include_directories(thandor_asset_browser_transform_tests PRIVATE src/editor)
    target_link_libraries(thandor_asset_browser_transform_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_asset_browser_transform_tests COMMAND thandor_asset_browser_transform_tests)

    add_executable(thandor_asset_browser_category_texture_tests tests/asset_browser_category_texture_tests.cpp)
    target_include_directories(thandor_asset_browser_category_texture_tests PRIVATE src/editor)
    target_link_libraries(thandor_asset_browser_category_texture_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_asset_browser_category_texture_tests COMMAND thandor_asset_browser_category_texture_tests)

    add_executable(thandor_map_workflow_tests tests/map_workflow_tests.cpp)
    target_link_libraries(thandor_map_workflow_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_map_workflow_tests COMMAND thandor_map_workflow_tests)

    add_executable(thandor_game_level_package_tests tests/game_level_package_tests.cpp)
    target_link_libraries(thandor_game_level_package_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_game_level_package_tests COMMAND thandor_game_level_package_tests)

    add_executable(thandor_g327_game_package_path_tests tests/g327_game_package_path_tests.cpp)
    target_link_libraries(thandor_g327_game_package_path_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g327_game_package_path_tests COMMAND thandor_g327_game_package_path_tests)

    add_executable(thandor_g328_frontend_catalog_runtime_tests tests/g328_frontend_catalog_runtime_tests.cpp)
    target_link_libraries(thandor_g328_frontend_catalog_runtime_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g328_frontend_catalog_runtime_tests COMMAND thandor_g328_frontend_catalog_runtime_tests)

    add_executable(thandor_g329_custom_alias_fallback_tests tests/g329_custom_alias_fallback_tests.cpp)
    target_link_libraries(thandor_g329_custom_alias_fallback_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g329_custom_alias_fallback_tests COMMAND thandor_g329_custom_alias_fallback_tests)

    add_executable(thandor_g330_player_faction_begin_contract_tests
                   tests/g330_player_faction_begin_contract_tests.cpp)
    target_link_libraries(thandor_g330_player_faction_begin_contract_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g330_player_faction_begin_contract_tests
             COMMAND thandor_g330_player_faction_begin_contract_tests)

    add_executable(thandor_g331_field_header_build_tests
                   tests/g331_field_header_build_tests.cpp)
    target_link_libraries(thandor_g331_field_header_build_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g331_field_header_build_tests
             COMMAND thandor_g331_field_header_build_tests)

    add_executable(thandor_g332_required_level_movie_contract_tests
                   tests/g332_required_level_movie_contract_tests.cpp)
    target_link_libraries(thandor_g332_required_level_movie_contract_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g332_required_level_movie_contract_tests
             COMMAND thandor_g332_required_level_movie_contract_tests)

    add_executable(thandor_g333_resource_camera_classification_tests
                   tests/g333_resource_camera_classification_tests.cpp)
    target_link_libraries(thandor_g333_resource_camera_classification_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g333_resource_camera_classification_tests
             COMMAND thandor_g333_resource_camera_classification_tests)

    add_executable(thandor_g334_game_ready_object_terrain_compatibility_tests
                   tests/g334_game_ready_object_terrain_compatibility_tests.cpp)
    target_link_libraries(thandor_g334_game_ready_object_terrain_compatibility_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g334_game_ready_object_terrain_compatibility_tests
             COMMAND thandor_g334_game_ready_object_terrain_compatibility_tests)

    add_executable(thandor_g335_dry_water_starting_resource_contract_tests
                   tests/g335_dry_water_starting_resource_contract_tests.cpp)
    target_link_libraries(thandor_g335_dry_water_starting_resource_contract_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g335_dry_water_starting_resource_contract_tests
             COMMAND thandor_g335_dry_water_starting_resource_contract_tests)

    add_executable(thandor_g336_resource_field_occupancy_contract_tests
                   tests/g336_resource_field_occupancy_contract_tests.cpp)
    target_link_libraries(thandor_g336_resource_field_occupancy_contract_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g336_resource_field_occupancy_contract_tests
             COMMAND thandor_g336_resource_field_occupancy_contract_tests)

    add_executable(thandor_g337_players_ai_renderer_correspondence_tests
                   tests/g337_players_ai_renderer_correspondence_tests.cpp)
    target_link_libraries(thandor_g337_players_ai_renderer_correspondence_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g337_players_ai_renderer_correspondence_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g337_players_ai_renderer_correspondence_tests
             COMMAND thandor_g337_players_ai_renderer_correspondence_tests)

    add_executable(thandor_g338_terrain_transition_shadow_tests
                   tests/g338_terrain_transition_shadow_tests.cpp)
    target_link_libraries(thandor_g338_terrain_transition_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g338_terrain_transition_shadow_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g338_terrain_transition_shadow_tests
             COMMAND thandor_g338_terrain_transition_shadow_tests)

    add_executable(thandor_g339_exact_soil_packet_resource_field_tests
                   tests/g339_exact_soil_packet_resource_field_tests.cpp)
    target_link_libraries(thandor_g339_exact_soil_packet_resource_field_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g339_exact_soil_packet_resource_field_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g339_exact_soil_packet_resource_field_tests
             COMMAND thandor_g339_exact_soil_packet_resource_field_tests)

    add_executable(thandor_primitive_render_tests tests/primitive_render_tests.cpp)
    target_include_directories(thandor_primitive_render_tests PRIVATE src/editor)
    add_test(NAME thandor_primitive_render_tests COMMAND thandor_primitive_render_tests)

    add_executable(thandor_g340_directdraw_resource_animation_tests
                   tests/g340_directdraw_resource_animation_tests.cpp)
    target_link_libraries(thandor_g340_directdraw_resource_animation_tests PRIVATE thandor_map_core)
    target_include_directories(thandor_g340_directdraw_resource_animation_tests PRIVATE src/editor)
    add_test(NAME thandor_g340_directdraw_resource_animation_tests
             COMMAND thandor_g340_directdraw_resource_animation_tests)

    add_executable(thandor_g341_directdraw_vertex_aura_tests
                   tests/g341_directdraw_vertex_aura_tests.cpp)
    target_link_libraries(thandor_g341_directdraw_vertex_aura_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g341_directdraw_vertex_aura_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g341_directdraw_vertex_aura_tests
             COMMAND thandor_g341_directdraw_vertex_aura_tests)

    add_executable(thandor_g342_directdraw_resource_tint_bypass_tests
                   tests/g342_directdraw_resource_tint_bypass_tests.cpp)
    target_link_libraries(thandor_g342_directdraw_resource_tint_bypass_tests PRIVATE thandor_map_core)
    target_include_directories(thandor_g342_directdraw_resource_tint_bypass_tests PRIVATE src/editor)
    target_compile_definitions(thandor_g342_directdraw_resource_tint_bypass_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g342_directdraw_resource_tint_bypass_tests
             COMMAND thandor_g342_directdraw_resource_tint_bypass_tests)

    add_executable(thandor_g343_directdraw_encoded_framebuffer_blend_tests
                   tests/g343_directdraw_encoded_framebuffer_blend_tests.cpp)
    target_link_libraries(thandor_g343_directdraw_encoded_framebuffer_blend_tests PRIVATE thandor_map_core)
    target_include_directories(thandor_g343_directdraw_encoded_framebuffer_blend_tests PRIVATE src/editor)
    target_compile_definitions(thandor_g343_directdraw_encoded_framebuffer_blend_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g343_directdraw_encoded_framebuffer_blend_tests
             COMMAND thandor_g343_directdraw_encoded_framebuffer_blend_tests)

    add_executable(thandor_g344_directdraw_resource_non_bypass_lighting_tests
                   tests/g344_directdraw_resource_non_bypass_lighting_tests.cpp)
    target_link_libraries(thandor_g344_directdraw_resource_non_bypass_lighting_tests PRIVATE thandor_map_core)
    target_include_directories(thandor_g344_directdraw_resource_non_bypass_lighting_tests PRIVATE src/editor)
    target_compile_definitions(thandor_g344_directdraw_resource_non_bypass_lighting_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g344_directdraw_resource_non_bypass_lighting_tests
             COMMAND thandor_g344_directdraw_resource_non_bypass_lighting_tests)

    add_executable(thandor_g345_directdraw_single_level_object_texture_sampling_tests
                   tests/g345_directdraw_single_level_object_texture_sampling_tests.cpp)
    target_include_directories(thandor_g345_directdraw_single_level_object_texture_sampling_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g345_directdraw_single_level_object_texture_sampling_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g345_directdraw_single_level_object_texture_sampling_tests
             COMMAND thandor_g345_directdraw_single_level_object_texture_sampling_tests)

    add_executable(thandor_g346_directdraw_water_surface_animation_color_tests
                   tests/g346_directdraw_water_surface_animation_color_tests.cpp)
    target_link_libraries(thandor_g346_directdraw_water_surface_animation_color_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g346_directdraw_water_surface_animation_color_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g346_directdraw_water_surface_animation_color_tests
             COMMAND thandor_g346_directdraw_water_surface_animation_color_tests)

    add_executable(thandor_g347_brush_water_texture_faction_animation_tests
                   tests/g347_brush_water_texture_faction_animation_tests.cpp)
    target_link_libraries(thandor_g347_brush_water_texture_faction_animation_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g347_brush_water_texture_faction_animation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g347_brush_water_texture_faction_animation_tests
             COMMAND thandor_g347_brush_water_texture_faction_animation_tests)

    add_executable(thandor_g348_flat_water_alpha_faction_scope_tests
                   tests/g348_flat_water_alpha_faction_scope_tests.cpp)
    target_link_libraries(thandor_g348_flat_water_alpha_faction_scope_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g348_flat_water_alpha_faction_scope_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g348_flat_water_alpha_faction_scope_tests
             COMMAND thandor_g348_flat_water_alpha_faction_scope_tests)

    add_executable(thandor_g364_animated_primary_surface_tests
                   tests/g364_animated_primary_surface_tests.cpp)
    target_link_libraries(thandor_g364_animated_primary_surface_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g364_animated_primary_surface_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g364_animated_primary_surface_tests
             COMMAND thandor_g364_animated_primary_surface_tests)

    add_executable(thandor_g365_presentation_timeline_lava_effect_tests
                   tests/g365_presentation_timeline_lava_effect_tests.cpp)
    target_link_libraries(thandor_g365_presentation_timeline_lava_effect_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g365_presentation_timeline_lava_effect_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g365_presentation_timeline_lava_effect_tests
             COMMAND thandor_g365_presentation_timeline_lava_effect_tests)

    add_executable(thandor_g365_stock_lava_effect_census
                   tests/g365_stock_lava_effect_census.cpp)
    target_link_libraries(thandor_g365_stock_lava_effect_census PRIVATE thandor_map_core)

    add_executable(thandor_g366_view_scoped_animation_performance_tests
                   tests/g366_view_scoped_animation_performance_tests.cpp)
    target_compile_definitions(thandor_g366_view_scoped_animation_performance_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g366_view_scoped_animation_performance_tests
             COMMAND thandor_g366_view_scoped_animation_performance_tests)

    add_executable(thandor_g349_recursive_model_hierarchy_preview_tests
                   tests/g349_recursive_model_hierarchy_preview_tests.cpp)
    target_link_libraries(thandor_g349_recursive_model_hierarchy_preview_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g349_recursive_model_hierarchy_preview_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g349_recursive_model_hierarchy_preview_tests
             COMMAND thandor_g349_recursive_model_hierarchy_preview_tests)

    add_executable(thandor_g349_stock_building_hierarchy_census
                   tests/g349_stock_building_hierarchy_census.cpp)
    target_link_libraries(thandor_g349_stock_building_hierarchy_census PRIVATE thandor_map_core)

    add_executable(thandor_g350_pck_asset_name_propagation_tests
                   tests/g350_pck_asset_name_propagation_tests.cpp)
    target_link_libraries(thandor_g350_pck_asset_name_propagation_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g350_pck_asset_name_propagation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g350_pck_asset_name_propagation_tests
             COMMAND thandor_g350_pck_asset_name_propagation_tests)

    add_executable(thandor_g350_stock_asset_name_census
                   tests/g350_stock_asset_name_census.cpp)
    target_link_libraries(thandor_g350_stock_asset_name_census PRIVATE thandor_map_core)


    add_executable(thandor_g351_arm_linked_model_preview_raster_tests
                   tests/g351_arm_linked_model_preview_raster_tests.cpp)
    target_include_directories(thandor_g351_arm_linked_model_preview_raster_tests PRIVATE src/editor)
    target_link_libraries(thandor_g351_arm_linked_model_preview_raster_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g351_arm_linked_model_preview_raster_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g351_arm_linked_model_preview_raster_tests
             COMMAND thandor_g351_arm_linked_model_preview_raster_tests)

    add_executable(thandor_g351_stock_linked_army_model_census
                   tests/g351_stock_linked_army_model_census.cpp)
    target_link_libraries(thandor_g351_stock_linked_army_model_census PRIVATE thandor_map_core)

    add_executable(thandor_g352_placed_model_rotation_receiver_shadow_tests
                   tests/g352_placed_model_rotation_receiver_shadow_tests.cpp)
    target_include_directories(thandor_g352_placed_model_rotation_receiver_shadow_tests PRIVATE src/editor)
    target_link_libraries(thandor_g352_placed_model_rotation_receiver_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g352_placed_model_rotation_receiver_shadow_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g352_placed_model_rotation_receiver_shadow_tests
             COMMAND thandor_g352_placed_model_rotation_receiver_shadow_tests)

    add_executable(thandor_g353_sprite_plane_normal_winding_tests
                   tests/g353_sprite_plane_normal_winding_tests.cpp)
    target_link_libraries(thandor_g353_sprite_plane_normal_winding_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g353_sprite_plane_normal_winding_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g353_sprite_plane_normal_winding_tests
             COMMAND thandor_g353_sprite_plane_normal_winding_tests)

    add_executable(thandor_g354_game_cursor_mouse_camera_tests
                   tests/g354_game_cursor_mouse_camera_tests.cpp)
    target_include_directories(thandor_g354_game_cursor_mouse_camera_tests PRIVATE src/editor)
    target_link_libraries(thandor_g354_game_cursor_mouse_camera_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g354_game_cursor_mouse_camera_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g354_game_cursor_mouse_camera_tests
             COMMAND thandor_g354_game_cursor_mouse_camera_tests)

    add_executable(thandor_g355_binary_generated_shadow_tests
                   tests/g355_binary_generated_shadow_tests.cpp)
    target_link_libraries(thandor_g355_binary_generated_shadow_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g355_binary_generated_shadow_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g355_binary_generated_shadow_tests
             COMMAND thandor_g355_binary_generated_shadow_tests)

    add_executable(thandor_g356_dedicated_shadow_table_tests
                   tests/g356_dedicated_shadow_table_tests.cpp)
    target_link_libraries(thandor_g356_dedicated_shadow_table_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g356_dedicated_shadow_table_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g356_dedicated_shadow_table_tests
             COMMAND thandor_g356_dedicated_shadow_table_tests)

    add_executable(thandor_g356_stock_shadow_table_census
                   tests/g356_stock_shadow_table_census.cpp)
    target_link_libraries(thandor_g356_stock_shadow_table_census PRIVATE thandor_map_core)

    add_executable(thandor_g357_lev_light_shadow_direction_tests
                   tests/g357_lev_light_shadow_direction_tests.cpp)
    target_link_libraries(thandor_g357_lev_light_shadow_direction_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g357_lev_light_shadow_direction_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g357_lev_light_shadow_direction_tests
             COMMAND thandor_g357_lev_light_shadow_direction_tests)

    add_executable(thandor_g358_full_visibility_directional_lut_tests
                   tests/g358_full_visibility_directional_lut_tests.cpp)
    target_link_libraries(thandor_g358_full_visibility_directional_lut_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g358_full_visibility_directional_lut_tests PRIVATE
        THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g358_full_visibility_directional_lut_tests
             COMMAND thandor_g358_full_visibility_directional_lut_tests)

    add_executable(thandor_g325_stock_composition_census tests/g325_stock_composition_census.cpp)
    target_include_directories(thandor_g325_stock_composition_census PRIVATE src/editor)
    target_link_libraries(thandor_g325_stock_composition_census PRIVATE thandor_map_core)
    add_test(NAME thandor_g325_stock_composition_census COMMAND thandor_g325_stock_composition_census)

    add_test(NAME thandor_imgui_vulkan_api_detector_tests
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/imgui_vulkan_api_detector_tests.py)
    set_tests_properties(thandor_imgui_vulkan_api_detector_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(thandor_g367_general_camera_start_cards_tests
                   tests/g367_general_camera_start_cards_tests.cpp)
    target_link_libraries(thandor_g367_general_camera_start_cards_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g367_general_camera_start_cards_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g367_general_camera_start_cards_tests
             COMMAND thandor_g367_general_camera_start_cards_tests)

    add_executable(thandor_g368_headquarters_render_animation_tests
                   tests/g368_headquarters_render_animation_tests.cpp)
    target_include_directories(thandor_g368_headquarters_render_animation_tests PRIVATE src/editor)
    target_link_libraries(thandor_g368_headquarters_render_animation_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g368_headquarters_render_animation_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g368_headquarters_render_animation_tests
             COMMAND thandor_g368_headquarters_render_animation_tests)

    add_executable(thandor_g368_stock_headquarters_census
                   tests/g368_stock_headquarters_census.cpp)
    target_include_directories(thandor_g368_stock_headquarters_census PRIVATE src/editor)
    target_link_libraries(thandor_g368_stock_headquarters_census PRIVATE thandor_map_core)
    add_test(NAME thandor_g368_stock_headquarters_census
             COMMAND thandor_g368_stock_headquarters_census)

    add_executable(thandor_g369_effect_transition2_smoke_tests
                   tests/g369_effect_transition2_smoke_tests.cpp)
    target_link_libraries(thandor_g369_effect_transition2_smoke_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g369_effect_transition2_smoke_tests
             COMMAND thandor_g369_effect_transition2_smoke_tests)

    add_executable(thandor_g369_headquarters_fidelity_tests
                   tests/g369_headquarters_fidelity_tests.cpp)
    target_include_directories(thandor_g369_headquarters_fidelity_tests PRIVATE src/editor)
    target_link_libraries(thandor_g369_headquarters_fidelity_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g369_headquarters_fidelity_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g369_headquarters_fidelity_tests
             COMMAND thandor_g369_headquarters_fidelity_tests)

    add_executable(thandor_g370_viewport_picking_brush_tests
                   tests/g370_viewport_picking_brush_tests.cpp)
    target_include_directories(thandor_g370_viewport_picking_brush_tests PRIVATE src/editor)
    target_link_libraries(thandor_g370_viewport_picking_brush_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g370_viewport_picking_brush_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g370_viewport_picking_brush_tests
             COMMAND thandor_g370_viewport_picking_brush_tests)

    add_executable(thandor_g371_simulation_weapon_selection_tests
                   tests/g371_simulation_weapon_selection_tests.cpp)
    target_include_directories(thandor_g371_simulation_weapon_selection_tests PRIVATE src/editor)
    target_link_libraries(thandor_g371_simulation_weapon_selection_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g371_simulation_weapon_selection_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g371_simulation_weapon_selection_tests
             COMMAND thandor_g371_simulation_weapon_selection_tests)

    add_executable(thandor_g371_stock_simulation_census
                   tests/g371_stock_simulation_census.cpp)
    target_link_libraries(thandor_g371_stock_simulation_census PRIVATE thandor_map_core)
    add_test(NAME thandor_g371_stock_simulation_census
             COMMAND thandor_g371_stock_simulation_census)

    add_test(NAME thandor_g372_target_toolchain_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g372_target_toolchain_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g372_target_toolchain_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_test(NAME thandor_g390_vector_domain_compile_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g390_vector_domain_compile_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g390_vector_domain_compile_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(thandor_g373_simulation_static_composition_tests
                   tests/g373_simulation_static_composition_tests.cpp)
    target_link_libraries(thandor_g373_simulation_static_composition_tests PRIVATE thandor_map_core)
    add_test(NAME thandor_g373_simulation_static_composition_tests
             COMMAND thandor_g373_simulation_static_composition_tests)

    add_test(NAME thandor_g373_simulation_render_regression_tests
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/g373_simulation_render_regression_tests.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g373_simulation_render_regression_tests
                         PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")

    add_executable(thandor_g374_contextual_order_marker_tests
                   tests/g374_contextual_order_marker_tests.cpp)
    target_link_libraries(thandor_g374_contextual_order_marker_tests PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g374_contextual_order_marker_tests PRIVATE
                               THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g374_contextual_order_marker_tests
             COMMAND thandor_g374_contextual_order_marker_tests)

    add_executable(thandor_g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                   tests/g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests.cpp)
    target_link_libraries(thandor_g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests
             COMMAND thandor_g375_persistent_drive_weapon_hierarchy_effect_unit_animation_tests)
    add_executable(thandor_g376_live_pose_effect_culling_hover_regression_tests
                   tests/g376_live_pose_effect_culling_hover_regression_tests.cpp)
    target_link_libraries(thandor_g376_live_pose_effect_culling_hover_regression_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g376_live_pose_effect_culling_hover_regression_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g376_live_pose_effect_culling_hover_regression_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g376_live_pose_effect_culling_hover_regression_tests
             COMMAND thandor_g376_live_pose_effect_culling_hover_regression_tests)

    add_executable(thandor_g377_attack_runtime_rotor_shadow_pipeline_tests
                   tests/g377_attack_runtime_rotor_shadow_pipeline_tests.cpp)
    target_link_libraries(thandor_g377_attack_runtime_rotor_shadow_pipeline_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g377_attack_runtime_rotor_shadow_pipeline_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g377_attack_runtime_rotor_shadow_pipeline_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g377_attack_runtime_rotor_shadow_pipeline_tests
             COMMAND thandor_g377_attack_runtime_rotor_shadow_pipeline_tests)

    add_executable(thandor_g378_exact_shot_effect_impact_texture_capacity_tests
                   tests/g378_exact_shot_effect_impact_texture_capacity_tests.cpp)
    target_link_libraries(thandor_g378_exact_shot_effect_impact_texture_capacity_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g378_exact_shot_effect_impact_texture_capacity_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g378_exact_shot_effect_impact_texture_capacity_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g378_exact_shot_effect_impact_texture_capacity_tests
             COMMAND thandor_g378_exact_shot_effect_impact_texture_capacity_tests)

    add_executable(thandor_g379_shot_axis_beam_class9_reload_tests
                   tests/g379_shot_axis_beam_class9_reload_tests.cpp)
    target_link_libraries(thandor_g379_shot_axis_beam_class9_reload_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g379_shot_axis_beam_class9_reload_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g379_shot_axis_beam_class9_reload_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g379_shot_axis_beam_class9_reload_tests
             COMMAND thandor_g379_shot_axis_beam_class9_reload_tests)

    add_executable(thandor_g380_mode3_direct_path_launch_reload_visibility_tests
                   tests/g380_mode3_direct_path_launch_reload_visibility_tests.cpp)
    target_link_libraries(thandor_g380_mode3_direct_path_launch_reload_visibility_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g380_mode3_direct_path_launch_reload_visibility_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g380_mode3_direct_path_launch_reload_visibility_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g380_mode3_direct_path_launch_reload_visibility_tests
             COMMAND thandor_g380_mode3_direct_path_launch_reload_visibility_tests)


    add_executable(thandor_g381_semantic_c_class9_pivot_rack_reload_tests
                   tests/g381_semantic_c_class9_pivot_rack_reload_tests.cpp)
    target_link_libraries(thandor_g381_semantic_c_class9_pivot_rack_reload_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g381_semantic_c_class9_pivot_rack_reload_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g381_semantic_c_class9_pivot_rack_reload_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g381_semantic_c_class9_pivot_rack_reload_tests
             COMMAND thandor_g381_semantic_c_class9_pivot_rack_reload_tests)

    add_executable(thandor_g382_semantic_export_resolver_carry_selector_retry_tests
                   tests/g382_semantic_export_resolver_carry_selector_retry_tests.cpp)
    target_link_libraries(thandor_g382_semantic_export_resolver_carry_selector_retry_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g382_semantic_export_resolver_carry_selector_retry_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g382_semantic_export_resolver_carry_selector_retry_tests
             COMMAND thandor_g382_semantic_export_resolver_carry_selector_retry_tests)

    add_executable(thandor_g383_semantic_export_map_editor_logic_audit_tests
                   tests/g383_semantic_export_map_editor_logic_audit_tests.cpp)
    target_link_libraries(thandor_g383_semantic_export_map_editor_logic_audit_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g383_semantic_export_map_editor_logic_audit_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g383_semantic_export_map_editor_logic_audit_tests
             COMMAND thandor_g383_semantic_export_map_editor_logic_audit_tests)

    add_executable(thandor_g384_exact_placement_contact_surface_bounds_tests
                   tests/g384_exact_placement_contact_surface_bounds_tests.cpp)
    target_link_libraries(thandor_g384_exact_placement_contact_surface_bounds_tests
                          PRIVATE thandor_map_core)
    target_include_directories(thandor_g384_exact_placement_contact_surface_bounds_tests
                               PRIVATE src/editor)
    target_compile_definitions(thandor_g384_exact_placement_contact_surface_bounds_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g384_exact_placement_contact_surface_bounds_tests
             COMMAND thandor_g384_exact_placement_contact_surface_bounds_tests)

    add_executable(thandor_g385_semantic_field_rename_incremental_closure_tests
                   tests/g385_semantic_field_rename_incremental_closure_tests.cpp)
    target_link_libraries(thandor_g385_semantic_field_rename_incremental_closure_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g385_semantic_field_rename_incremental_closure_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g385_semantic_field_rename_incremental_closure_tests
             COMMAND thandor_g385_semantic_field_rename_incremental_closure_tests)

    add_executable(thandor_g386_technology_variant_asset_name_effect_pipeline_tests
                   tests/g386_technology_variant_asset_name_effect_pipeline_tests.cpp)
    target_link_libraries(thandor_g386_technology_variant_asset_name_effect_pipeline_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g386_technology_variant_asset_name_effect_pipeline_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g386_technology_variant_asset_name_effect_pipeline_tests
             COMMAND thandor_g386_technology_variant_asset_name_effect_pipeline_tests)

    add_executable(thandor_g396_fog_economy_integrity_damage_effect_transparency_tests
                   tests/g396_fog_economy_integrity_damage_effect_transparency_tests.cpp)
    target_link_libraries(thandor_g396_fog_economy_integrity_damage_effect_transparency_tests
                          PRIVATE thandor_map_core)
    target_compile_definitions(thandor_g396_fog_economy_integrity_damage_effect_transparency_tests
                               PRIVATE THANDOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME thandor_g396_fog_economy_integrity_damage_effect_transparency_tests
             COMMAND thandor_g396_fog_economy_integrity_damage_effect_transparency_tests)

    add_test(NAME thandor_g363_full_authority_metadata_validation
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_g363_full_authority_metadata.py
                     --source ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(thandor_g363_full_authority_metadata_validation
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
    target_link_libraries(thandor_imgui PUBLIC glfw Vulkan::Vulkan)

    add_executable(thandor-map-editor
        src/editor/main.cpp
        src/editor/editor_app.cpp
        src/editor/vulkan_context.cpp
        src/editor/terrain_renderer.cpp
    )
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
        set(SHADER_NAMES terrain.vert terrain.frag surface.vert surface.frag water.vert water.frag markers.vert markers.frag
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
    else()
        message(FATAL_ERROR "glslc was not found. Install the Vulkan SDK so shaders can be compiled.")
    endif()
endif()
