#include "thandor/simulation_lab_site_placement.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace thandor;

namespace {
void check(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}
MovementPoint world(const FieldGrid& field,std::uint32_t x,std::uint32_t y) {
    const auto& cell=field.at(x,y);
    return {cell.world_x_q12,cell.world_y_q12};
}
SimulationLabRun run_for(const FieldGrid& field) {
    SimulationLabRun run;
    run.ai_parameters[28U]=0x2000U;
    run.ai_parameters[0x51U]=0x10000U;
    auto scratch=std::make_shared<SimulationLabRetailScratchImage>();
    scratch->width=field.width()*4U;
    scratch->height=field.height()*4U;
    scratch->state.assign(static_cast<std::size_t>(scratch->width)*scratch->height,0U);
    scratch->field_occupancy_masks.assign(field.cells().size(),std::uint64_t{1}<<8U);
    run.retail_scratch=std::move(scratch);
    return run;
}
SimulationLabSiteOwner support(MovementPoint point,std::int32_t reach=0x2000) {
    return {{point,1U,11U,0U,reach,true},0,{}};
}
}

int main() try {
    auto field=FieldGrid::create(80U,80U,0,0U);
    const auto site=world(field,40U,40U);
    field.at(40U,40U).flags_and_material|=0x800U;
    const SimulationLabSiteModel resource{330U,14U,0,0,0x2000,0,0,{}};
    const SimulationLabSiteModel storage{333U,15U,0,0,0x2000,0,0,{}};

    {
        auto run=run_for(field);
        auto hostile=support({site.x-0x1000,site.y});
        hostile.support.faction=2U;
        const std::vector<SimulationLabSiteOwner> owners{hostile};
        const auto result=simulation_lab_pioneer_site_admission(
            run,field,owners,resource,storage,site,{},1U);
        check(result.disposition==PioneerSiteAdmission::SkipSite &&
              !result.mode3_accepted && result.mode0_queried && !result.mode0_accepted,
              "failed mode3/mode0 pair did not skip only this site");
    }
    {
        auto run=run_for(field);
        std::vector<SimulationLabSiteOwner> owners{support(site)};
        const auto result=simulation_lab_pioneer_site_admission(
            run,field,owners,resource,storage,site,{},1U);
        check(result.disposition==PioneerSiteAdmission::StopSelector &&
              result.mode3_accepted && result.clearance_q12==0U &&
              !result.mode0_queried && result.bucket_count==0U,
              "zero-clearance mode3 result did not stop the selector");
    }
    {
        auto run=run_for(field);
        // Farther than unpadded support, but within mode3's KI[0x51] padding.
        const MovementPoint far_support{site.x-0xC000,site.y};
        std::vector<SimulationLabSiteOwner> owners{support(far_support)};
        const auto result=simulation_lab_pioneer_site_admission(
            run,field,owners,resource,storage,site,{},1U);
        check(result.disposition==PioneerSiteAdmission::ScoreSite &&
              result.mode3_accepted && result.mode0_queried && !result.mode0_accepted &&
              result.bucket_count>=5U && !result.reserve_chain_called,
              "large admitted bucket did not bypass ARM333 reservation");
    }
    {
        auto run=run_for(field);
        const MovementPoint near_support{site.x-0x3000,site.y};
        std::vector<SimulationLabSiteOwner> owners{support(near_support)};
        const Vec3f anchor{q12_to_float(site.x-0x1000),q12_to_float(site.y),0.F};
        const std::array<Vec3f,1> anchors{anchor};
        const auto result=simulation_lab_pioneer_site_admission(
            run,field,owners,resource,storage,site,anchors,1U);
        check(result.disposition==PioneerSiteAdmission::StopSelector &&
              result.bucket_count>0U && result.bucket_count<5U &&
              result.reserve_chain_called && !result.reserve_chain_rejected &&
              result.probes_created==1U,
              "near temporary ARM333 chain did not stop the whole selector");
        const auto no_anchor=simulation_lab_pioneer_site_admission(
            run,field,owners,resource,storage,site,{},1U);
        check(no_anchor.disposition==PioneerSiteAdmission::ScoreSite &&
              no_anchor.reserve_chain_called && no_anchor.reserve_chain_rejected &&
              no_anchor.probes_created==0U,
              "failed ARM333 chain did not leave the small site scoreable");
    }

    std::cout<<"V746_PIONEER_LIVE_ADMISSION PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"V746_PIONEER_LIVE_ADMISSION FAIL: "<<error.what()<<'\n';
    return 1;
}
