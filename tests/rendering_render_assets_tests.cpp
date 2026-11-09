#include "thandor/simulation_lab_render_assets.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool b,const char* s){if(!b)throw std::runtime_error(s);}
int main()try{
 SimulationLabRun run;SimulationLabEntity unit;unit.army_asset_id=2;unit.faction_index=1;unit.synthetic=true;
 run.entities.push_back(unit);const auto initial=simulation_lab_resident_render_assets(run);
 for(size_t i=0;i<317;++i){unit.source_placement_index=i;unit.runtime_removed=i<243;run.entities.push_back(unit);}
 check(simulation_lab_resident_render_assets(run)==initial,"duplicate production/tombstones invalidated resident texture assets");
 auto other=unit;other.army_asset_id=60;run.entities.push_back(other);
 check(simulation_lab_resident_render_assets(run).size()==2,"new stock ARM failed to invalidate preload identity");
 other.faction_index=2;run.entities.push_back(other);
 check(simulation_lab_resident_render_assets(run).size()==3,"new faction texture family failed to invalidate preload identity");
 const auto expected=simulation_lab_resident_render_assets(run);std::reverse(run.entities.begin(),run.entities.end());
 check(simulation_lab_resident_render_assets(run)==expected,"entity iteration order changed texture residency");
 other.synthetic=false;other.army_asset_id=999;other.runtime_removed=true;run.entities.push_back(other);
 check(simulation_lab_resident_render_assets(run)==expected,"authored tombstone duplicated the separate placement suppression domain");
 std::cout<<"V620_RENDER_ASSETS PASS: 317 repeated instances add zero texture invalidations; new ARM/faction retained\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
