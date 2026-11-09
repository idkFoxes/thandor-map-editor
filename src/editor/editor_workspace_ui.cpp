#include "editor_app.hpp"
#include <imgui.h>
#include <array>
#include <cmath>
#include <cstdio>

namespace editor {
namespace {
constexpr ImGuiWindowFlags kWorkspaceWindow = ImGuiWindowFlags_NoDecoration |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

bool ribbon_button(const char* label, int icon, bool selected, bool compact, float width) {
    ImGui::PushID(label);
    const auto p = ImGui::GetCursorScreenPos();
    const ImVec2 size{width, compact ? ui_px(26.0F) : ui_px(52.0F)};
    const bool clicked = ImGui::InvisibleButton("##action", size);
    const bool hovered = ImGui::IsItemHovered();
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, {p.x+size.x,p.y+size.y}, selected ? IM_COL32(28,91,151,255) :
        hovered ? IM_COL32(37,55,77,255) : IM_COL32(23,32,45,255), 4);
    d->AddRect(p,{p.x+size.x,p.y+size.y},selected ? IM_COL32(62,164,255,255) :
        IM_COL32(48,65,85,255),4);
    const ImVec2 c{p.x+width*0.5F,p.y+(compact?ui_px(13.0F):ui_px(17.0F))};
    const ImU32 ink=IM_COL32(213,230,249,255), blue=IM_COL32(72,176,255,255);
    const int icon_vertex_start=d->VtxBuffer.Size;
    switch(icon){
        case 0: d->AddTriangleFilled({c.x-6,c.y-9},{c.x-5,c.y+8},{c.x+8,c.y+2},ink);break;
        case 1:
            d->AddLine({c.x-9,c.y},{c.x+9,c.y},ink,2);d->AddLine({c.x,c.y-9},{c.x,c.y+9},ink,2);
            d->AddTriangleFilled({c.x+10,c.y},{c.x+5,c.y-4},{c.x+5,c.y+4},blue);
            d->AddTriangleFilled({c.x,c.y-10},{c.x-4,c.y-5},{c.x+4,c.y-5},blue);break;
        case 2: d->AddCircle(c,8,ink,20,2);
            d->AddTriangleFilled({c.x+9,c.y-4},{c.x+4,c.y-10},{c.x+13,c.y-9},blue);break;
        case 3: case 4:
            d->AddTriangleFilled({c.x,c.y+(icon==3?-9:9)},
                {c.x-9,c.y+(icon==3?7:-7)},{c.x+9,c.y+(icon==3?7:-7)},blue);break;
        case 5: d->AddBezierCubic({c.x-10,c.y+4},{c.x-5,c.y-10},
                {c.x+5,c.y+10},{c.x+10,c.y-4},ink,2);break;
        case 6: d->AddLine({c.x-10,c.y+6},{c.x+10,c.y+6},blue,3);
            d->AddLine({c.x,c.y-8},{c.x,c.y+3},ink,2);break;
        case 7: d->AddCircleFilled({c.x,c.y+2},7,blue,16);
            d->AddTriangleFilled({c.x,c.y-11},{c.x-6,c.y},{c.x+6,c.y},blue);break;
        case 8: d->AddCircle({c.x-5,c.y},6,IM_COL32(225,184,72,255),16,2);
            d->AddCircle({c.x+5,c.y},6,blue,16,2);break;
        case 9: d->AddRectFilled({c.x-9,c.y-9},{c.x,c.y},blue);
            d->AddRectFilled({c.x,c.y},{c.x+9,c.y+9},blue);
            d->AddRect({c.x-9,c.y-9},{c.x+9,c.y+9},ink);break;
        case 10: case 11: d->AddLine({c.x-6,c.y-9},{c.x-6,c.y+10},ink,2);
            d->AddTriangleFilled({c.x-4,c.y-9},{c.x+9,c.y-4},{c.x-4,c.y+1},
                icon==10 ? IM_COL32(241,151,83,255) : blue);break;
        case 13:
            d->AddTriangleFilled({c.x-11,c.y+8},{c.x-3,c.y-7},{c.x+5,c.y+8},ink);
            d->AddTriangleFilled({c.x-1,c.y+8},{c.x+6,c.y-3},{c.x+13,c.y+8},blue);
            d->AddLine({c.x+7,c.y-11},{c.x+7,c.y-5},ink,1.5F);
            d->AddLine({c.x+4,c.y-8},{c.x+10,c.y-8},ink,1.5F);break;
        default: d->AddCircle(c,8,ink,20,1.5F);
            d->AddLine({c.x-11,c.y},{c.x+11,c.y},blue);d->AddLine({c.x,c.y-11},{c.x,c.y+11},blue);break;
    }
    for(int v=icon_vertex_start;v<d->VtxBuffer.Size;++v){
        auto& pos=d->VtxBuffer[v].pos;
        pos={c.x+(pos.x-c.x)*editor_ui_scale_factor,c.y+(pos.y-c.y)*editor_ui_scale_factor};
    }
    if(!compact){const auto ts=ImGui::CalcTextSize(label);
        d->AddText({p.x+(width-ts.x)*0.5F,p.y+size.y-ts.y-ui_px(4)},ink,label);}
    if(hovered)ImGui::SetTooltip("%s",label);
    ImGui::PopID();return clicked;
}
} // namespace

