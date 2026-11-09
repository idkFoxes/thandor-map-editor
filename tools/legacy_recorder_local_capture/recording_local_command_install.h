#ifndef THANDOR_RECORDING_LOCAL_COMMAND_INSTALL_H
#define THANDOR_RECORDING_LOCAL_COMMAND_INSTALL_H
#include "recording_local_command_sites.h"
/* Included by the existing Windows bootstrap after its Win32/patch declarations. */
static void* threc_local_stub_memory;
static int threc_local_hooks_installed;
static int threc_install_local_hooks(void* logger) {
    u32 i,j;
    DWORD old_protection;
    u8* memory;
    if(threc_local_hooks_installed)return 1;
    if(!logger||threc_local_stub_memory)return 0;
    /* Validate every entry and approved return site before changing executable
       memory. An unsupported executable must never get a guessed hook. */
    for(i=0;i<THREC_LOCAL_HANDLER_COUNT;++i){
        const ThRecLocalHandler* h=&threc_local_handlers[i];
        const u8* original=(const u8*)(SIZE_T)h->entry;
        for(j=0;j<h->prologue_size;++j)if(original[j]!=h->prologue[j])return 0;
    }
    for(i=0;i<THREC_LOCAL_CALLER_COUNT;++i){
        const ThRecLocalCaller* c=&threc_local_callers[i];
        const u8* original=(const u8*)(SIZE_T)(c->return_pc-c->call_size);
        for(j=0;j<c->call_size;++j)if(original[j]!=c->call_bytes[j])return 0;
    }
    memory=(u8*)VirtualAlloc(0,THREC_LOCAL_HANDLER_COUNT*THREC_LOCAL_STUB_CAPACITY,
                           MEM_RESERVE|MEM_COMMIT,4u); /* PAGE_READWRITE */
    if(!memory)return 0;
    threc_local_stub_memory=memory;
    for(i=0;i<THREC_LOCAL_HANDLER_COUNT;++i){
        u8* stub=memory+i*THREC_LOCAL_STUB_CAPACITY;
        if(!threc_build_local_stub(stub,THREC_LOCAL_STUB_CAPACITY,(u32)(SIZE_T)stub,
                                  (u32)(SIZE_T)logger,&threc_local_handlers[i]))return 0;
    }
    if(!VirtualProtect(memory,THREC_LOCAL_HANDLER_COUNT*THREC_LOCAL_STUB_CAPACITY,
                       0x20u,&old_protection))return 0; /* PAGE_EXECUTE_READ */
    if(!FlushInstructionCache(GetCurrentProcess(),(LPCSTR)memory,
                              THREC_LOCAL_HANDLER_COUNT*THREC_LOCAL_STUB_CAPACITY))return 0;
    for(i=0;i<THREC_LOCAL_HANDLER_COUNT;++i)
        if(!th_patch_jmp(threc_local_handlers[i].entry,memory+i*THREC_LOCAL_STUB_CAPACITY,
                         threc_local_handlers[i].prologue_size))return 0;
    threc_local_hooks_installed=1;
    return 1;
}
#endif
