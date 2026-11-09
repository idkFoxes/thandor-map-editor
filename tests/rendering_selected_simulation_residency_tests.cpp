#include "simulation_lab_selected_residency.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <memory>
#include <string>
#include <vector>

namespace {
void require(bool condition){if(!condition)throw std::runtime_error("selected Lab texture residency regression");}
struct Entity {bool synthetic{},runtime_removed{},root_model_terminal{};std::uint32_t faction_index{};std::uint32_t army_asset_id{};std::shared_ptr<const std::vector<std::string>> runtime_model_hierarchy{};};
struct Run {std::vector<Entity> entities;std::array<std::array<std::uint32_t,8>,8> faction_technology_masks{};};
}
int main(){
    std::array<Run,3> runs{};
    runs[0].entities={{true,false,false,1,101},{true,false,false,2,102}};
    runs[1].entities={{true,false,false,3,201},{true,false,false,3,201},
                     {false,false,false,1,202},
                     {true,true,false,1,203},{true,false,true,1,204},
                     {true,false,false,0,205},{true,false,false,8,206}};
    runs[2].entities={{true,false,false,4,301},{true,false,false,4,301}};
    runs[1].faction_technology_masks[3][2]=42;
    std::vector<std::uint32_t> observed;
    const std::span<const Run> snapshots=runs;
    require(editor::for_each_selected_lab_synthetic_asset(snapshots,1,
        [&](const Run& run,const Entity& entity,std::size_t faction){
            if(faction==3)require(run.faction_technology_masks[faction][2]==42);
            observed.push_back(entity.army_asset_id);
        })==2);
    require(observed==std::vector<std::uint32_t>({201,204}));
    observed.clear();
    require(editor::for_each_selected_lab_synthetic_asset(snapshots,2,
        [&](const Run&,const Entity& entity,std::size_t){observed.push_back(entity.army_asset_id);})==1);
    require(observed==std::vector<std::uint32_t>{301});
    // Two actors share the same stock ARM and graphics family, but their live
    // runtime model hierarchies reference different SPR texture subresources.
    // Both overrides must be resident in the selected run, and neither from an
    // inactive run may reach the renderer.
    runs[1].entities[0].runtime_model_hierarchy=
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"family3/13"});
    runs[1].entities[1].runtime_model_hierarchy=
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"family3/15"});
    runs[1].entities[4].runtime_model_hierarchy=
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"terminal/family4/3"});
    runs[1].entities[2].runtime_model_hierarchy=
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"authored/17"});
    runs[2].entities[0].runtime_model_hierarchy=
        std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"inactive/99"});
    std::vector<std::string> selected_sprites;
    require(editor::for_each_selected_lab_runtime_hierarchy(snapshots,1,
        [&](const Run&,const Entity& entity,std::size_t faction){
            require(faction==3||faction==1);
            for(const auto& sprite:*entity.runtime_model_hierarchy)selected_sprites.push_back(sprite);
        })==4);
    require(selected_sprites==std::vector<std::string>{"family3/13","family3/15","authored/17","terminal/family4/3"});
    selected_sprites.clear();
    require(editor::for_each_selected_lab_runtime_hierarchy(snapshots,2,
        [&](const Run&,const Entity& entity,std::size_t){
            for(const auto& sprite:*entity.runtime_model_hierarchy)selected_sprites.push_back(sprite);
        })==1);
    require(selected_sprites==std::vector<std::string>{"inactive/99"});
    // Last instance transitions to terminal while the draw pass can still
    // encounter its model; the snapshot's terminal sprite remains resident.
    runs[1].entities[0].runtime_removed=true;
    runs[1].entities[1].runtime_removed=true;
    observed.clear();
    require(editor::for_each_selected_lab_synthetic_asset(snapshots,1,
        [&](const Run&,const Entity& entity,std::size_t){observed.push_back(entity.army_asset_id);})==1);
    require(observed==std::vector<std::uint32_t>{204});
    selected_sprites.clear();
    require(editor::for_each_selected_lab_runtime_hierarchy(snapshots,1,
        [&](const Run&,const Entity& entity,std::size_t){
            for(const auto& sprite:*entity.runtime_model_hierarchy)selected_sprites.push_back(sprite);
        })==2);
    require(selected_sprites==std::vector<std::string>{"authored/17","terminal/family4/3"});
    observed.clear();
    require(editor::for_each_selected_lab_synthetic_asset(snapshots,3,
        [&](const Run&,const Entity&,std::size_t){observed.push_back(999);})==0);
    require(editor::for_each_selected_lab_runtime_hierarchy(snapshots,3,
        [&](const Run&,const Entity&,std::size_t){observed.push_back(999);})==0);
    require(observed.empty());
    return 0;
}
