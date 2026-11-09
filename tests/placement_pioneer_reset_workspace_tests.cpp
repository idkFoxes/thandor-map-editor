#include "thandor/pioneer_retail_reset.hpp"
#include "thandor/pioneer_gate_trace.hpp"
#include <iostream>
#include <stdexcept>
using namespace thandor;
static void check(bool b, const char* why) { if(!b) throw std::runtime_error(why); }
int main() try {
    constexpr MovementPoint pos{34500,-12000};
    constexpr auto unknown=pioneer_retail_reset_common({0x37fU,3U,std::nullopt,pos});
    static_assert(unknown.movement_flags==(0x37fU&0xFFFFFFC6U));
    static_assert(unknown.special_behavior_retained && !unknown.command_mode_known);
    static_assert(unknown.command_mode==3U && !unknown.command_generation_reissued);
    static_assert(unknown.primary.x==pos.x && unknown.stored.y==pos.y && unknown.fallback.x==pos.x);
    constexpr auto state_zero=pioneer_retail_reset_common({0x100U,3U,0,pos});
    static_assert(state_zero.command_mode_known && state_zero.command_mode==3U);
    constexpr auto state_nonzero=pioneer_retail_reset_common({0x300U,0x13U,5,pos});
    static_assert(state_nonzero.command_mode==0x14U && state_nonzero.command_generation_reissued);
    constexpr auto no_mode=pioneer_retail_reset_common({0x300U,0x10U,5,pos});
    static_assert(no_mode.command_mode==0x10U && !no_mode.command_generation_reissued);
    constexpr auto no_special=pioneer_retail_reset_common({0x27fU,0U,0,pos});
    static_assert(!no_special.special_behavior_retained);

    PioneerGateTrace t;
    const PioneerGateInput same{0x304U,0x304U,false,4U,4U,2U,0,0,0,0};
    t.record(18,false,same,pioneer_movement_gate(same));
    check(t.workspace_equal_calls==1 && t.workspace_unequal_calls==0 &&
          t.reset_with_equal_workspaces==1,"equal/reset provenance");
    const PioneerGateInput unequal_special{0x304U,0x304U,false,5U,4U,2U,0,0,0,0};
    t.record(19,false,unequal_special,pioneer_movement_gate(unequal_special));
    check(t.workspace_unequal_calls==1 && t.special_with_unequal_nonempty_sites==1 &&
          t.unequal_with_update_calls==0,"latched-special structural gate");
    const PioneerGateInput unequal_no_carry{0U,0U,false,5U,4U,2U,0,0,10000,0};
    t.record(20,true,unequal_no_carry,pioneer_movement_gate(unequal_no_carry));
    check(t.workspace_unequal_calls==2 && t.unequal_with_update_calls==1 &&
          t.unequal_carry_calls==0,"movement carry not fabricated");
    const PioneerGateInput unequal_carry{0U,0U,true,5U,4U,2U,0,0,10000,0};
    t.record(21,true,unequal_carry,pioneer_movement_gate(unequal_carry));
    check(t.unequal_carry_calls==1 && t.actions[2]==1 && t.calls==4,
          "scoring branch counter");
    std::cout << "V595_RESET_WORKSPACE_CONTRACT PASS\n";
    return 0;
} catch(const std::exception& e) {
    std::cerr << "V595_RESET_WORKSPACE_CONTRACT FAIL: " << e.what() << '\n';
    return 1;
}
