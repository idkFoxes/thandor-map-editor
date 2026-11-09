#include "editor_app.hpp"
#include <imgui.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace editor {
void EditorApp::reset_terrain_generator(){
    generator_preview_.reset();generator_source_field_.clear();generator_source_level_.clear();
    generator_selection_.clear();generator_outline_.clear();generator_weights_.clear();
    generator_region_ready_=false;generator_drawing_=false;generator_message_.clear();
    generator_edit_path_=false;generator_stroke_is_path_=false;generator_settings_.river_path.clear();
    generator_selection_label_="Draw an area or choose Whole map.";
}
void EditorApp::discard_terrain_generator_preview(){
    if(!generator_preview_)return;
    generator_preview_.reset();generator_source_field_.clear();generator_source_level_.clear();rebuild_mesh();
}
std::vector<uint8_t> EditorApp::terrain_generator_protection() const {
    if(!document_)return {};
    const auto& f=document_->field();std::vector<uint8_t> mask(f.cells().size());
    if(!generator_protect_structures_)return mask;
    const auto placements=document_->level().placements();
    for(std::size_t i=0;i<placements.size();++i){const auto& p=placements[i];const auto* preset=catalog_.army_preset(p.army_asset_id);
        if(!preset||(preset->visual_kind!=thandor::PlacementVisualKind::PlayerStart&&preset->browser_kind!=thandor::PlacementBrowserKind::Building))continue;
        float min_x=p.render_x()-1.5F,max_x=p.render_x()+1.5F,min_y=p.render_y()-1.5F,max_y=p.render_y()+1.5F;
        if(i<placement_pick_bounds_.size()&&placement_pick_bounds_[i].valid()){
            const auto& b=placement_pick_bounds_[i];min_x=std::min(min_x,b.minimum.x-0.6F);max_x=std::max(max_x,b.maximum.x+0.6F);
            min_y=std::min(min_y,b.minimum.y-0.6F);max_y=std::max(max_y,b.maximum.y+0.6F);
        }
        for(std::size_t cell=0;cell<f.cells().size();++cell){const auto& c=f.cells()[cell];
            const float x=thandor::q12_to_float(c.world_x_q12),y=thandor::q12_to_float(c.world_y_q12);
            if(x>=min_x&&x<=max_x&&y>=min_y&&y<=max_y)mask[cell]=1;
        }
    }
    return mask;
}
void EditorApp::refresh_terrain_generator_selection(bool sample_height){
    if(!document_||!generator_region_ready_)return;
    const auto& f=document_->field();
    generator_weights_=thandor::terrain_region_weights(f,generator_selection_,generator_settings_,terrain_generator_protection());
    std::size_t selected=0;double sum=0;float min_x=static_cast<float>(f.width()),max_x=0,min_y=static_cast<float>(f.height()),max_y=0;
    for(uint32_t y=0;y<f.height();++y)for(uint32_t x=0;x<f.width();++x){const auto i=static_cast<std::size_t>(y)*f.width()+x;if(generator_weights_[i]<=0)continue;
        ++selected;sum+=f.cells()[i].terrain_height();min_x=std::min(min_x,static_cast<float>(x));max_x=std::max(max_x,static_cast<float>(x));min_y=std::min(min_y,static_cast<float>(y));max_y=std::max(max_y,static_cast<float>(y));}
    generator_selection_label_=std::to_string(selected)+" editable cells · "+(generator_settings_.outside?"outside":"inside");
    if(sample_height&&selected){generator_settings_.base_height=static_cast<float>(sum/static_cast<double>(selected));
        generator_settings_.water_level=generator_settings_.base_height-(generator_settings_.preset==thandor::TerrainGeneratorPreset::Lake?generator_settings_.height*0.35F:0.0F);
        generator_settings_.river_angle_degrees=(max_x-min_x>=max_y-min_y)?0.0F:90.0F;}
}
void EditorApp::preview_generated_terrain(){
    if(!document_||!generator_region_ready_)throw std::runtime_error("Draw an area or choose Whole map first.");
    if(simulation_mode_||simulation_lab_active())throw std::runtime_error("Stop the simulation before generating terrain.");
    generator_edit_path_=false;generator_drawing_=false;generator_outline_.clear();
    auto next=thandor::generate_terrain_region(document_->field(),generator_selection_,generator_settings_,terrain_generator_protection());
    generator_source_field_=document_->field().serialize();generator_source_level_=document_->level().serialize();
    const auto lighting=lighting_cycle_preview_enabled_?document_->level().lighting().sampled_at_tick(lighting_cycle_preview_tick_):document_->level().lighting();
    generator_weights_=next.weights;generator_message_="Preview · "+std::to_string(next.changed_cells)+" changed cells. Apply to keep it.";
    if(generator_settings_.preset==thandor::TerrainGeneratorPreset::River){
        if(next.river_cells==0||next.river_water_cells==0)generator_message_="The river misses this region. Adjust direction, width or the selection.";
        else if(generator_settings_.drivable_river&&next.blocked_river_count)generator_message_="Red cells block standard ground traversal. Widen the area, adjust the level, or disable Drivable river.";
        else if(generator_settings_.drivable_river)generator_message_="Preview · river passes the game's terrain checks for ground bands 1-6.";
    }
    if(generator_settings_.preset==thandor::TerrainGeneratorPreset::Lake||generator_settings_.preset==thandor::TerrainGeneratorPreset::River)draw_water_=true;
    if(next.water_level_lowered){char level[32];std::snprintf(level,sizeof(level),"%.2f",next.applied_water_level);
        generator_message_+=" Water capped at "+std::string(level)+" to stay below the banks.";}
    if(next.playability_checked){
        generator_message_+=" Connected building areas: "+std::to_string(next.connected_building_sites)+" / "+std::to_string(next.building_sites.size())+".";
        if(!next.protected_approaches.empty())generator_message_+=" Protected areas reached: "+std::to_string(next.connected_protected_areas)+" / "+std::to_string(next.protected_approaches.size())+".";
        if(!next.playability_ok)generator_message_+=" No safe layout fits this selection. Adjust relief or protected areas before applying.";
    }
    generator_preview_=std::move(next);
    renderer_.update_mesh(generator_preview_->field,lighting,terrain_texture_layout_?&*terrain_texture_layout_:nullptr,
        surface_texture_layout_?&*surface_texture_layout_:nullptr,exact_soil_packets_,soil_transition_rng_seed_,resource_animation_rng_seed_);

}
void EditorApp::apply_generated_terrain(){
    if(!document_||!generator_preview_)throw std::runtime_error("Preview the terrain first.");
    if(simulation_mode_||simulation_lab_active())throw std::runtime_error("Stop the simulation before applying terrain.");
    if(generator_source_field_!=document_->field().serialize()||generator_source_level_!=document_->level().serialize()){
        discard_terrain_generator_preview();throw std::runtime_error("The map changed. Make a new preview before applying.");}
    if(generator_settings_.preset==thandor::TerrainGeneratorPreset::River&&generator_settings_.drivable_river&&generator_preview_->blocked_river_count)
        throw std::runtime_error("This river has blocked ground cells. Adjust the red areas before applying as drivable.");
    if(generator_settings_.preset==thandor::TerrainGeneratorPreset::River&&generator_preview_->river_water_cells==0)
        throw std::runtime_error("The river has no water. Widen the selection or adjust the surface level.");
    if(generator_preview_->playability_checked&&!generator_preview_->playability_ok)
        throw std::runtime_error("Building areas or protected starts/buildings lack verified ground access. Adjust the terrain and preview again.");
    if(generator_preview_->changed_cells==0)throw std::runtime_error("This preview has no changes to apply.");
    checkpoint();document_->field()=std::move(generator_preview_->field);generator_preview_.reset();
    document_->mark_dirty();reset_simulation(true);rebuild_mesh();refresh_terrain_generator_selection();
    generator_message_="Terrain applied. Ctrl+Z undoes this generation.";status_=generator_message_;
}
void EditorApp::draw_terrain_generator_panel(){
    if(!document_)return;
    if(generator_selection_.size()!=document_->field().cells().size()){reset_terrain_generator();generator_selection_.resize(document_->field().cells().size());}
    const auto action=[&](auto f){try{f();}catch(const std::exception& e){discard_terrain_generator_preview();generator_message_=e.what();}};
    if(simulation_mode_||simulation_lab_active()){ImGui::TextWrapped("Stop the simulation to edit terrain. This tool works on new and loaded maps.");return;}
    ImGui::PushItemWidth(ui_px(165));
    ImGui::SeparatorText("1. Choose an area");
    const char* shapes[]{"Lasso","Rectangle"};ImGui::Combo("Selection tool",&generator_shape_,shapes,2);
    const char* operations[]{"Replace area","Add area","Remove area"};ImGui::Combo("Selection action",&generator_combine_,operations,3);
    if(!generator_region_ready_)ImGui::TextWrapped("Left-drag on the map and release to close the area. Right-drag moves the camera. Escape cancels a stroke.");
    else ImGui::TextWrapped(generator_edit_path_?"Drag an open line on the map for the river route. Escape cancels.":"Drag another area to replace, add or remove it.");
    if(ImGui::Button("Whole map"))action([&]{discard_terrain_generator_preview();std::fill(generator_selection_.begin(),generator_selection_.end(),1);generator_settings_.outside=false;generator_region_ready_=true;refresh_terrain_generator_selection(true);});
    ImGui::SameLine();if(ImGui::Button("Clear"))action([&]{discard_terrain_generator_preview();reset_terrain_generator();generator_selection_.resize(document_->field().cells().size());});
    int side=generator_settings_.outside?1:0;const char* sides[]{"Inside area","Outside area"};
    bool changed=false;if(ImGui::Combo("Generate in",&side,sides,2)){generator_settings_.outside=side==1;changed=true;}
    changed|=ImGui::Checkbox("Keep buildings and player starts",&generator_protect_structures_);
    changed|=ImGui::Checkbox("Keep existing water",&generator_settings_.keep_water);
    ImGui::Checkbox("Show selected area",&generator_show_selection_);
    ImGui::TextWrapped("%s",generator_selection_label_.c_str());
    ImGui::SeparatorText("2. Choose terrain");
    using P=thandor::TerrainGeneratorPreset;int preset=static_cast<int>(generator_settings_.preset);
    const char* names[]{"Rolling hills","Mountains","Lake","River","Hole / depression","Plateau","Flat / plains"};
    if(ImGui::Combo("Terrain",&preset,names,7)){
        generator_settings_.preset=static_cast<P>(preset);const float defaults[]{2,5,2,1.5F,3,2,0.3F};generator_settings_.height=defaults[preset];changed=true;
        generator_edit_path_=false;generator_drawing_=false;generator_outline_.clear();
        if(generator_region_ready_){refresh_terrain_generator_selection(true);}
    }
    const bool river=generator_settings_.preset==P::River,lake=generator_settings_.preset==P::Lake;
    if(generator_settings_.preset==P::Hills||generator_settings_.preset==P::Mountains||
       generator_settings_.preset==P::Plateau||generator_settings_.preset==P::Plains){
        if(generator_settings_.preset==P::Mountains)ImGui::TextWrapped("Whole-map mountains keep four connected building areas.");
        else changed|=ImGui::Checkbox("Connected building areas",&generator_settings_.connected_building_areas);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Whole-map generation reserves four dry building areas and connecting ground routes.\nWater and protected cells stay unchanged. Apply requires terrain checks to pass.");
    }
    if(river){
        int route=generator_settings_.use_river_path?1:0;const char* routes[]{"Direction","Drawn path"};
        if(ImGui::Combo("River route",&route,routes,2)){
            generator_settings_.use_river_path=route==1;generator_edit_path_=false;generator_drawing_=false;generator_outline_.clear();changed=true;
        }
        if(generator_settings_.use_river_path){
            if(ImGui::Button(generator_settings_.river_path.empty()?"Draw river path":"Redraw river path"))action([&]{
                if(!generator_region_ready_)throw std::runtime_error("Choose an area or Whole map first.");
                discard_terrain_generator_preview();generator_outline_.clear();generator_drawing_=false;generator_edit_path_=true;
                generator_message_="Left-drag an open river route on the map. Release to finish; Escape cancels.";
            });
            if(!generator_settings_.river_path.empty()){ImGui::SameLine();if(ImGui::Button("Clear path")){
                generator_settings_.river_path.clear();generator_edit_path_=false;changed=true;
            }}
        }
        changed|=ImGui::Checkbox("Drivable river",&generator_settings_.drivable_river);
        if(generator_settings_.drivable_river)changed|=ImGui::SliderFloat("Shallow depth",&generator_settings_.shallow_depth,0.02F,0.10F,"%.2f");
        else changed|=ImGui::SliderFloat("River depth",&generator_settings_.height,0.1F,12,"%.2f");
        changed|=ImGui::SliderFloat("Channel width",&generator_settings_.river_width_cells,1,32,"%.1f cells");
        if(ImGui::CollapsingHeader("River shape and crossings")){
        if(!generator_settings_.use_river_path)changed|=ImGui::SliderFloat("Direction",&generator_settings_.river_angle_degrees,0,180,"%.0f deg");
        changed|=ImGui::SliderFloat("Bends",&generator_settings_.river_bend,0,1,"%.2f");
        int fords=static_cast<int>(generator_settings_.ford_count);if(ImGui::SliderInt("Dry crossings",&fords,0,4)){generator_settings_.ford_count=static_cast<unsigned>(fords);changed=true;}
        }
        ImGui::TextWrapped("Shallow depth and gentle banks are checked for ground traversal. Correct red cells before applying as drivable.");
    }else changed|=ImGui::SliderFloat(lake||generator_settings_.preset==P::Depression?"Depth":"Relief",&generator_settings_.height,0,32,"%.2f");
    if(!river){
        int pattern=static_cast<int>(generator_settings_.pattern);const char* patterns[]{"Classic / rolling","Warped / rugged","Cellular / rocky"};
        if(ImGui::Combo("Pattern",&pattern,patterns,3)){generator_settings_.pattern=static_cast<thandor::TerrainNoisePattern>(pattern);changed=true;}
        if(!lake){bool terraces=generator_settings_.terrace_steps!=0;if(ImGui::Checkbox("Terraced heights",&terraces)){
            generator_settings_.terrace_steps=terraces?6U:0U;changed=true;
        }}
        if((generator_settings_.pattern==thandor::TerrainNoisePattern::DomainWarped||(!lake&&generator_settings_.terrace_steps))&&ImGui::CollapsingHeader("Pattern settings")){
            if(generator_settings_.pattern==thandor::TerrainNoisePattern::DomainWarped)changed|=ImGui::SliderFloat("Warp amount",&generator_settings_.warp_strength,0,3,"%.2f");
            if(!lake&&generator_settings_.terrace_steps){int steps=static_cast<int>(generator_settings_.terrace_steps);if(ImGui::SliderInt("Height steps",&steps,2,32)){
                generator_settings_.terrace_steps=static_cast<unsigned>(steps);changed=true;
            }}
        }
    }
    if(!river)changed|=ImGui::InputFloat("Base height",&generator_settings_.base_height);
    if(lake||river){changed|=ImGui::InputFloat("Surface level",&generator_settings_.water_level);ImGui::TextWrapped("Uses the map's current animated surface: %s.",thandor::terrain_surface_family_name(surface_family_));
        if(lake)ImGui::TextWrapped("The water stays below the basin rim. Small areas limit depth to leave gentle banks.");}
    if(generator_settings_.preset==P::Hills||generator_settings_.preset==P::Mountains||generator_settings_.preset==P::Depression)
        changed|=ImGui::Checkbox("Build on existing terrain",&generator_settings_.build_on_existing);
    changed|=ImGui::SliderFloat("Feature size",&generator_settings_.feature_size_cells,2,64,"%.1f cells");
    changed|=ImGui::SliderFloat("Blend at border",&generator_settings_.blend_cells,0,24,"%.1f cells");
    changed|=ImGui::Checkbox("Paint selected ground layer",&generator_settings_.paint_material);
    if(generator_settings_.paint_material){int layer=generator_settings_.material;if(ImGui::SliderInt("Ground layer",&layer,0,25)){generator_settings_.material=static_cast<uint8_t>(layer);changed=true;}}
    changed|=ImGui::InputScalar("Seed",ImGuiDataType_U32,&generator_settings_.seed);
    if(changed)action([&]{discard_terrain_generator_preview();refresh_terrain_generator_selection();generator_message_="Settings changed. Preview to see the result.";});
    ImGui::SeparatorText("3. Preview and apply");
    if(ImGui::Button("Preview"))action([&]{preview_generated_terrain();});
    ImGui::SameLine();if(ImGui::Button("Reroll"))action([&]{generator_settings_.seed=thandor::next_terrain_seed(generator_settings_.seed);preview_generated_terrain();});
    if(ImGui::Button("Random seed + preview"))action([&]{generator_settings_.seed=thandor::next_terrain_seed(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));preview_generated_terrain();});
    if(generator_preview_){if(ImGui::Button("Apply terrain")){
            if(river&&generator_settings_.drivable_river&&generator_preview_->blocked_river_count)generator_message_="Correct the red ground-traversal cells before applying this drivable river.";
            else action([&]{apply_generated_terrain();});}
        ImGui::SameLine();if(ImGui::Button("Discard preview")){discard_terrain_generator_preview();generator_message_="Preview discarded. The map is unchanged.";}}
    if(!generator_message_.empty())ImGui::TextWrapped("%s",generator_message_.c_str());
    ImGui::PopItemWidth();
}
void EditorApp::update_terrain_generator_selection(){
    if(!document_||simulation_mode_||simulation_lab_active())return;
    auto& io=ImGui::GetIO();
    if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&!io.WantTextInput){
        if(generator_drawing_){generator_outline_.clear();generator_drawing_=false;generator_message_="Stroke cancelled.";}
        else discard_terrain_generator_preview();
        generator_edit_path_=false;
        return;
    }
    if(generator_preview_)return;
    const bool can_draw=mouse_in_canvas(io.MousePos.x,io.MousePos.y)&&!io.WantCaptureMouse;
    if(can_draw&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        const auto hit=pick_terrain_cell(document_->field(),canvas_view_ray(io.MousePos.x,io.MousePos.y));
        if(hit){generator_outline_.clear();generator_drawing_=true;generator_stroke_is_path_=generator_edit_path_;}
    }
    if(generator_drawing_&&can_draw&&ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        const auto hit=pick_terrain_cell(document_->field(),canvas_view_ray(io.MousePos.x,io.MousePos.y));
        if(hit){const auto sample=document_->field().sample_terrain(hit->position.x,hit->position.y);const thandor::TerrainRegionPoint point{sample.grid_x,sample.grid_y};
            if(generator_outline_.empty())generator_outline_.push_back(point);
            else if(!generator_stroke_is_path_&&generator_shape_==1){if(generator_outline_.size()==1)generator_outline_.push_back(point);else generator_outline_.back()=point;}
            else if(generator_outline_.size()<(generator_stroke_is_path_?512U:2048U)&&std::hypot(point.x-generator_outline_.back().x,point.y-generator_outline_.back().y)>=(generator_stroke_is_path_?0.75F:0.25F))generator_outline_.push_back(point);
        }
    }
    if(generator_drawing_&&!ImGui::IsMouseDown(ImGuiMouseButton_Left)){
        generator_drawing_=false;
        try{
            if(generator_stroke_is_path_){
                if(generator_outline_.size()<2)throw std::runtime_error("Draw a longer river path.");
                generator_settings_.river_path=generator_outline_;generator_edit_path_=false;
                generator_message_="River path ready. Preview to see the channel.";
                generator_outline_.clear();return;
            }
            auto polygon=generator_outline_;
            if(generator_shape_==1&&polygon.size()==2){const auto a=polygon[0],b=polygon[1];polygon={a,{b.x,a.y},b,{a.x,b.y}};}
            const auto mask=thandor::terrain_polygon_mask(document_->field().width(),document_->field().height(),polygon);
            if(std::none_of(mask.begin(),mask.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("That area is too small. Draw a larger outline.");
            thandor::combine_terrain_region(generator_selection_,mask,static_cast<thandor::TerrainRegionCombine>(generator_combine_));
            generator_region_ready_=true;refresh_terrain_generator_selection(true);generator_message_="Area selected. Choose terrain, then Preview.";
        }catch(const std::exception& e){generator_message_=e.what();}
        generator_outline_.clear();
    }
}
void EditorApp::draw_terrain_generator_overlay(){
    if(!document_||tool_!=Tool::GenerateTerrain)return;
    const auto& f=generator_preview_?generator_preview_->field:document_->field();
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());auto* draw=ImGui::GetBackgroundDrawList();
    const auto project=[&](uint32_t x,uint32_t y,ImVec2& screen){const auto& c=f.at(x,y);float depth{};
        return project_to_canvas({thandor::q12_to_float(c.world_x_q12),thandor::q12_to_float(c.world_y_q12),c.terrain_height()+0.08F},vp,screen.x,screen.y,depth);};
    if(generator_show_selection_&&generator_weights_.size()==f.cells().size())for(uint32_t y=0;y+1<f.height();++y)for(uint32_t x=0;x+1<f.width();++x){
        const auto i=static_cast<std::size_t>(y)*f.width()+x;if(generator_weights_[i]<=0)continue;
        ImVec2 a,b,c,d;if(!project(x,y,a)||!project(x+1,y,b)||!project(x+1,y+1,c)||!project(x,y+1,d))continue;
        const int alpha=static_cast<int>((generator_preview_?15:48)*generator_weights_[i]);draw->AddQuadFilled(a,b,c,d,IM_COL32(65,180,230,alpha));
    }
    if(generator_preview_&&generator_preview_->blocked_river_count)for(uint32_t y=0;y<f.height();++y)for(uint32_t x=0;x<f.width();++x){
        if(!generator_preview_->blocked_river_cells[static_cast<std::size_t>(y)*f.width()+x])continue;
        ImVec2 p;if(project(x,y,p))draw->AddCircleFilled(p,ui_px(2),IM_COL32(255,70,60,210),6);
    }
    if(generator_preview_&&generator_preview_->playability_checked)for(const auto& site:generator_preview_->building_sites){
        ImVec2 p;if(project(static_cast<uint32_t>(site.x),static_cast<uint32_t>(site.y),p))
            draw->AddCircle(p,ui_px(12),IM_COL32(95,225,140,230),24,ui_px(2));
    }
    auto outline=generator_outline_;
    if(!generator_stroke_is_path_&&generator_shape_==1&&outline.size()==2){const auto a=outline[0],b=outline[1];outline={a,{b.x,a.y},b,{a.x,b.y}};}
    const auto point=[&](const thandor::TerrainRegionPoint& p,ImVec2& screen){
        const auto& a=f.at(0,0);const auto& b=f.at(1,0);const auto& c=f.at(0,1);
        const float x=thandor::q12_to_float(a.world_x_q12)+(thandor::q12_to_float(b.world_x_q12-a.world_x_q12))*p.x+(thandor::q12_to_float(c.world_x_q12-a.world_x_q12))*p.y;
        const float y=thandor::q12_to_float(a.world_y_q12)+(thandor::q12_to_float(b.world_y_q12-a.world_y_q12))*p.x+(thandor::q12_to_float(c.world_y_q12-a.world_y_q12))*p.y;
        float depth{};return project_to_canvas({x,y,f.sample_terrain(x,y).height+0.12F},vp,screen.x,screen.y,depth);
    };
    if(generator_settings_.preset==thandor::TerrainGeneratorPreset::River&&generator_settings_.use_river_path){
        const auto& path=generator_settings_.river_path;
        for(std::size_t i=1;i<path.size();++i){ImVec2 a,b;if(point(path[i-1],a)&&point(path[i],b))draw->AddLine(a,b,IM_COL32(70,215,255,220),ui_px(2));}
        if(!path.empty()){ImVec2 a,b;if(point(path.front(),a))draw->AddCircleFilled(a,ui_px(4),IM_COL32(70,215,255,255));
            if(point(path.back(),b))draw->AddCircleFilled(b,ui_px(4),IM_COL32(70,215,255,255));}
    }
    for(std::size_t i=1;i<outline.size();++i){ImVec2 a,b;if(point(outline[i-1],a)&&point(outline[i],b))draw->AddLine(a,b,IM_COL32(255,205,70,255),ui_px(2));}
    if(!generator_stroke_is_path_&&outline.size()>2){ImVec2 a,b;if(point(outline.back(),a)&&point(outline.front(),b))draw->AddLine(a,b,IM_COL32(255,205,70,160),ui_px(1));}
    if(generator_preview_){const auto canvas=workspace_layout().canvas;draw->AddText({canvas.x+ui_px(14),canvas.y+ui_px(12)},IM_COL32(255,215,100,255),"TERRAIN PREVIEW · Apply or discard in Terrain");}
}
} // namespace editor
