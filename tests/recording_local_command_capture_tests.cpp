#include "../tools/legacy_recorder_local_capture/recording_local_command_sites.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint32_t word(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}
int main()try{
 unsigned approved=0,excluded=0;
 for(const auto& caller:threc_local_callers){
  const auto code=caller.command==THREC_LOCAL_ANY_COMMAND?0xd40U:caller.command;
  const auto allowed=[&](int enabled,uint32_t role,uint32_t local,uint32_t player,uint32_t pc,uint32_t command){
   return threc_local_capture_allowed(enabled,role,local,player,pc,command,threc_local_callers,THREC_LOCAL_CALLER_COUNT)!=0;
  };
  check(allowed(1,0,7,7,caller.return_pc,code),"approved native UI input was excluded");++approved;
  for(uint32_t role:{1U,2U,3U}){check(!allowed(1,role,7,7,caller.return_pc,code),"network-applied command duplicated as local");++excluded;}
  check(!allowed(0,0,7,7,caller.return_pc,code),"disabled recorder captured a command");++excluded;
  check(!allowed(1,0,7,6,caller.return_pc,code),"foreign player accepted as local");++excluded;
  check(!allowed(1,0,255,255,caller.return_pc,code),"reserved player id accepted");++excluded;
  check(!allowed(1,0,7,7,0x0053C811U,code),"AI/non-UI caller accepted");++excluded;
  check(!allowed(1,0,7,0xffffffffU,caller.return_pc,code),"native AI sentinel accepted as local player");++excluded;
  if(caller.command!=THREC_LOCAL_ANY_COMMAND){check(!allowed(1,0,7,7,caller.return_pc,code+1),"direct call site accepted a different command");++excluded;}
 }
 std::array<ThRecU32,5> stack{0x56d13e,7,0x80000001,0x76543210,0xffffffff};
 std::array<ThRecU32,8> packet{};threc_local_packet(packet.data(),123,2,0xfe0,stack.data());
 check(packet==std::array<ThRecU32,8>{1,123,7,2,0xfe0,0x80000001,0x76543210,0xffffffff},"UCMD field/argument order differs from original queue layout");
 unsigned stubs=0;
 for(const auto& handler:threc_local_handlers){
  std::array<unsigned char,THREC_LOCAL_STUB_CAPACITY> bytes{};const uint32_t base=0x61001000,logger=0x61002000;
  auto n=threc_build_local_stub(bytes.data(),bytes.size(),base,logger,&handler);
  check(n==32+handler.prologue_size&&bytes[0]==0x9c&&bytes[1]==0x60,"stub size or preservation prefix invalid");
  check(word(bytes.data()+8)==handler.command,"stub embeds wrong command code");
  check(base+17+word(bytes.data()+13)==logger,"stub logger relative call target incorrect");
  check(bytes[20]==0x61&&bytes[21]==0x9d,"stub does not restore registers and flags before native prologue");
  for(uint32_t i=0;i<handler.prologue_size;++i)check(bytes[22+i]==handler.prologue[i],"native stolen prologue changed");
  check(bytes[n-10]==0xff&&bytes[n-9]==0x25&&word(bytes.data()+n-8)==base+n-4&&
        word(bytes.data()+n-4)==handler.entry+handler.prologue_size,"tail jump clobbers a register or has wrong continuation");
  check(!threc_build_local_stub(bytes.data(),n-1,base,logger,&handler),"short output buffer accepted");++stubs;
 }
 std::cout<<"LOCAL_COMMAND_CAPTURE PASS approved="<<approved<<" excluded="<<excluded<<" stubs="<<stubs<<" packet_order=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
