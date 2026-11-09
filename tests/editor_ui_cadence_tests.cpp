#include "simulation_lab_ui_cadence.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char* message){
    if(!condition){
        std::cerr<<"FAIL: "<<message<<'\n';
        std::exit(1);
    }
}
}

int main(){
    using editor::simulation_lab_ui_refresh_due;
    require(simulation_lab_ui_refresh_due(10.0,0.0,false),"first UI/debug sample must be immediate");
    require(!simulation_lab_ui_refresh_due(10.499,10.0,true),"UI/debug data must not refresh before 500 ms");
    require(simulation_lab_ui_refresh_due(10.5,10.0,true),"UI/debug data must refresh at 500 ms");
    require(simulation_lab_ui_refresh_due(9.0,10.0,true),"timer rollback must force a safe refresh");
    std::cout<<"V609 Simulation Lab UI cadence tests PASS\n";
    return 0;
}
