#include "thandor/simulation_lab_shot_terrain.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace thandor;
static void require(bool b,const char* m) {if(!b)throw std::runtime_error(m);}
int main() try {
    const auto field=FieldGrid::create(64,64,0,0);
    std::vector<uint64_t> occupancy(field.cells().size(),0);
    constexpr std::array<uint32_t,3> xx{{12,26,40}};
    std::array<Vec3f,3> pts{};
    for(size_t i=0;i<pts.size();++i){
        const auto& cell=field.at(xx[i],20);
        pts[i]={q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),0};
        occupancy[size_t(20)*field.width()+xx[i]]=0x88ULL<<(8U*(i+1U));
    }
    std::array<uint32_t,3> masks{};
    for(size_t i=0;i<3;++i){
        masks[i]=simulation_lab_shot_neighborhood_mask(
            field,occupancy,pts[i],false,pts[0],false);
        require(masks[i]!=0,"single point not classified");
    }
    const auto beam=simulation_lab_shot_neighborhood_mask(
        field,occupancy,pts[0],true,pts[2],true);
    require(beam==(masks[0]|masks[1]|masks[2]),"beam must OR start, midpoint, endpoint");
    const auto no_beam=simulation_lab_shot_neighborhood_mask(
        field,occupancy,pts[0],true,pts[2],false);
    require(no_beam==masks[0],"zero beam length must only sample origin");
    const auto moving=simulation_lab_shot_neighborhood_mask(
        field,occupancy,pts[0],false,pts[2],true);
    require(moving==masks[0],"moving projectile must not sample unused beam points");
    std::cout<<"V736_SHOT_TERRAIN PASS start/mid/end and non-beam branches\n";
} catch(const std::exception& e){std::cerr<<"V736_SHOT_TERRAIN FAIL "<<e.what()<<'\n';return 1;}
