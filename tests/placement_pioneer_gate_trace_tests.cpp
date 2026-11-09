#include "thandor/pioneer_gate_trace.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void require(bool v,const char* msg){if(!v) throw std::runtime_error(msg);}
int main() try {
    PioneerGateTrace t;
    // Legitimate scoring gate: no special flag on entry, arrived carry, unequal
    // workspaces, admitted resource site.
    const PioneerGateInput score{0U,0U,true,6U,4U,1U,0,0,10000,0};
    auto r = pioneer_movement_gate(score);
    require(r.action==PioneerGateAction::ScoreSites && r.movement_update_called,
            "score precondition contract changed");
    t.record(74,true,score,r);
    require(t.actions[2]==1 && t.movement_carry_calls==1 && t.scheduled_calls==1,
            "scored gate did not count real carry");
    const PioneerGateInput bootstrap{0U,0U,true,4U,4U,1U,0,0,10000,0};
    r=pioneer_movement_gate(bootstrap); t.record(138,true,bootstrap,r);
    require(r.action==PioneerGateAction::BootstrapForward && t.actions[1]==1,
            "equal workspace bootstrap must take precedence over site scoring");
    const PioneerGateInput no_carry{0U,0U,false,6U,4U,1U,0,0,10000,0};
    r=pioneer_movement_gate(no_carry); t.record(202,true,no_carry,r);
    require(r.action==PioneerGateAction::AwaitFallback && t.actions[4]==1,
            "without movement carry the scoring path cannot execute");
    // Distinguish special-flag suppressed update from an ordinary failed update.
    const PioneerGateInput special{0x304U,0x304U,false,6U,4U,1U,34487,-23915,34487,-23915};
    r=pioneer_movement_gate(special);
    require(r.action==PioneerGateAction::ResetAtModel && !r.movement_update_called &&
            (r.flags&0x100U)!=0U,"isolated port reset contract unexpectedly changed");
    t.record(203,false,special,r);
    require(t.special_flag_calls==1 && t.special_flag_prevented_update==1 &&
            t.reset_preserved_special_flag==1 && t.actions[3]==1 &&
            t.last_entry_flags==0x304U && t.last_gate_flags==r.flags &&
            t.last_workspace00==6 && t.last_workspace04==4 &&
            t.last_workspace08==1 && t.last_action==4 && !t.last_movement_carry,
            "special-flag reset provenance was lost");
    // No candidate list: correct branch is NoCommand, not a selector invocation.
    const PioneerGateInput empty{0U,0U,true,6U,4U,0U,0,0,10000,0};
    r=pioneer_movement_gate(empty);t.record(266,true,empty,r);
    require(r.action==PioneerGateAction::NoCommand && t.actions[0]==1 &&
            t.calls==5 && t.movement_update_calls==4 &&
            t.movement_carry_calls==3 && t.special_flag_calls==1,
            "gate coverage counters or absent-workspace branch wrong");
    std::cout << "V594_PIONEER_GATE_TRACE PASS\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr<<"V594_PIONEER_GATE_TRACE FAIL: "<<e.what()<<'\n';
    return 1;
}
