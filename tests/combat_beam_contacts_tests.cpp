#include "thandor/simulation_lab_shot_rays.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
using namespace thandor;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
SimulationLabStaticShotHull box(Vec3f p,float r=.2F,uint32_t cls=1){SimulationLabStaticShotHull h;h.impact_class=cls;
 for(uint32_t i=0;i<8;++i)h.corners[i]={p.x+((i&1)?r:-r),p.y+((i&2)?r:-r),p.z+((i&4)?r:-r)};return h;}
int main()try{
 auto field=FieldGrid::create(60,60,0,0);const auto& c=field.at(25,25);Vec3f start{q12_to_float(c.world_x_q12),q12_to_float(c.world_y_q12),3};
 ShotDefinitionRecord shot;shot.trajectory_mode=0;shot.launch_speed_q12=4096;shot.projectile_lifetime_ticks=8;shot.target_impact_effect_ids[1]=44;
 SimulationLabRuntimeCatalog catalog;catalog.effect_lifetimes[44]=7;
 SimulationProjectileState beam;beam.launch_position=start;beam.target_position={start.x+2,start.y,3};beam.launch_velocity={1,0,0};beam.lifetime_ticks=8;beam.live_target_placement_index=1;
 std::vector<SimulationLabLiveShotHull> hulls{{box(start),0,0},{box({start.x+2,start.y,3}),1,2}};
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,{})&&beam.impact_target_placement_index==1,"beam failed current target or excluded its owner incorrectly");
 check(beam.impact_target_model_component_index==2&&beam.impact_visual_lifetime_ticks==7,"beam lost hit component/effect lifetime");
 const auto near=beam.target_position;
 hulls[1].hull=box({start.x+2,start.y+1,3});
 check(!simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,{})&&!beam.impact_target_placement_index,"beam continued hitting a target that moved out");
 check(std::abs(beam.target_position.x-start.x-8)<1.e-5F&&std::abs(beam.target_position.y-start.y)<1.e-5F,"beam retargeted or failed to recover full native length");
 hulls.push_back({box({start.x+4,start.y,3}),2,0});
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,{})&&beam.impact_target_placement_index==2,"beam did not hit a new unit crossing its frozen ray");
 const std::array obstacle{box({start.x+1,start.y,3})};
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,obstacle)&&!beam.impact_target_placement_index&&beam.target_position.x<near.x,"static model failed to intercept beam");
 auto zero=obstacle;zero[0].impact_class=2;
 check(!simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,zero)&&
       beam.impact_kind==SimulationImpactKind::None&&!beam.impact_target_placement_index&&
       std::abs(beam.target_position.x-start.x-8)<1.e-5F,
       "null-effect nearest model stopped beam or searched behind that model");
 shot.target_impact_effect_ids[1]=0;
 check(!simulation_lab_refresh_beam_contact(beam,shot,field,catalog,hulls,{})&&
       beam.impact_kind==SimulationImpactKind::None&&!beam.impact_target_placement_index,
       "null target-effect slot was accepted as a beam hit");
 shot.target_impact_effect_ids[1]=44;
 // Exact terrain rays use the real staggered FLD. Downward beam reaches ground first.
 beam.launch_pitch_turn16=0xE000;shot.terrain_impact_effect_ids[0]=45;
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,{},{} )&&beam.impact_kind==SimulationImpactKind::TerrainSurface,"beam failed to clip to terrain");
 const std::array null_foreground{box({start.x+.5F,start.y,2.5F},.2F,2)};
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,{},null_foreground)&&
       beam.impact_kind==SimulationImpactKind::TerrainSurface,
       "ineligible foreground model concealed eligible ground contact");
 for(auto& cell:field.cells())cell.water_delta_q12=4096;shot.primary_effect_id=46;
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,{},{} )&&beam.impact_kind==SimulationImpactKind::SecondarySurface,"nearer secondary surface did not clip beam");
 shot.primary_effect_id=0;
 check(simulation_lab_refresh_beam_contact(beam,shot,field,catalog,{},{} )&&beam.impact_kind==SimulationImpactKind::TerrainSurface,"null secondary effect stopped beam");
 std::cout<<"V621_BEAM_CONTACTS PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
