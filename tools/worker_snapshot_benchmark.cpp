#include "thandor/simulation_lab_worker.hpp"
#include <chrono>
#include <iostream>
#include <thread>
using namespace thandor;
int main(){
 for(unsigned trial=0;trial<3;++trial){
  SimulationLabConfig c;c.run_count=4;c.duration_ticks=128;c.parameter_index=100;
  std::array<uint32_t,AiKnowledgeData::kParameterCount> p{};
  std::vector<SimulationLabSeedEntity> seeds;
  const auto field=FieldGrid::create(59,59,0,0);const auto& cell=field.at(29,29);
  for(unsigned i=0;i<64;++i){SimulationLabSeedEntity s;s.army_asset_id=1000+i;s.faction_index=1;
   s.root_model_maximum_integrity=1000;s.actor.position={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
   s.actor.weapons.resize(8);seeds.push_back(s);
  }
  SimulationLabWorker worker;worker.start(c,p,seeds,field);
  const auto started=std::chrono::steady_clock::now();worker.request_advance(128);
  const auto deadline=started+std::chrono::seconds(10);
  while(std::chrono::steady_clock::now()<deadline){auto snapshot=worker.snapshot();if(snapshot&&snapshot->finished()&&worker.completed_generations()>=128)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  const auto snapshot=worker.snapshot();if(!snapshot||!snapshot->finished())return 1;
  std::cout<<"trial="<<trial<<" ticks="<<snapshot->runs()[0].tick<<" milliseconds="
   <<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" publications=";
#ifdef V610_BASELINE
  std::cout<<worker.completed_generations();
#else
  std::cout<<worker.snapshot_publications();
#endif
  std::cout<<'\n';
 }
}
