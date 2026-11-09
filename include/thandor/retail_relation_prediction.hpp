#pragma once
#include "thandor/level.hpp"
#include <array>
#include <bit>
namespace thandor {
// Native relation prediction modifies only satisfied bits, in serialized order.
// Use a copy for diagnostics: the live relation drift/merge dispatcher is separate.
inline bool retail_relation_predict(uint32_t focal,uint32_t mask,
    const std::array<uint8_t,8>& lifecycle,
    std::array<LevelScheduledCondition,64>& conditions,
    const std::array<LevelEndConditionTrigger,16>& triggers) {
 uint32_t actual=0;for(uint32_t f=1;f<8;++f)if(lifecycle[f]==1)actual|=1U<<f;
 if(actual==mask)return true;
 for(auto& c:conditions) {
  c.status_and_kind&=0xFEU;bool holds=false;
  switch(c.kind()) {
   case LevelScheduledConditionKind::NoActiveEntityWithDefinition:
   case LevelScheduledConditionKind::NoActiveEntityWithDefinitionAndClassCommandGroupA:
   case LevelScheduledConditionKind::NoActiveEntityWithDefinitionAndRuntimeId:
   case LevelScheduledConditionKind::NoActiveEntityWithClassIdOutsideClassCommandGroupA:
    holds=(mask&(1U<<(c.operand(0)&31U)))==0;break;
   case LevelScheduledConditionKind::MatchingDefinitionAndRuntimeIdActiveEntityCountAtLeast:
    holds=(mask&(1U<<(c.operand(0)&31U)))!=0;break;
   case LevelScheduledConditionKind::FactionInactiveOrRelationAtLeast8:
   case LevelScheduledConditionKind::FactionTerrainOccupancyPercentAtLeast:
   case LevelScheduledConditionKind::XeniteStorageLimitAtMost0FA0:holds=true;break;
   case LevelScheduledConditionKind::CountdownElapsed:holds=c.operand(1)==0;break;
   case LevelScheduledConditionKind::BooleanPostfixExpression:{
    uint32_t stack=0;
    for(const auto token:c.payload_bytes) {
     if(token==0xFC)break;
     const auto pop=uint32_t(std::bit_cast<int32_t>(stack)>>1);
     if(token==0xFD)stack^=1U;
     else if(token==0xFE)stack=pop&(stack|~1U);
     else if(token==0xFF)stack=pop|(stack&1U);
     else if(token<conditions.size())stack=uint32_t(conditions[token].satisfied())+stack*2U;
    }
    holds=(stack&1U)!=0;break;
   }
   default:break;
  }
  if(holds)c.status_and_kind|=1U;
 }
 for(const auto& t:triggers) {
  if(t.state_flags!=1 || t.condition_index>=conditions.size() ||
     !conditions[t.condition_index].satisfied() || t.faction_runtime_index>=8 ||
     lifecycle[t.faction_runtime_index]!=1)continue;
  auto variant=t.movie_variant_selector;
  if(focal!=t.faction_runtime_index && (mask&(1U<<t.faction_runtime_index))==0)variant^=1;
  return variant==0;
 }
 return true;
}
}
namespace thandor {
inline uint32_t retail_relation_bloc(uint32_t faction,const std::array<uint8_t,8>& life,
    const std::array<uint32_t,8>& rows) {
 uint32_t mask=0;
 for(uint32_t other=1;other<8;++other)if(life[other]==1 &&
   (other==faction || ((rows[other]>>(4U*faction))&15U)>=4))mask|=1U<<other;
 return mask;
}
inline bool retail_relation_pair_blocked(uint32_t other,uint32_t faction,uint32_t flags,
    const std::array<uint8_t,8>& life,const std::array<uint32_t,8>& rows,
    std::array<LevelScheduledCondition,64>& conditions,
    const std::array<LevelEndConditionTrigger,16>& triggers) {
 if(other>=8 || faction>=8)return true;
 const auto state=(rows[faction]>>(4U*other))&15U;
 if(state==2 || state==5 || state==9 || (flags&4U) ||
   (state>=4 && (flags&2U)) || (state>=8 && (flags&1U)))return true;
 if(state>=4)return false;
 const auto mask=retail_relation_bloc(faction,life,rows)|retail_relation_bloc(other,life,rows);
 return retail_relation_predict(faction,mask,life,conditions,triggers) ||
        retail_relation_predict(other,mask,life,conditions,triggers);
}
}