EditorWorkspaceLayout EditorApp::workspace_layout() const noexcept {
    const auto& io=ImGui::GetIO();
    return EditorWorkspaceLayout::calculate(io.DisplaySize.x,io.DisplaySize.y,
        show_left_panel_,show_right_panel_,compact_ribbon_,overview_expanded_,editor_ui_scale_factor);
}
bool EditorApp::project_to_canvas(Vec3f point,const Mat4& vp,float& x,float& y,float& depth) const {
    return project_to_workspace(point,vp,workspace_layout().canvas,x,y,depth);
}
ViewRay EditorApp::canvas_view_ray(float x,float y) const {
    return workspace_view_ray(camera_,x,y,workspace_layout().canvas);
}
bool EditorApp::begin_inspector_tab(const char* label,int index){
    if((inspector_page_==0&&index>=4)||(inspector_page_>=4&&index!=inspector_page_))return false;
    const bool requested=requested_inspector_tab_==index;
    const bool open=ImGui::BeginTabItem(label,nullptr,requested?ImGuiTabItemFlags_SetSelected:0);
    if(requested&&open)requested_inspector_tab_=-1;
    return open;
}
void EditorApp::choose_workspace_tool(Tool tool,int tab){
    tool_=tool;stroke_active_=false;show_right_panel_=true;requested_inspector_tab_=tab;
    if(tool==Tool::Water){draw_water_=true;ensure_water_selection_size();}
}

