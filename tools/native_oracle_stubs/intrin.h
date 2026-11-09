#pragma once
inline void* _InterlockedExchangePointer(void* volatile* p,void* v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
inline long _InterlockedExchange(volatile long* p,long v){return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST);}
