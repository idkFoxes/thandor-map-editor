#include "thandor/purchase_affordability_v600.hpp"
#include <cassert>
using namespace thandor;
int main() {
    constexpr auto none=AiPurchaseBlockReasonV600::None;
    constexpr auto balance=AiPurchaseBlockReasonV600::TemporaryBalance;
    constexpr auto ceiling=AiPurchaseBlockReasonV600::StorageCeiling;
    constexpr auto unknown=AiPurchaseBlockReasonV600::UnknownStorage;
    assert(ai_purchase_block_reason_v600(24000,20000,true,20000)==ceiling);
    assert(ai_purchase_block_reason_v600(24000,9000,true,30000)==balance);
    assert(ai_purchase_block_reason_v600(24000,24000,true,20000)==none);
    assert(ai_purchase_block_reason_v600(24000,9000,false,0)==unknown);
    AiPurchaseBlockDiagnosticV600 d;
    d.record(100,303,24000,10000,true,20000,true);
    assert(d.calls==1 && d.ceiling_blocks==1 && d.last_cached==1);
    d.record(200,331,5000,2500,true,20000,false);
    assert(d.calls==2 && d.balance_blocks==1 && d.last_candidate_id==331);
    d.record(300,331,5000,5000,true,20000,false);
    assert(d.calls==2 && d.last_tick==200);
}
