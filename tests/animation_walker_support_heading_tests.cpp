#include "thandor/simulation.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main()try{
 SimulationActorState actor;actor.body_yaw_turn16=0x4000;
 check(simulation_articulated_support_heading(actor)==0x4000,"uninitialized contact changed heading");
 actor.articulated_contacts_initialized=true;actor.articulated_contact_heading_turn16={0,0};
 actor.articulated_contact_target_heading_turn16={0x2000,0};
 actor.articulated_contact_transition_active=true;actor.articulated_was_moving=true;
 for(uint32_t p:{0U,1024U,2048U,3072U,4096U}){
  actor.articulated_contact_progress_q12=p;
  check(simulation_articulated_support_heading(actor)==p,"body detached from interpolated contacts");
 }
 actor.articulated_contact_heading_turn16={0xF000,0xF000};
 actor.articulated_contact_target_heading_turn16={0x1000,0xF000};actor.articulated_contact_progress_q12=2048;
 check(simulation_articulated_support_heading(actor)==0xF800,"turn crossing zero took long path");
 actor.articulated_swing_side=1;actor.articulated_contact_heading_turn16={0x1000,0xF000};
 actor.articulated_contact_target_heading_turn16={0x1000,0x1000};
 check(simulation_articulated_support_heading(actor)==0x0800,"closing foot lost second half turn");
 SimulationActorState pivot;
 simulation_update_articulated_contact_transition(pivot,{0,0,0},0,{0,0,0},0x800,1400,132);
 check(pivot.articulated_contact_progress_q12==264&&pivot.articulated_contact_event_serial==0,
       "pivot heading change completed an entire footstep in one tick");
 std::cout<<"WALKER_SUPPORT_HEADING PASS\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
