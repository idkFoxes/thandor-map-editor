// Open-Thandor tools/test/pcx_check.py's pattern, with explicit padded scanlines.
#include "thandor/frontend_player_texture.hpp"
#include "thandor/binary.hpp"
#include <iostream>
#include <stdexcept>
static std::vector<std::byte> fixture(uint32_t stride){
    std::vector<std::byte> b(128);b[0]=std::byte{10};b[2]=std::byte{1};b[3]=std::byte{8};
    b[8]=b[10]=std::byte{63};b[65]=std::byte{1};b[66]=std::byte(stride);
    for(uint32_t y=0;y<64;++y)for(uint32_t x=0;x<stride;++x){
        const uint8_t v=x<64?uint8_t((x+y)%3?((x/5)*13+y*3)&255:0xC3):0xEE;
        if(v>=0xC0)b.push_back(std::byte{0xC1});b.push_back(std::byte(v));
    }
    b.push_back(std::byte{12});for(unsigned i=0;i<256;++i){b.push_back(std::byte(i));b.push_back(std::byte((i*7)&255));b.push_back(std::byte(255-i));}return b;
}
int main()try{
    for(uint32_t stride:{64U,66U,128U}){
        const auto p=thandor::decode_frontend_player_pcx64(fixture(stride));
        for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
            if(p.indexed_pixels[y*64+x]!=uint8_t((x+y)%3?((x/5)*13+y*3)&255:0xC3))throw std::runtime_error("PCX pattern/padding mismatch");
        for(unsigned i=0;i<256;++i)if(p.palette_rgb[i]!=std::array<uint8_t,3>{uint8_t(i),uint8_t(i*7),uint8_t(255-i)})throw std::runtime_error("PCX palette mismatch");
    }
    unsigned rejected=0;
    for(unsigned mutation=0;mutation<5;++mutation){auto b=fixture(64);
        if(mutation==0)b[128]=std::byte{0xC0}; // zero-length RLE
        if(mutation==1)b[b.size()-769]=std::byte{0}; // absent palette marker
        if(mutation==2)b.erase(b.begin()+130,b.end()-769); // truncated pixel data
        if(mutation==3)b[8]=std::byte{64}; // outside frontend's 64x64 domain
        if(mutation==4){
            const auto palette=std::vector<std::byte>(b.end()-769,b.end());b.resize(128);
            for(unsigned left=4096;left;){const auto run=std::min(left,63U);b.push_back(std::byte(0xC0|run));b.push_back(std::byte{0});left-=run;}
            b.insert(b.end(),palette.begin(),palette.end()); // Valid total, invalid per-line runs.
        }
        try{(void)thandor::decode_frontend_player_pcx64(b);}catch(const thandor::FormatError&){++rejected;}
    }
    if(rejected!=5)throw std::runtime_error("malformed/domain PCX accepted");
    std::cout<<"NATIVE_PCX_PATTERN PASS strides=64,66,128 pixels=12288 palettes=768 rejected=5\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
