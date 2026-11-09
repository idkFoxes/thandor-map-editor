#ifndef THANDOR_RECORDING_LOCAL_COMMAND_HOOK_H
#define THANDOR_RECORDING_LOCAL_COMMAND_HOOK_H
/* Shared by the Windows recorder patch and the portable ABI/regression tools.
   No host pointers or compiler-generated wrapper assembly enter the x86 stub. */
typedef unsigned int ThRecU32;
typedef unsigned char ThRecU8;
typedef char ThRecU32_must_be_4[(sizeof(ThRecU32)==4)?1:-1];
typedef struct ThRecLocalHandler {
    ThRecU32 entry, command, prologue_size;
    ThRecU8 prologue[6];
} ThRecLocalHandler;
typedef struct ThRecLocalCaller {
    ThRecU32 return_pc, command, call_size;
    ThRecU8 call_bytes[7];
} ThRecLocalCaller;
#define THREC_LOCAL_STUB_CAPACITY 64u
#define THREC_LOCAL_ANY_COMMAND 0xffffffffu
#ifdef _MSC_VER
#define THREC_INLINE static __inline
#else
#define THREC_INLINE static inline
#endif
THREC_INLINE void threc_put_u32(ThRecU8* p,ThRecU32 value) {
    p[0]=(ThRecU8)value;p[1]=(ThRecU8)(value>>8);
    p[2]=(ThRecU8)(value>>16);p[3]=(ThRecU8)(value>>24);
}
THREC_INLINE int threc_local_capture_allowed(int enabled,ThRecU32 role_flags,
    ThRecU32 local_player,ThRecU32 player,ThRecU32 return_pc,ThRecU32 command,
    const ThRecLocalCaller* callers,ThRecU32 count) {
    ThRecU32 i;
    if(!enabled||(role_flags&3u)!=0u||player>=255u||player!=local_player)return 0;
    for(i=0;i<count;++i)
        if(callers[i].return_pc==return_pc &&
           (callers[i].command==command||callers[i].command==THREC_LOCAL_ANY_COMMAND))return 1;
    return 0;
}
/* Produces the unchanged 32-byte UCMD payload. The handler's argument1 is
   queue-record +0x0C; argument3 is +0x04, as verified in the original queue. */
THREC_INLINE void threc_local_packet(ThRecU32* packet,ThRecU32 tick,ThRecU32 faction,
    ThRecU32 command,const ThRecU32* original_stack) {
    packet[0]=1u;packet[1]=tick;packet[2]=original_stack[1];packet[3]=faction;
    packet[4]=command;packet[5]=original_stack[2];
    packet[6]=original_stack[3];packet[7]=original_stack[4];
}
THREC_INLINE ThRecU32 threc_build_local_stub(ThRecU8* out,ThRecU32 capacity,
    ThRecU32 stub_address,ThRecU32 logger_address,const ThRecLocalHandler* handler) {
    ThRecU32 at=0,i,slot;
    if(!out||!handler||handler->prologue_size<5u||handler->prologue_size>6u||
       capacity<32u+handler->prologue_size)return 0;
    out[at++]=0x9c;out[at++]=0x60; /* pushfd; pushad */
    out[at++]=0x8d;out[at++]=0x44;out[at++]=0x24;out[at++]=0x24; /* lea eax,[esp+36] */
    out[at++]=0x50; /* push original handler-entry ESP */
    out[at++]=0x68;threc_put_u32(out+at,handler->command);at+=4;
    out[at++]=0xe8;threc_put_u32(out+at,logger_address-(stub_address+at+4u));at+=4;
    out[at++]=0x83;out[at++]=0xc4;out[at++]=0x08; /* cdecl logger, two arguments */
    out[at++]=0x61;out[at++]=0x9d; /* popad; popfd */
    for(i=0;i<handler->prologue_size;++i)out[at++]=handler->prologue[i];
    /* Tail jump through a memory-held pointer. Do not clobber EAX or another
       restored caller register: the earlier AI hook had that exact defect. */
    out[at++]=0xff;out[at++]=0x25;slot=stub_address+at+4u;
    threc_put_u32(out+at,slot);at+=4;
    threc_put_u32(out+at,handler->entry+handler->prologue_size);at+=4;
    return at;
}
#undef THREC_INLINE
#endif
