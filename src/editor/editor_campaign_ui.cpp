#include "editor_app.hpp"
#include "editor_paths.hpp"
#include "thandor/frontend_player_texture.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <numbers>
#include <cstring>
#include <set>

namespace editor {
namespace {
bool edit_string(const char* label,std::string& value,std::size_t capacity=2048) {
    std::vector<char> buffer(std::max(capacity,value.size()+1));
    std::copy(value.begin(),value.end(),buffer.begin());
    if(!ImGui::InputText(label,buffer.data(),buffer.size()))return false;
    value=buffer.data();return true;
}
std::string mission_lev_path(const thandor::CampaignLevelRecord& record) {
    return "level/"+thandor::normalize_asset_path(record.level_path())+".lev";
}
}
void EditorApp::refresh_campaign_labels() {
    campaign_mission_labels_.clear();if(!campaign_document_)return;
    for(const auto& record:campaign_document_->campaign().records())
        campaign_mission_labels_.push_back(std::to_string(record.level_id())+" - "+record.level_path());
    if(campaign_mission_labels_.empty()){campaign_selected_mission_=0;campaign_map_path_[0]='\0';campaign_validation_={};return;}
    if(campaign_selected_mission_<0){
        const auto& records=campaign_document_->campaign().records();
        const auto start=std::find_if(records.begin(),records.end(),[&](const auto& record){
            return record.level_id()==campaign_document_->campaign().first_level_id();
        });
        campaign_selected_mission_=start==records.end()?0:static_cast<int>(start-records.begin());
    }
    campaign_selected_mission_=std::clamp(campaign_selected_mission_,0,static_cast<int>(campaign_mission_labels_.size())-1);
    const auto& source=campaign_document_->source(campaign_document_->campaign().records()[static_cast<std::size_t>(campaign_selected_mission_)].level_id());
    std::snprintf(campaign_map_path_,sizeof(campaign_map_path_),"%s",source.map_project.string().c_str());
    campaign_validation_={};
}
void EditorApp::open_campaign_mission(std::size_t index) {
    if(!campaign_document_||index>=campaign_document_->campaign().records().size())return;
    if(document_&&document_->dirty())throw std::runtime_error("Save the current map before opening another campaign mission.");
    const auto& record=campaign_document_->campaign().records()[index];
    const auto& source=campaign_document_->source(record.level_id());
    if(!source.map_project.empty()) {
        if(!load_map_from_path(source.map_project))throw std::runtime_error(status_);
    }else {
        const auto wanted=mission_lev_path(record);bool found=false;
        for(std::size_t i=0;i<catalog_.map_groups().size();++i)if(thandor::normalize_asset_path(catalog_.map_groups()[i].level_path)==wanted) {
            const auto* previous=document_.get();load_map(i);found=document_.get()!=previous;
            break;
        }
        if(!found)throw std::runtime_error("Campaign mission map is not installed: "+wanted);
    }
    current_stock_group_.reset(); // Campaign editing saves a project rather than modifying stock archives.
    campaign_editing_mission_=record.level_id();campaign_selected_mission_=static_cast<int>(index);
    if(source.map_project.empty()) {
        const auto root=campaign_project_path_.empty()?project_maps_directory()/"campaigns"/campaign_document_->identifier():campaign_project_path_.parent_path();
        const auto target=root/"maps"/(document_->map_name()+".thmap");
        std::snprintf(campaign_map_path_,sizeof(campaign_map_path_),"%s",target.string().c_str());
    }else std::snprintf(campaign_map_path_,sizeof(campaign_map_path_),"%s",source.map_project.string().c_str());
    campaign_message_="Mission opened. Edit conditions in the Map inspector, then save its mission project here.";
}
void EditorApp::save_campaign_mission(const std::filesystem::path& path) {
    if(!document_||!campaign_document_)throw std::runtime_error("Open a campaign and a map first.");
    if(path.empty())throw std::runtime_error("Enter a mission project path.");
    const auto absolute=std::filesystem::absolute(path).lexically_normal();
    if(thandor::lower_ascii(absolute.extension().string())!=".thmap")throw std::runtime_error("Mission projects use the .thmap extension.");
    const auto id=campaign_document_->campaign().records()[static_cast<std::size_t>(campaign_selected_mission_)].level_id();
    if(campaign_editing_mission_!=id)throw std::runtime_error("Open this mission before saving its edits.");
    if(std::filesystem::exists(absolute)&&absolute!=std::filesystem::absolute(current_project_path_).lexically_normal())
        throw std::runtime_error("That file already exists. Open it or choose a new mission project path.");
    auto updated_record=campaign_document_->campaign().record(static_cast<std::size_t>(campaign_selected_mission_));
    updated_record.set_level_path(document_->map_name());
    document_->save_project(absolute,true); // No skirmish resource/faction repair.
    auto& source=campaign_document_->source(id);source.map_project=absolute;
    campaign_document_->campaign().record(static_cast<std::size_t>(campaign_selected_mission_))=updated_record;
    current_project_path_=absolute;last_opened_map_type_="file";last_opened_map_value_=absolute.string();document_->mark_clean();campaign_dirty_=true;
    refresh_campaign_labels();campaign_message_="Mission project saved. Save the campaign project to retain this binding.";
}
void EditorApp::draw_campaign_exit_zones() {
    if(!campaign_show_exit_zones_||!campaign_document_||!campaign_editing_mission_||!document_)return;
    const auto* record=campaign_document_->campaign().find_level(*campaign_editing_mission_);if(!record)return;
    const auto vp=camera_.view_projection(workspace_layout().canvas.aspect());
    auto* draw=ImGui::GetBackgroundDrawList();
    for(unsigned faction=0;faction<8;++faction) {
        const auto zone=thandor::campaign_exit_zone_render(record->exit_zone(faction));if(zone[2]<=0)continue;
        const auto rgb=thandor::graphics_family_representative_rgb24(graphics_family_for_faction(faction));
        const auto color=IM_COL32((rgb>>16)&255,(rgb>>8)&255,rgb&255,220);
        auto projected=[&](float x,float y,float& screen_x,float& screen_y) {
            const auto sample=document_->field().sample_terrain(x,y);float depth{};
            return project_to_canvas({x,y,sample.height+0.15F},vp,screen_x,screen_y,depth);
        };
        for(unsigned segment=0;segment<64;++segment) {
            const float a=2.0F*std::numbers::pi_v<float>*static_cast<float>(segment)/64.0F;
            const float b=2.0F*std::numbers::pi_v<float>*static_cast<float>(segment+1)/64.0F;
            float x1{},y1{},x2{},y2{};
            if(projected(zone[0]+std::cos(a)*zone[2],zone[1]+std::sin(a)*zone[2],x1,y1)&&
               projected(zone[0]+std::cos(b)*zone[2],zone[1]+std::sin(b)*zone[2],x2,y2))
                draw->AddLine({x1,y1},{x2,y2},color,ui_px(faction==static_cast<unsigned>(campaign_zone_faction_)?2.5F:1.5F));
        }
        float x{},y{};if(projected(zone[0],zone[1],x,y)) {
            char label[48];std::snprintf(label,sizeof(label),"Faction %u exit",faction);
            draw->AddText({x+ui_px(5),y},color,label);
        }
    }
}
void EditorApp::draw_campaign_trigger_destination(uint8_t outcome) {
    if(!campaign_document_||!campaign_editing_mission_)return;
    if(!campaign_document_->campaign().find_level(*campaign_editing_mission_))return;
    ImGui::TextWrapped("%s: %s",campaign_document_->outcome_label(*campaign_editing_mission_,outcome).c_str(),
        campaign_document_->continuation_label(*campaign_editing_mission_,outcome).c_str());
}
void EditorApp::draw_campaign_window() {
    if(!show_campaign_window_)return;
    if(campaign_native_paths_.empty()) {
        std::set<std::string> paths;
        for(const auto& asset:catalog_.assets())if(asset.normalized_path.ends_with(".cgn"))paths.insert(asset.normalized_path);
        campaign_native_paths_.assign(paths.begin(),paths.end());
        try{campaign_package_slot_=static_cast<int>(thandor::CampaignDocument::first_free_package_slot(catalog_.game_root()));}catch(const std::exception& e){campaign_message_=e.what();}
    }
    ImGui::SetNextWindowSize({ui_px(900),ui_px(700)},ImGuiCond_FirstUseEver);
    if(!ImGui::Begin(campaign_dirty_?"Campaign *###CampaignWorkspace":"Campaign###CampaignWorkspace",&show_campaign_window_)){ImGui::End();return;}
    auto attempt=[&](auto action){try{action();}catch(const std::exception& e){campaign_message_=e.what();}};
    if(ImGui::BeginTabBar("CampaignTabs")) {
        if(ImGui::BeginTabItem("Project")) {
            ImGui::SeparatorText("Open or create");
            ImGui::InputText("Campaign project",campaign_file_path_,sizeof(campaign_file_path_));
            if(ImGui::Button("Open project"))attempt([&]{
                if(campaign_dirty_)throw std::runtime_error("Save the current campaign project before opening another.");
                auto loaded=thandor::CampaignDocument::open_project(campaign_file_path_);
                campaign_document_=std::move(loaded);campaign_project_path_=std::filesystem::absolute(campaign_file_path_);
                campaign_selected_mission_=-1;campaign_editing_mission_.reset();refresh_campaign_labels();campaign_message_="Campaign project opened.";
            });
            ImGui::SameLine();if(ImGui::Button("Save project"))attempt([&]{
                if(!campaign_document_)throw std::runtime_error("Create or import a campaign first.");
                if(campaign_file_path_[0]=='\0')throw std::runtime_error("Enter a campaign project path ending in .thcampaign.");
                const auto path=std::filesystem::absolute(campaign_file_path_);
                if(thandor::lower_ascii(path.extension().string())!=".thcampaign")throw std::runtime_error("Campaign projects use the .thcampaign extension.");
                campaign_document_->save_project(path);campaign_project_path_=path;campaign_dirty_=false;campaign_message_="Campaign project saved. Mission projects remain separate referenced files.";
            });
            if(!campaign_native_paths_.empty()) {
                if(ImGui::BeginCombo("Game campaign",campaign_native_paths_[static_cast<std::size_t>(campaign_import_index_)].c_str())) {
                    for(std::size_t i=0;i<campaign_native_paths_.size();++i)if(ImGui::Selectable(campaign_native_paths_[i].c_str(),campaign_import_index_==static_cast<int>(i)))campaign_import_index_=static_cast<int>(i);
                    ImGui::EndCombo();
                }
                if(ImGui::Button("Import game campaign"))attempt([&]{
                    if(campaign_dirty_)throw std::runtime_error("Save the current campaign project before importing another.");
                    auto imported=thandor::CampaignDocument::import_native(catalog_.game_root(),campaign_native_paths_[static_cast<std::size_t>(campaign_import_index_)]);
                    campaign_document_=std::move(imported);campaign_project_path_.clear();campaign_editing_mission_.reset();campaign_selected_mission_=-1;campaign_dirty_=true;
                    const auto path=project_maps_directory()/"campaigns"/campaign_document_->identifier()/(campaign_document_->identifier()+".thcampaign");
                    std::snprintf(campaign_file_path_,sizeof(campaign_file_path_),"%s",path.string().c_str());
                    refresh_campaign_labels();campaign_message_="Imported. Use a new identifier when exporting a separate campaign.";
                });
                ImGui::InputText("New identifier",campaign_new_identifier_,sizeof(campaign_new_identifier_));
                if(ImGui::Button("New campaign with current map"))attempt([&]{
                    if(campaign_dirty_)throw std::runtime_error("Save the current campaign project before creating another.");
                    if(!document_)throw std::runtime_error("Open or create the first mission map.");
                    const auto base=thandor::CampaignDocument::import_native(catalog_.game_root(),campaign_native_paths_[static_cast<std::size_t>(campaign_import_index_)]);
                    auto created=thandor::CampaignDocument::create(base,campaign_new_identifier_);
                    const auto root=project_maps_directory()/"campaigns"/created.identifier();
                    // A new campaign owns its first map and FLD name. Reusing the
                    // stock identity makes safe package export reject that field.
                    auto first_map=*document_;
                    first_map.set_map_name(created.identifier().substr(0,24)+"_01");
                    const auto path=root/"maps"/(first_map.map_name()+".thmap");
                    if(std::filesystem::exists(path))throw std::runtime_error("This campaign folder already contains the mission project. Choose a new identifier or open the campaign project.");
                    const auto first=thandor::CampaignLevelRecord::minimal(1,first_map.map_name());
                    created.campaign()=thandor::CampaignAsset::from_template(created.campaign(),1,std::span(&first,1));
                    std::filesystem::create_directories(path.parent_path());first_map.save_project(path,true);
                    reset_simulation(false);simulation_mode_=false;
                    document_=std::make_unique<thandor::MapDocument>(std::move(first_map));
                    selected_placement_.reset();selected_edit_index_.reset();
                    refresh_scenario_text_buffers();rebuild_mesh();
                    created.source(1).map_project=std::filesystem::absolute(path);campaign_document_=std::move(created);
                    current_project_path_=path;last_opened_map_type_="file";last_opened_map_value_=std::filesystem::absolute(path).string();current_stock_group_.reset();document_->mark_clean();campaign_editing_mission_=1;
                    campaign_project_path_=root/(campaign_document_->identifier()+".thcampaign");
                    std::snprintf(campaign_file_path_,sizeof(campaign_file_path_),"%s",campaign_project_path_.string().c_str());
                    campaign_selected_mission_=0;campaign_dirty_=true;refresh_campaign_labels();campaign_message_="Campaign created with a saved copy of the current map. Add missions in the Missions tab.";
                });
            }
            if(campaign_document_) {
                ImGui::SeparatorText("Campaign details");
                auto identifier=campaign_document_->identifier();
                if(edit_string("Identifier",identifier,64))attempt([&]{campaign_document_->set_identifier(identifier);campaign_dirty_=true;});
                campaign_dirty_|=edit_string("Title",campaign_document_->title);
                std::vector<char> description(std::max<std::size_t>(16384,campaign_document_->description.size()+1));
                std::copy(campaign_document_->description.begin(),campaign_document_->description.end(),description.begin());
                if(ImGui::InputTextMultiline("Description",description.data(),description.size(),{0,ui_px(120)})){campaign_document_->description=description.data();campaign_dirty_=true;}
                ImGui::TextWrapped("The title and description are written to all existing game languages. The campaign project references its mission .thmap files; keep them together when moving the project.");
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Missions")) {
            if(!campaign_document_)ImGui::TextWrapped("Open, import or create a campaign in the Project tab.");
            else {
                auto& campaign=campaign_document_->campaign();
                const auto index=static_cast<std::size_t>(campaign_selected_mission_);
                if(ImGui::BeginCombo("Mission",campaign_mission_labels_[index].c_str())) {
                    for(std::size_t i=0;i<campaign_mission_labels_.size();++i)if(ImGui::Selectable(campaign_mission_labels_[i].c_str(),i==index)) {
                        campaign_selected_mission_=static_cast<int>(i);refresh_campaign_labels();
                    }
                    ImGui::EndCombo();
                }
                // Selection may have changed above; resolve after the combo.
                const auto selected=static_cast<std::size_t>(campaign_selected_mission_);
                auto& record=campaign.record(selected);const auto id=record.level_id();
                if(ImGui::BeginCombo("Starting mission",campaign_mission_labels_[static_cast<std::size_t>(std::find_if(campaign.records().begin(),campaign.records().end(),[&](const auto& r){return r.level_id()==campaign.first_level_id();})-campaign.records().begin())].c_str())) {
                    for(std::size_t i=0;i<campaign.records().size();++i)if(ImGui::Selectable(campaign_mission_labels_[i].c_str(),campaign.records()[i].level_id()==campaign.first_level_id())){campaign.set_first_level_id(campaign.records()[i].level_id());campaign_dirty_=true;}
                    ImGui::EndCombo();
                }
                const auto& source=campaign_document_->source(id);
                const auto wanted=mission_lev_path(record);
                const bool mission_available=!source.map_project.empty()||std::any_of(
                    catalog_.map_groups().begin(),catalog_.map_groups().end(),[&](const auto& group){
                        return thandor::normalize_asset_path(group.level_path)==wanted;
                    });
                if(mission_available){
                    if(ImGui::Button("Open mission in editor"))attempt([&]{open_campaign_mission(selected);});
                }else ImGui::TextWrapped("Mission map is not installed: %s",wanted.c_str());
                ImGui::SameLine();if(ImGui::Button("Remove mission"))attempt([&]{campaign_document_->remove_mission(id);if(campaign_editing_mission_==id)campaign_editing_mission_.reset();campaign_dirty_=true;refresh_campaign_labels();});
                // Removing invalidates native records; skip the remainder of this frame if it succeeded.
                if(selected<campaign.records().size()&&campaign.records()[selected].level_id()==id) {
                    ImGui::InputText("Mission project",campaign_map_path_,sizeof(campaign_map_path_));
                    if(ImGui::Button("Save open mission project"))attempt([&]{save_campaign_mission(campaign_map_path_);});
                    ImGui::SameLine();if(ImGui::Button("Add project as new mission"))attempt([&]{
                        campaign_document_->add_map_project(catalog_,campaign_map_path_);campaign_selected_mission_=static_cast<int>(campaign.records().size())-1;
                        campaign_dirty_=true;refresh_campaign_labels();campaign_message_="Mission added. Choose its incoming outcome links below.";
                    });
                    ImGui::TextWrapped("Add a saved .thmap project above, or open this mission and save its edited copy. Mission conditions are edited in the Map inspector.");
                    // Adding can replace the record vector. Resolve again rather than retaining references.
                    auto& chosen=campaign.record(static_cast<std::size_t>(campaign_selected_mission_));const auto chosen_id=chosen.level_id();
                    int edited_id=chosen_id;
                    if(ImGui::InputInt("Map ID",&edited_id,1,1,ImGuiInputTextFlags_EnterReturnsTrue))attempt([&]{campaign_document_->set_mission_id(chosen_id,edited_id);if(campaign_editing_mission_==chosen_id)campaign_editing_mission_=edited_id;campaign_dirty_=true;refresh_campaign_labels();});
                    ImGui::SeparatorText("Outcomes");
                    for(unsigned outcome=0;outcome<8;++outcome) {
                        ImGui::PushID(static_cast<int>(outcome));
                        auto& live=campaign.record(static_cast<std::size_t>(campaign_selected_mission_));
                        const auto live_id=live.level_id();
                        const auto heading=campaign_document_->outcome_label(live_id,outcome)+"  >  "+campaign_document_->continuation_label(live_id,outcome)+"###Outcome";
                        if(ImGui::TreeNode(heading.c_str())) {
                            campaign_dirty_|=edit_string("Name",campaign_document_->source(live_id).outcome_names[outcome],128);
                            if(ImGui::BeginCombo("Next",campaign_document_->continuation_label(live_id,outcome).c_str())) {
                                int32_t terminal=0;while(campaign.find_level(terminal)&&terminal<std::numeric_limits<int32_t>::max())++terminal;
                                if(ImGui::Selectable("Finish campaign")){live.set_successor(outcome,terminal);campaign_dirty_=true;}
                                if(ImGui::Selectable("Replay this map")){live.set_successor(outcome,live_id);campaign_dirty_=true;}
                                if(ImGui::Selectable("Keep current campaign")){live.set_successor(outcome,-1);campaign_dirty_=true;}
                                for(std::size_t i=0;i<campaign.records().size();++i)if(ImGui::Selectable(campaign_mission_labels_[i].c_str())){live.set_successor(outcome,campaign.records()[i].level_id());campaign_dirty_=true;}
                                ImGui::EndCombo();
                            }
                            bool carry=live.carry_over(outcome),retain=live.retain_previous_carry_over(outcome);
                            if(ImGui::Checkbox("Carry armies and technology through exit zones",&carry)){live.set_carry_over(outcome,carry);campaign_dirty_=true;}
                            if(ImGui::Checkbox("Retain previous carry-over records",&retain)){live.set_retain_previous_carry_over(outcome,retain);campaign_dirty_=true;}
                            int movie=live.end_movie(outcome,false),alternate=live.end_movie(outcome,true);
                            if(ImGui::InputInt("Ending movie",&movie)){live.set_end_movie(outcome,false,movie);campaign_dirty_=true;}
                            if(ImGui::InputInt("Alternate ending movie",&alternate)){live.set_end_movie(outcome,true,alternate);campaign_dirty_=true;}
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    if(ImGui::CollapsingHeader("Carry-over exit zones")) {
                        ImGui::Checkbox("Show exit zones on the map",&campaign_show_exit_zones_);
                        ImGui::SliderInt("Faction",&campaign_zone_faction_,0,7);
                        auto& live=campaign.record(static_cast<std::size_t>(campaign_selected_mission_));
                        auto zone=thandor::campaign_exit_zone_render(live.exit_zone(static_cast<std::size_t>(campaign_zone_faction_)));bool changed=false;
                        changed|=ImGui::InputFloat("Exit center X",&zone[0]);changed|=ImGui::InputFloat("Exit center Y",&zone[1]);
                        changed|=ImGui::InputFloat("Exit radius",&zone[2]);changed|=ImGui::InputFloat("Destination X",&zone[3]);changed|=ImGui::InputFloat("Destination Y",&zone[4]);
                        if(changed)attempt([&]{live.set_exit_zone(static_cast<std::size_t>(campaign_zone_faction_),thandor::campaign_exit_zone_from_render(zone));campaign_dirty_=true;});
                        if(document_&&campaign_editing_mission_==live.level_id()&&selected_placement_&&*selected_placement_<document_->level().placements().size()) {
                            if(ImGui::Button("Use selected object's position as exit center"))attempt([&]{const auto& p=document_->level().placements()[*selected_placement_];zone[0]=p.render_x();zone[1]=p.render_y();live.set_exit_zone(static_cast<std::size_t>(campaign_zone_faction_),thandor::campaign_exit_zone_from_render(zone));campaign_dirty_=true;});
                        }
                        if(document_&&selected_placement_&&*selected_placement_<document_->level().placements().size()) {
                            if(ImGui::Button("Use selected object's position as destination"))attempt([&]{const auto& p=document_->level().placements()[*selected_placement_];zone[3]=p.render_x();zone[4]=p.render_y();live.set_exit_zone(static_cast<std::size_t>(campaign_zone_faction_),thandor::campaign_exit_zone_from_render(zone));campaign_dirty_=true;});
                        }
                        ImGui::TextWrapped("Coordinates match the map viewport. Radius 0 disables collection for that faction. Destination coordinates are on the next map; position = old position + destination - exit center.");
                    }
                }
            }
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Export")) {
            if(!campaign_document_)ImGui::TextWrapped("Create or open a campaign first.");
            else {
                ImGui::TextWrapped("Export creates a new folder with the mission package, a merged text patch, and installation notes. Choose a free package slot for this game installation.");
                ImGui::InputInt("Package slot (00-99)",&campaign_package_slot_);campaign_package_slot_=std::clamp(campaign_package_slot_,0,99);
                ImGui::InputText("New export folder",campaign_export_path_,sizeof(campaign_export_path_));
                if(ImGui::Button("Validate campaign"))attempt([&]{campaign_validation_=campaign_document_->validate(catalog_);campaign_message_=campaign_validation_.ok()?"Campaign validation passed. Review any warnings below.":"Campaign needs corrections before export.";});
                ImGui::SameLine();if(ImGui::Button("Export campaign package"))attempt([&]{
                    if(campaign_editing_mission_&&document_&&document_->dirty())throw std::runtime_error("Save the open mission project before exporting.");
                    if(campaign_export_path_[0]=='\0')throw std::runtime_error("Enter a new export folder outside the game installation.");
                    const auto result=campaign_document_->export_package(catalog_,campaign_export_path_,static_cast<unsigned>(campaign_package_slot_));
                    campaign_validation_={};campaign_validation_.warnings=result.warnings;campaign_message_="Exported "+std::to_string(result.mission_count)+" missions to "+result.level_package.parent_path().string()+". Read INSTALL.txt before copying the packages.";
                });
                for(const auto& error:campaign_validation_.errors)ImGui::TextWrapped("Error: %s",error.c_str());
                for(const auto& warning:campaign_validation_.warnings)ImGui::TextWrapped("Warning: %s",warning.c_str());
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if(!campaign_message_.empty()){ImGui::Separator();ImGui::TextWrapped("%s",campaign_message_.c_str());}
    ImGui::End();
}
} // namespace editor
