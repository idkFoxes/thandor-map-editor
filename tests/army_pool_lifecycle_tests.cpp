#include "thandor/simulation_lab_army_pool.hpp"
#include "thandor/pioneer_site_reservation.hpp"
#include <stdexcept>
#include <iostream>
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try{
 SimulationArmyPool pool;pool.enabled=true;
 for(unsigned i=0;i<8;++i)check(pool.create(i,2)==i,"initial slot order");
 pool.slots[3].command_flags=9;pool.slots[3].command_generation=32;pool.slots[3].target_slot=1;
 pool.slots[3].retained_waypoint_count=1;pool.slots[3].retained_waypoints[0]={123,-456};pool.slots[3].retained_waypoints[1]={789,-10};
 pool.release(3);check(pool.slots[3].faction==2&&pool.slots[3].command_flags==9,"release erased retained record");
 // Native fails model creation after writing the new ARM/faction, before
 // initializing command fields; that partial write is observable by old shots.
 check(pool.begin_create(333,1)==3,"failed constructor selected another slot");
 check(!pool.slots[3].occupied&&pool.slots[3].faction==1&&pool.slots[3].command_flags==9,
       "failed constructor discarded its partial slot writes");
 pool.finish_create(3,{2,-3,0});check(pool.slots[3].command_flags==0&&!pool.slots[3].target_slot,"constructor did not initialize commands");
 check(pool.slots[3].retained_waypoint_count==1&&pool.slots[3].retained_waypoints[0]==std::array<int32_t,2>{8192,-12288}&&
       pool.slots[3].retained_waypoints[1]==std::array<int32_t,2>{789,-10},
       "constructor did not preserve waypoint count/tail while resetting the coordinate aliases");
 // Neutral wreck creation happens inside hierarchy destruction, before the
 // army's slot becomes vacant. It must not take its still-occupied parent slot.
 const auto wreck=pool.create(412,0);check(wreck==8,"replacement reused its still-owned parent slot");
 pool.release(*wreck);
 pool.release(3);pool.release(5);pool.release(7);
 std::vector<size_t> claimed,released;unsigned attempts=0;
 const bool rejected=pioneer_reserve_separated_site_chain<size_t>({0,0},1,
  [&](MovementPoint)->std::optional<MovementPoint>{return MovementPoint{20,20};},
  [&](MovementPoint)->std::optional<size_t>{auto s=pool.create(333,4);if(s){claimed.push_back(*s);++attempts;}return s;},
  [&](size_t slot){released.push_back(slot);pool.release(slot);});
 check(rejected&&attempts==4&&claimed==std::vector<size_t>{3,5,7,8},"concurrent probes did not occupy first-free slots");
 check(released==std::vector<size_t>{8,7,5,3},"probe cleanup order");
 for(auto i:claimed)check(!pool.slots[i].occupied&&pool.slots[i].faction==4&&pool.slots[i].army==333,
   "cleanup restored previous slot identity");
 check(pool.create(16,6)==3&&pool.slots[3].faction==6,"live reuse lost the latest owner");
 pool.release(3);check(pool.create(333,1)==3&&pool.slots[3].faction==1,"repeated probe chain missed reused slot");
 SimulationArmyPool bounded;bounded.enabled=true;
 for(size_t i=0;i<SimulationArmyPool::capacity;++i)check(bounded.create(1,1)==i,"capacity slot order");
 check(!bounded.create(2,2)&&bounded.slots.size()==SimulationArmyPool::capacity,"exhausted native army pool grew");
 // Native construction is independent of the player's lifecycle. Inactive
 // scripted factions and a defeated player's finishing factory still own slots.
 SimulationLabRun late;late.army_pool.enabled=true;late.entities.resize(1);
 auto& produced=late.entities[0];produced.faction_index=1;produced.army_asset_id=115;
 produced.actor.position={2,-3,0};
 for(uint32_t state:{0U,1U,2U}){
  late.faction_lifecycle_states[1]=state;
  simulation_lab_claim_army_slot(late,produced,0);
  check(produced.native_army_pool_slot==0&&late.army_pool.slots[0].occupied,
        "player lifecycle suppressed a completed army's runtime slot");
  simulation_lab_release_army_slot(late,produced);
 }
 std::cout<<"ARMY_POOL_LIFECYCLE PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