void EditorApp::draw_workspace_ribbon(){
    const auto r=workspace_layout().ribbon;
    const bool compact=compact_ribbon_||r.width<ui_px(850.0F);
    ImGui::SetNextWindowPos({r.x,r.y});ImGui::SetNextWindowSize({r.width,r.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2{ui_px(8),ui_px(6)});
    ImGui::Begin("##workspace-ribbon",nullptr,kWorkspaceWindow|ImGuiWindowFlags_NoScrollbar);
    if(!show_left_panel_){if(ImGui::SmallButton("Assets >"))show_left_panel_=true;ImGui::SameLine();}
    if(!show_right_panel_){if(ImGui::SmallButton("< Inspector"))show_right_panel_=true;ImGui::SameLine();}
    if(ImGui::SmallButton(compact_ribbon_?"Expand tools":"Compact tools"))compact_ribbon_=!compact_ribbon_;
    if(!compact)ImGui::SameLine();
    if(!compact)ImGui::TextDisabled("%s   |   W Move  /  E Rotate","Editing tools");
    // A horizontal child preserves every tool on narrow windows.
    if(compact)ImGui::SameLine();
    ImGui::BeginChild("##workspace-tools",{0,compact?ui_px(42.0F):ui_px(56.0F)},false,
        ImGuiWindowFlags_HorizontalScrollbar);
    constexpr std::array<const char*,14> labels{{"Select","Move","Rotate","Raise","Lower",
        "Smooth","Flatten","Water","Fields","Texture","AI Start","Player","Center","Generate"}};
    constexpr std::array<Tool,14> tools{{Tool::Select,Tool::Select,Tool::Select,Tool::Raise,
        Tool::Lower,Tool::Smooth,Tool::Flatten,Tool::Water,Tool::ResourceField,Tool::Texture,
        Tool::PlayerStart,Tool::PlayerStart,Tool::Select,Tool::GenerateTerrain}};
    const float width=compact?ui_px(32.0F):std::clamp((r.width-ui_px(16.0F)-13.0F*ui_px(6.0F))/14.0F,ui_px(58.0F),ui_px(84.0F));
    for(std::size_t i=0;i<labels.size();++i){
        if(i)ImGui::SameLine();
        const bool selected=(i<=2&&tool_==Tool::Select&&workspace_transform_action_==static_cast<int>(i))||
            (i>=3&&i<=9&&tool_==tools[i])||(i==13&&tool_==Tool::GenerateTerrain);
        if(!ribbon_button(labels[i],static_cast<int>(i),selected,compact,width))continue;
        if(i==12){center_camera();continue;}
        if(i<=2)workspace_transform_action_=static_cast<int>(i);
        if(i==1||i==2)gizmo_mode_=i==1?GizmoMode::Translate:GizmoMode::Rotate;
        if(i==10||i==11){
            const auto factions=document_->player_ai_configuration();
            if((i==10&&factions.active_factions<=factions.human_assignable_factions)||
               (i==11&&factions.human_assignable_factions==0U)){
                status_="Configure the matching human or computer faction in Players before placing a start.";
                show_right_panel_=true;requested_inspector_tab_=2;continue;
            }
            selected_faction_=i==10?factions.human_assignable_factions+1U:1U;
            bool found_start=false;
            for(const auto& preset:catalog_.army_presets())
                if(preset.visual_kind==thandor::PlacementVisualKind::PlayerStart&&preset.browser_visible){
                    selected_asset_id_=preset.registry_id;found_start=true;break;
                }
            if(!found_start){status_="No placeable start asset is available.";continue;}
            choose_workspace_tool(Tool::PlayerStart,2);
        }else choose_workspace_tool(tools[i],i<=2?0:1);
    }
    ImGui::EndChild();ImGui::End();ImGui::PopStyleVar();
}

void EditorApp::draw_asset_categories(std::span<const uint32_t> ids){
    ImGui::BeginChild("##asset-categories",{0,ui_px(178)},true);
    const auto category=[&](const char* label,int index){
        if(ImGui::Selectable(label,asset_category_==index))asset_category_=index;
    };
    if(ImGui::TreeNodeEx("Terrain",ImGuiTreeNodeFlags_DefaultOpen)){
        category("Ground textures",0);category("World / scenery",1);ImGui::TreePop();
    }
    category("Resources",2);category("Buildings",3);category("Units",4);category("Map items / effects",5);
    ImGui::EndChild();
    constexpr std::array<const char*,6> names{{"Ground textures","World / scenery","Resources",
        "Buildings","Units","Map items / effects"}};
    asset_category_=std::clamp(asset_category_,0,5);
    ImGui::SeparatorText(names[static_cast<std::size_t>(asset_category_)]);
    if(asset_category_==0){
        ImGui::BeginChild("##texture-library",{0,0},false,ImGuiWindowFlags_AlwaysVerticalScrollbar);
        draw_material_palette(true);ImGui::EndChild();return;
    }
    std::vector<uint32_t> filtered;
    for(uint32_t id:ids){const auto* p=catalog_.army_preset(id);if(!p)continue;
        const bool matches=(asset_category_==1&&p->browser_kind==thandor::PlacementBrowserKind::Element)||
            (asset_category_==2&&thandor::placement_is_resource(*p))||
            (asset_category_==3&&p->browser_kind==thandor::PlacementBrowserKind::Building)||
            (asset_category_==4&&p->browser_kind==thandor::PlacementBrowserKind::Unit)||
            (asset_category_==5&&p->browser_kind==thandor::PlacementBrowserKind::General);
        if(matches)filtered.push_back(id);
    }
    if(asset_category_==3||asset_category_==4)draw_global_faction_selector("category-faction");
    draw_placement_presets(filtered,std::nullopt);
}

void EditorApp::draw_selected_object_preview(const thandor::ArmyAssetPreset& preset,uint32_t faction){
    const auto p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##selected-preview",{ui_px(80),ui_px(80)});
    auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(p,{p.x+ui_px(80),p.y+ui_px(80)},IM_COL32(14,22,31,255),4);
    d->AddRect(p,{p.x+ui_px(80),p.y+ui_px(80)},IM_COL32(55,79,106,255),4);
    struct RestoreFaction{uint32_t& value;uint32_t previous;~RestoreFaction(){value=previous;}};
    RestoreFaction restore{selected_faction_,selected_faction_};
    selected_faction_=std::min(faction,7U);
    try{
        const auto& mesh=asset_preview(preset);
        constexpr uint32_t samples=32;
        if(!mesh.raster_argb.empty()&&mesh.raster_width&&mesh.raster_height)
            for(uint32_t y=0;y<samples;++y)for(uint32_t x=0;x<samples;++x){
                const auto ix=std::min(mesh.raster_width-1U,x*mesh.raster_width/samples);
                const auto iy=std::min(mesh.raster_height-1U,y*mesh.raster_height/samples);
                const uint32_t argb=mesh.raster_argb[static_cast<std::size_t>(iy)*mesh.raster_width+ix];
                if((argb>>24U)==0)continue;
                const ImU32 color=IM_COL32((argb>>16U)&255U,(argb>>8U)&255U,argb&255U,argb>>24U);
                d->AddRectFilled({p.x+ui_px(4)+ui_px(72.0F)*x/samples,p.y+ui_px(4)+ui_px(72.0F)*y/samples},
                    {p.x+ui_px(4)+ui_px(72.0F)*(x+1U)/samples,p.y+ui_px(4)+ui_px(72.0F)*(y+1U)/samples},color);
            }
    }catch(const std::exception&){d->AddText({p.x+ui_px(6),p.y+ui_px(30)},IM_COL32(134,155,180,255),"Preview");}
    ImGui::SameLine();ImGui::BeginChild("##selected-summary",{0,ui_px(80)},false);
    ImGui::TextWrapped("%s",preset.display_name.c_str());
    ImGui::TextDisabled("Faction %u",faction);
    ImGui::EndChild();
}

void EditorApp::draw_map_overview(){
    const auto r=workspace_layout().overview;
    ImGui::SetNextWindowPos({r.x,r.y});ImGui::SetNextWindowSize({r.width,r.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2{ui_px(10),ui_px(5)});
    ImGui::Begin("##workspace-overview",nullptr,kWorkspaceWindow);
    if(ImGui::SmallButton(overview_expanded_?"v  Map Overview":">  Map Overview"))overview_expanded_=!overview_expanded_;
    if(!overview_expanded_){ImGui::End();ImGui::PopStyleVar();return;}
    const auto& f=document_->field();
    const auto placements=document_->level().placements();
    const auto& first=f.at(0,0);const auto& last=f.at(f.width()-1U,f.height()-1U);
    const float xmin=std::min(thandor::q12_to_float(first.world_x_q12),thandor::q12_to_float(last.world_x_q12));
    const float xmax=std::max(thandor::q12_to_float(first.world_x_q12),thandor::q12_to_float(last.world_x_q12));
    const float ymin=std::min(thandor::q12_to_float(first.world_y_q12),thandor::q12_to_float(last.world_y_q12));
    const float ymax=std::max(thandor::q12_to_float(first.world_y_q12),thandor::q12_to_float(last.world_y_q12));
    const float spanx=std::max(xmax-xmin,0.01F),spany=std::max(ymax-ymin,0.01F);
    const float mini_scale=std::min(ui_px(190.0F)/spanx,
        std::max(ui_px(32.0F),std::min(ui_px(132.0F),r.height-ui_px(46.0F)))/spany);
    const float mini_height=mini_scale*spany,mini_width=mini_scale*spanx;
    const auto p=ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##map-minimap",{mini_width,mini_height});
    if(ImGui::IsItemClicked()){
        const auto mouse=ImGui::GetIO().MousePos;
        camera_.target.x=xmin+std::clamp((mouse.x-p.x)/mini_width,0.0F,1.0F)*spanx;
        camera_.target.y=ymax-std::clamp((mouse.y-p.y)/mini_height,0.0F,1.0F)*spany;
        camera_.target.z=terrain_height_near(camera_.target.x,camera_.target.y);
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Click to center the camera here");
    auto* d=ImGui::GetWindowDrawList();
    d->AddRectFilled(p,{p.x+mini_width,p.y+mini_height},IM_COL32(0,0,0,255));
    constexpr int samples=40;
    for(int y=0;y<samples;++y)for(int x=0;x<samples;++x){
        const float wx=xmin+(static_cast<float>(x)+0.5F)/samples*spanx;
        const float wy=ymax-(static_cast<float>(y)+0.5F)/samples*spany;
        const auto sample=f.sample_terrain(wx,wy);
        if(!sample.inside)continue;
        const auto cx=static_cast<uint32_t>(std::clamp(static_cast<int>(sample.grid_x),0,static_cast<int>(f.width()-1U)));
        const auto cy=static_cast<uint32_t>(std::clamp(static_cast<int>(sample.grid_y),0,static_cast<int>(f.height()-1U)));
        const auto& cell=f.at(cx,cy);
        ImU32 color=IM_COL32(83,106,80,255);
        if(overview_heightmap_){const int shade=static_cast<int>(std::clamp(100.0F+sample.height*5.0F,25.0F,220.0F));
            color=IM_COL32(shade,shade,shade,255);
        }else if(cell.material()<material_images_.size()){
            const auto& image=material_images_[cell.material()];
            if(image.pixels.size()>=4U){const auto at=image.pixels.size()/8U*4U;
                color=IM_COL32(image.pixels[at],image.pixels[at+1],image.pixels[at+2],255);}
        }
        if(overview_resources_&&(cell.flags_and_material&0x1800U))
            color=(cell.flags_and_material&0x0800U)?IM_COL32(219,173,65,255):IM_COL32(64,173,235,255);
        d->AddRectFilled({p.x+mini_width*x/samples,p.y+mini_height*y/samples},
            {p.x+mini_width*(x+1)/samples,p.y+mini_height*(y+1)/samples},color);
    }
    if(overview_grid_)for(int i=1;i<8;++i){
        d->AddLine({p.x+mini_width*i/8,p.y},{p.x+mini_width*i/8,p.y+mini_height},IM_COL32(206,225,244,65));
        d->AddLine({p.x,p.y+mini_height*i/8},{p.x+mini_width,p.y+mini_height*i/8},IM_COL32(206,225,244,65));}
    const auto map_point=[&](float x,float y){return ImVec2{p.x+(x-xmin)/spanx*mini_width,p.y+(ymax-y)/spany*mini_height};};
    if(overview_players_)for(const auto& placement:placements){
        const auto* preset=catalog_.army_preset(placement.army_asset_id);
        if(!preset||preset->visual_kind!=thandor::PlacementVisualKind::PlayerStart)continue;
        d->AddCircleFilled(map_point(placement.render_x(),placement.render_y()),4,IM_COL32(92,186,255,255),12);
    }
    d->AddCircle(map_point(camera_.target.x,camera_.target.y),5,IM_COL32(255,255,255,255),16,1.5F);
    d->AddRect(p,{p.x+mini_width,p.y+mini_height},IM_COL32(75,108,141,255));
    ImGui::SameLine();ImGui::BeginChild("##overview-layers",{ui_px(125),mini_height},false);
    ImGui::TextDisabled("LAYERS");
    ImGui::Checkbox("Players",&overview_players_);ImGui::Checkbox("Resources",&overview_resources_);
    ImGui::Checkbox("Grid",&overview_grid_);ImGui::Checkbox("Heightmap",&overview_heightmap_);
    ImGui::EndChild();
    if(r.width>ui_px(620.0F)){
        ImGui::SameLine();ImGui::BeginChild("##overview-starts",{std::max(ui_px(170.0F),r.width-ui_px(560.0F)),mini_height},false);
        ImGui::TextDisabled("PLAYER / AI STARTS");
        bool any=false;
        for(std::size_t i=0;i<placements.size();++i){const auto& placement=placements[i];
            const auto* preset=catalog_.army_preset(placement.army_asset_id);
            if(!preset||preset->visual_kind!=thandor::PlacementVisualKind::PlayerStart)continue;
            any=true;ImGui::PushID(static_cast<int>(i));char label[96]{};
            std::snprintf(label,sizeof(label),"Faction %u    %.1f, %.1f",placement.faction_index,placement.render_x(),placement.render_y());
            if(ImGui::Selectable(label,selected_placement_==i)){
                selected_placement_=i;camera_.target={placement.render_x(),placement.render_y(),
                    terrain_height_near(placement.render_x(),placement.render_y())};
                choose_workspace_tool(Tool::Select,0);
            }
            ImGui::PopID();
        }
        if(!any)ImGui::TextDisabled("No starts placed.");
        ImGui::EndChild();
    }
    if(r.width>ui_px(850.0F)){
        ImGui::SameLine();ImGui::BeginChild("##overview-map-info",{0,mini_height},false);
        ImGui::TextDisabled("MAP INFO");ImGui::Text("%u x %u cells",f.width(),f.height());
        ImGui::Text("%zu objects",placements.size());
        uint32_t xenite=0,tritium=0;for(const auto& cell:f.cells()){
            if(cell.flags_and_material&0x0800U)++xenite;
            if(cell.flags_and_material&0x1000U)++tritium;}
        ImGui::Text("Xenite: %u cells",xenite);ImGui::Text("Tritium: %u cells",tritium);
        if(ImGui::SmallButton("Map settings")){show_right_panel_=true;requested_inspector_tab_=4;}
        ImGui::EndChild();
    }
    ImGui::End();ImGui::PopStyleVar();
}
} // namespace editor
