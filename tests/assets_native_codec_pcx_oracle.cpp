// Build only through verify_native_codec_pcx.py with an external Open-Thandor checkout.
#include THANDOR_NATIVE_CODEC_SOURCE
#include THANDOR_NATIVE_PCX_SOURCE
#include "thandor/pck.hpp"
#include "thandor/frontend_player_texture.hpp"
#include <cstdlib>
#include <iostream>
#include <random>
#include <new>
static uint32_t allocate(uint32_t bytes,void** out){*out=::operator new(bytes,std::nothrow);return *out?0:1;}
static uint32_t release(void* ptr){::operator delete(ptr);return 0;}
MemoryApiTable g_MemoryApi{allocate,release,nullptr,nullptr,nullptr};
void Thandor_Log(const char*,...){}
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
using namespace thandor;
static void require(bool value,const char* label){if(!value)throw std::runtime_error(label);}
static std::vector<std::byte> editor_decode(std::vector<std::byte> packed,uint32_t output,uint32_t method){
  const std::array entries{PckWriteEntry{"test.bin",std::move(packed),0}};
  auto image=build_stored_pck(entries);write_u32(image,0x3f0,output);write_u32(image,0x3fc,method);
  auto archive=PckArchive::parse("oracle.pck",std::move(image));return archive.extract(archive.entries()[0]);
}
static bool native_decode(std::vector<std::byte>& input,uint32_t output,uint32_t method,std::vector<std::byte>& result){
  result.assign(output+64,std::byte{0});std::fill(result.begin()+output,result.end(),std::byte{0xA5});
  uint32_t count=0,error=0;auto source=reinterpret_cast<uint8_t*>(input.data());auto dest=reinterpret_cast<uint8_t*>(result.data());
  const bool ok=method==0?PckCodec_DecodeHuffmanRle(output,dest,uint32_t(input.size()),source,&count,&error):
    PckCodec_DecodeFieldGrid(output,reinterpret_cast<FieldGridAsset*>(dest),uint32_t(input.size()),source,&count,&error);
  require(std::all_of(result.begin()+output,result.end(),[](auto b){return b==std::byte{0xA5};}),"native output guard changed");
  result.resize(output);return ok;
}
static void reject_both(std::vector<std::byte> input,uint32_t output,uint32_t method){
  std::vector<std::byte> decoded;require(!native_decode(input,output,method,decoded),"native accepted negative fixture");
  bool rejected=false;try{(void)editor_decode(input,output,method);}catch(const FormatError&){rejected=true;}
  require(rejected,"editor accepted native-rejected fixture");
}
static std::vector<std::byte> pcx(uint32_t width,uint32_t stride){
  std::vector<std::byte> b(128);b[0]=std::byte{10};b[2]=std::byte{1};b[3]=std::byte{8};b[65]=std::byte{1};
  b[8]=std::byte(width-1);b[10]=std::byte{63};b[66]=std::byte(stride&255);b[67]=std::byte(stride>>8);
  for(uint32_t y=0;y<64;++y)for(uint32_t x=0;x<stride;++x){
    const uint8_t v=x<width?uint8_t((x+y)%3?((x/5)*13+y*3)&255:0xC3):0xEE;
    if(v>=0xC0)b.push_back(std::byte{0xC1});b.push_back(std::byte(v));
  }
  b.push_back(std::byte{12});for(unsigned i=0;i<256;++i){b.push_back(std::byte(i));b.push_back(std::byte((i*7)&255));b.push_back(std::byte(255-i));}return b;
}
int main(int argc,char** argv)try{
  uint32_t positive=0,stock=0,fields=0,negative=0;std::mt19937 rng(12345);
  for(uint32_t size:{1U,2U,3U,17U,18U,19U,256U,4096U,100000U})for(unsigned pattern=0;pattern<3;++pattern){
    std::vector<std::byte> raw(size);for(auto& b:raw)b=std::byte(pattern==0?0x41:pattern==1?rng()%3:rng()%256);
    std::vector<std::byte> packed(size*2+4096);uint32_t count=0,error=0;
    require(PckCodec_EncodeHuffmanRle(uint32_t(packed.size()),reinterpret_cast<uint8_t*>(packed.data()),size,reinterpret_cast<uint8_t*>(raw.data()),&count,&error),"native encoding failed");packed.resize(count);
    std::vector<std::byte> decoded;require(native_decode(packed,size,0,decoded)&&decoded==raw,"native roundtrip failed");
    require(editor_decode(packed,size,0)==raw,"editor/native encoding interoperability failed");++positive;
  }
  std::vector<std::byte> valid(260);valid[0]=valid[1]=std::byte{1};
  reject_both(valid,0,0);++negative;
  auto single=valid;single[1]=std::byte{0};reject_both(single,16,0);++negative;
  reject_both(std::vector<std::byte>(260),16,0);++negative;
  auto short_input=valid;short_input.resize(257);reject_both(short_input,1,0);++negative;
  reject_both(std::vector<std::byte>(128),16,0);++negative;
  reject_both(valid,5,0);++negative; // Fifth literal needs a dword at byte 257.
  reject_both(valid,4096,0);++negative;
  std::vector<std::byte> emptyfield(16+256+128+4);write_u32(emptyfield,0,512);emptyfield[16]=emptyfield[17]=std::byte{1};
  reject_both(emptyfield,512,2);++negative;
  if(argc>1)for(const auto& path:std::filesystem::directory_iterator(argv[1])){
    if(lower_ascii(path.path().extension().string())!=".pck" || lower_ascii(path.path().stem().string())=="filme")continue;
    const auto bytes=read_file(path.path());const auto archive=PckArchive::parse(path.path(),bytes);
    for(const auto& entry:archive.entries())if(entry.compression_method!=thandor::PckCompressionMethod::Stored){
      auto data=std::vector<std::byte>(bytes.begin()+entry.data_offset,bytes.begin()+entry.data_offset+entry.packed_size);
      std::vector<std::byte> decoded;
      if(!native_decode(data,entry.unpacked_size,uint32_t(entry.compression_method),decoded) || decoded!=archive.extract(entry)){
        std::cerr<<"stock codec divergence "<<path.path().filename()<<":"<<entry.name<<'\n';return 1;}
      ++stock;fields+=entry.compression_method==thandor::PckCompressionMethod::FieldGrid;
    }
  }
  for(uint32_t stride:{64U,66U,128U}){
    auto bytes=pcx(64,stride);PcxIndexedImage native{};
    require(Pcx_DecodeIndexed8(reinterpret_cast<const uint8_t*>(bytes.data()),uint32_t(bytes.size()),&native),"native PCX failed");
    const auto editor=decode_frontend_player_pcx64(bytes);
    bool matches=native.width==64&&native.height==64&&std::equal(editor.indexed_pixels.begin(),editor.indexed_pixels.end(),native.pixels);
    for(size_t i=0;i<256;++i){const auto& c=editor.palette_rgb[i];matches&=native.paletteColors[i]==(0xFF000000U|uint32_t(c[0])<<16|uint32_t(c[1])<<8|c[2]);}
    Pcx_FreeIndexed8(&native);require(matches,"native/editor PCX pixels or palette differ");
  }
  auto crossing=pcx(64,64);
  const auto palette=std::vector<std::byte>(crossing.end()-769,crossing.end());crossing.resize(128);
  for(unsigned left=4096;left;){const auto run=std::min(left,63U);crossing.push_back(std::byte(0xC0|run));crossing.push_back(std::byte{0});left-=run;}
  crossing.insert(crossing.end(),palette.begin(),palette.end());PcxIndexedImage malformed{};
  require(!Pcx_DecodeIndexed8(reinterpret_cast<const uint8_t*>(crossing.data()),uint32_t(crossing.size()),&malformed),"native accepted crossing PCX run");
  bool crossing_rejected=false;try{(void)decode_frontend_player_pcx64(crossing);}catch(const FormatError&){crossing_rejected=true;}
  require(crossing_rejected,"editor accepted native-rejected crossing PCX run");
  auto odd=pcx(65,66);PcxIndexedImage native{};
  require(Pcx_DecodeIndexed8(reinterpret_cast<const uint8_t*>(odd.data()),uint32_t(odd.size()),&native),"native odd PCX failed");Pcx_FreeIndexed8(&native);
  bool rejected=false;try{(void)decode_frontend_player_pcx64(odd);}catch(const FormatError&){rejected=true;}require(rejected,"frontend must remain 64x64");
  std::cout<<"NATIVE_CODEC_PCX PASS seed=12345 native_encoded_roundtrips="<<positive<<" native_negative_cases="<<negative<<" stock_compressed_entries="<<stock<<" stock_fields="<<fields<<" pcx_padded_patterns=3 crossing_run_rejected=1 intentional_65x64_rejection=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
