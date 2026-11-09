// Fresh stock ARM13 command at a stalled Eisige Höhen position. CPU only;
// this is a controlled fixture, not replay of the full uploaded simulation.
#define main stock_replay_main
#include "simlab_stock_replay.cpp"
#undef main
#include "thandor/simulation_command_markers.hpp"

int main(int argc,char** argv)try{
    if(argc!=2)throw std::runtime_error("Usage: movement_terrain_obstruction_probe GAME_ROOT");
    const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
    const auto group=std::find_if(catalog.map_groups().begin(),catalog.map_groups().end(),
        [](const auto& g){return g.level_path.ends_with("/eisigehöhen.lev");});
    if(group==catalog.map_groups().end())throw std::runtime_error("Eisige Höhen map unavailable");
    const auto document=MapDocument::open(catalog,*group);
    const auto* preset=catalog.army_preset(13);
    if(!preset)throw std::runtime_error("ARM13 unavailable");
    auto seed=seed_for(*preset,catalog,{});seed.faction_index=1;
    seed.actor.position={50.176F,-3.08708F,0};seed.actor.body_yaw_turn16=53072;
    const Vec3f goal{47.8313F,-7.80859F,0};
    SimulationLabScenarioRuntimeSeed scenario;scenario.enabled=true;
    scenario.active_faction_count=1;scenario.ai_planning_enabled=false;
    scenario.static_navigation_blockers=simulation_lab_prepare_static_navigation_blockers(catalog,document.level());
    SimulationLabConfig config;config.run_count=1;config.duration_ticks=1024;
    std::array<uint32_t,AiKnowledgeData::kParameterCount> parameters{};
    SimulationLabExperiment lab;lab.reset(config,parameters,std::span(&seed,1),{},{},{},{},{},scenario);
    auto& run=lab.runs()[0];run.entities[0].actor.move_target=goal;
    for(uint32_t tick=0;tick<1024;++tick){
        lab.step(document.field(),1);const auto& e=run.entities[0];
        if(tick%64==0||tick==1023)
            std::cout<<"M tick="<<run.tick<<" x="<<e.actor.position.x<<" y="<<e.actor.position.y
                <<" yaw="<<e.actor.body_yaw_turn16<<" target="<<bool(e.actor.move_target)
                <<" waypoint="<<bool(e.actor.move_waypoint)<<" blocks="<<run.metrics.navigation_actual_step_blocks<<'\n';
    }
    const auto& actor=run.entities[0].actor;
    const bool arrived=!actor.move_target&&!actor.move_waypoint&&
        std::abs(actor.position.x-goal.x)<=1.F&&std::abs(actor.position.y-goal.y)<=1.F;
    std::cout<<"STOCK_TERRAIN "<<(arrived?"PASS":"FAIL")<<" arm=13 ticks=1024 arrived="<<arrived
        <<" markers="<<simulation_lab_command_markers(run).size()<<" gpu_verified=0\n";
    return arrived?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
