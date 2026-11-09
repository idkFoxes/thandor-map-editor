// Cases from Open-Thandor's Thandor_SelfTestCodecNegative, via the public archive API.
#include "thandor/pck.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace thandor;
std::vector<std::byte> extract(std::vector<std::byte> payload,uint32_t output,uint32_t method=0){
    const std::array entries{PckWriteEntry{"test/data.bin",std::move(payload),0}};
    auto bytes=build_stored_pck(entries);write_u32(bytes,0x200+0x1f0,output);write_u32(bytes,0x200+0x1fc,method);
    const auto pck=PckArchive::parse("fixture.pck",std::move(bytes));return pck.extract(pck.entries()[0]);
}
int main()try{
    unsigned checked=0,failures=0;
    auto rejects=[&](const char* label,std::vector<std::byte> payload,uint32_t output,uint32_t method=0){
        bool rejected=false;try{(void)extract(std::move(payload),output,method);}catch(const FormatError&){rejected=true;}
        ++checked;if(!rejected){++failures;std::cerr<<"accepted malformed codec input: "<<label<<'\n';}
    };
    std::vector<std::byte> valid(260);valid[0]=valid[1]=std::byte{1};
    if(extract(valid,1)!=std::vector<std::byte>{std::byte{0}})throw std::runtime_error("valid two-symbol literal failed");
    rejects("zero decoded size",valid,0);
    auto single=valid;single[0]=single[1]=std::byte{0};single[0x41]=std::byte{5};
    rejects("single-symbol tree",single,16);
    rejects("empty tree",std::vector<std::byte>(260),16);
    auto short_window=valid;short_window.resize(257);rejects("short native dword window",short_window,1);
    rejects("short frequency table",std::vector<std::byte>(128),16);
    rejects("late native dword window",valid,5);
    rejects("truncated bitstream",valid,4096);
    std::vector<std::byte> field(16+256+128+4);write_u32(field,0,512);
    field[16]=field[17]=std::byte{1}; // 512 literal zero bytes in a zero-cell compact FLD
    rejects("zero-cell field",field,512,2);
    const auto stored=extract({},0,1);if(!stored.empty())throw std::runtime_error("empty stored entry changed");
    // Native truncates the final run to the declared output size; preserve that valid quirk.
    valid[256]=std::byte{31}; // flag=1, run=18, left symbol=0
    if(extract(valid,1)!=std::vector<std::byte>{std::byte{0}})throw std::runtime_error("final-run clipping changed");
    if(failures)throw std::runtime_error("native codec rejection cases differ: "+std::to_string(failures));
    std::cout<<"NATIVE_CODEC_GUARDS PASS negative_cases="<<checked<<" stored_empty=1 final_run_clip=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
