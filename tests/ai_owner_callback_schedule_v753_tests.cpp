#include "thandor/simulation_lab_owner_schedule.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

void captured_chain_mutation_order(){
    const std::array captured{4,3,2,1}; // newest-first head snapshot
    std::array<bool,6> linked{};
    for(int id=1;id<=4;++id)linked[static_cast<std::size_t>(id)]=true;
    std::vector<int> live_order(captured.begin(),captured.end());
    std::vector<int> visited;
    thandor::simulation_lab_visit_captured_owner_chain<int>(captured,
        [&](int id){return linked[static_cast<std::size_t>(id)];},
        [&](int id){
            visited.push_back(id);
            if(id==4){
                // Insertion at the list head is outside the captured chain.
                linked[5]=true;
                live_order.insert(live_order.begin(),5);
                // A future owner is unlinked before its callback.
                linked[2]=false;
                std::erase(live_order,2);
            }
            if(id==3){
                // The current owner already owns this callback and completes it.
                linked[3]=false;
                std::erase(live_order,3);
            }
        });
    require((visited==std::vector<int>{4,3,1}),
        "captured owner chain did not preserve insert/current-remove/future-remove semantics");
    require((live_order==std::vector<int>{5,4,1}),
        "live owner chain mutation image is inconsistent");
}

void phase7_fresh_head_between_passes(){
    const std::array occupancy_capture{4,3,2,1};
    std::array<bool,6> linked{};
    for(int id=1;id<=4;++id)linked[static_cast<std::size_t>(id)]=true;
    std::vector<int> live_order(occupancy_capture.begin(),occupancy_capture.end());
    std::vector<int> occupancy_visited;
    thandor::simulation_lab_visit_captured_owner_chain<int>(occupancy_capture,
        [&](int id){return linked[static_cast<std::size_t>(id)];},
        [&](int id){
            occupancy_visited.push_back(id);
            if(id==4){
                linked[5]=true;
                live_order.insert(live_order.begin(),5);
                linked[2]=false;
                std::erase(live_order,2);
            }
        });
    std::vector<int> refresh_visited;
    const auto refresh_capture=live_order; // phase 7 reloads the current head
    thandor::simulation_lab_visit_captured_owner_chain<int>(refresh_capture,
        [&](int id){return linked[static_cast<std::size_t>(id)];},
        [&](int id){refresh_visited.push_back(id);});
    require((occupancy_visited==std::vector<int>{4,3,1}),
        "phase-7 occupancy pass visited a same-job inserted or removed owner");
    require((refresh_visited==std::vector<int>{5,4,3,1}),
        "phase-7 terrain refresh did not start from the fresh current head");
}
}

int main()try{
    captured_chain_mutation_order();
    phase7_fresh_head_between_passes();
    std::cout<<"V753_OWNER_CALLBACK_SCHEDULE PASS captured=1 fresh_head=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
