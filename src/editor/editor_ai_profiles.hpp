#pragma once
#include "thandor/ai_knowledge.hpp"
#include "thandor/binary.hpp"
#include <array>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace editor {
using AiProfileParameters=std::array<std::uint32_t,thandor::AiKnowledgeData::kParameterCount>;
inline std::string_view ai_ini_trim(std::string_view value){
 const auto a=value.find_first_not_of(" \t\r");if(a==std::string_view::npos)return {};
 return value.substr(a,value.find_last_not_of(" \t\r")-a+1);
}
inline std::string ai_ini_slug(std::string_view label){
 std::string out;for(unsigned char c:label){if(c>='A'&&c<='Z')c+=32;
  if((c>='a'&&c<='z')||(c>='0'&&c<='9'))out+=char(c);
  else if(!out.empty()&&out.back()!='_')out+='_';}
 while(!out.empty()&&out.back()=='_')out.pop_back();
 return out;
}
inline std::string ai_profile_encode(const AiProfileParameters& parameters){
 std::ostringstream out;
 out<<"; Original Thandor KI.DAT: 128 exact 32-bit parameters.\n"
    <<"; Values use original integer units: Q12 distances /4096, Q4 resources /16.\n"
    <<"; The pXX prefix is the stable hexadecimal KI index; names are descriptions.\n"
    <<"; Unmapped entries are preserved. Edit values, not the pXX prefixes.\n\n[parameters]\n";
 for(std::size_t i=0;i<parameters.size();++i){
  const auto* semantic=thandor::find_ai_knowledge_semantic(i);
  out<<"; "<<(semantic?semantic->label:"Unmapped original parameter")<<'\n';
  if(semantic)out<<"; Units: "<<thandor::ai_knowledge_value_domain_name(semantic->domain)<<'\n';
  if(i==0x31||i==0x32||i==0x34||i==0x35||i==0x36||i==0x38)
   out<<"; Original special-site scorer reads this offset from its site entry, not KI.DAT.\n";
  out<<'p'<<std::hex<<std::setw(2)<<std::setfill('0')<<i<<'.'
     <<(semantic?ai_ini_slug(semantic->label):"unmapped")<<" = "<<std::dec
     <<static_cast<std::int32_t>(parameters[i])<<'\n';
 }
 return out.str();
}
inline AiProfileParameters ai_profile_decode(std::string_view source,const AiProfileParameters& baseline){
 if(source.starts_with("\xEF\xBB\xBF"))source.remove_prefix(3);
 auto result=baseline;std::array<bool,128> seen{};std::istringstream input{std::string(source)};
 std::string line;bool parameters=false;std::size_t count=0,line_number=0;
 const auto invalid=[&](const char* why){throw std::runtime_error("KI INI line "+std::to_string(line_number)+": "+why);};
 while(std::getline(input,line)){
  ++line_number;auto value=ai_ini_trim(line);if(value.empty()||value.front()==';'||value.front()=='#')continue;
  if(value.front()=='['&&value.back()==']'){parameters=value=="[parameters]";continue;}
  if(!parameters)continue;
  const auto eq=value.find('=');if(eq==std::string_view::npos)invalid("expected parameter=value");
  const auto key=ai_ini_trim(value.substr(0,eq));value=ai_ini_trim(value.substr(eq+1));
  const auto comment=value.find_first_of(";#");if(comment!=std::string_view::npos)value=ai_ini_trim(value.substr(0,comment));
  if(key.size()<4||key[0]!='p'||key[3]!='.')invalid("expected a pXX.name key");
  unsigned index=0;const auto id=std::from_chars(key.data()+1,key.data()+3,index,16);
  if(id.ec!=std::errc{}||id.ptr!=key.data()+3||index>=128)invalid("index outside original KI.DAT");
  if(seen[index])invalid("duplicate parameter index");
  std::int64_t raw=0;int base=10;bool hex=value.starts_with("0x")||value.starts_with("0X");if(hex){base=16;value.remove_prefix(2);}
  const auto parsed=std::from_chars(value.data(),value.data()+value.size(),raw,base);
  if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||raw<INT32_MIN||raw>UINT32_MAX)
   invalid("value must fit an original 32-bit parameter");
  result[index]=static_cast<std::uint32_t>(raw);seen[index]=true;++count;
 }
 if(!count)throw std::runtime_error("KI INI has no [parameters] values");
 return result;
}
inline thandor::AiKnowledgeData ai_profile_knowledge(const AiProfileParameters& parameters){
 std::array<std::byte,512> bytes{};for(std::size_t i=0;i<parameters.size();++i)thandor::write_u32(bytes,i*4,parameters[i]);
 return thandor::AiKnowledgeData::parse(bytes);
}
inline void ai_profile_write(const std::filesystem::path& path,std::string_view content){
 std::filesystem::create_directories(path.parent_path());
 const auto temporary=std::filesystem::path(path.string()+".tmp."+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 {std::ofstream output(temporary,std::ios::binary);if(!output)throw std::runtime_error("Cannot create KI profile: "+path.string());
  output.write(content.data(),static_cast<std::streamsize>(content.size()));output.flush();
  if(!output){std::error_code ignored;std::filesystem::remove(temporary,ignored);throw std::runtime_error("Cannot write KI profile: "+path.string());}}
 std::error_code error;std::filesystem::rename(temporary,path,error);
 if(error){ // Windows cannot rename over an existing destination.
  std::filesystem::copy_file(temporary,path,std::filesystem::copy_options::overwrite_existing,error);
  std::error_code ignored;std::filesystem::remove(temporary,ignored);
  if(error)throw std::runtime_error("Cannot replace KI profile: "+path.string()+": "+error.message());
 }
}
struct EditorAiProfiles {
 AiProfileParameters game{},defaults{},custom{};
 std::filesystem::path directory;
 bool custom_available{},use_custom{};
 std::string message;
 std::filesystem::path path(bool custom_file)const{return directory/(custom_file?"ki.dat_custom.ini":"ki.dat.ini");}
 const AiProfileParameters& active()const{return use_custom?custom:defaults;}
 AiProfileParameters read(bool custom_file)const{
  const auto file=path(custom_file);if(std::filesystem::file_size(file)>524288)throw std::runtime_error("KI profile exceeds 512 KiB");
  const auto bytes=thandor::read_file(file);return ai_profile_decode(std::string_view(reinterpret_cast<const char*>(bytes.data()),bytes.size()),custom_file?defaults:game);
 }
 void initialize(const AiProfileParameters& stock,const std::filesystem::path& root,bool requested_custom){
  game=defaults=custom=stock;directory=root;custom_available=use_custom=false;message.clear();
  try{if(!std::filesystem::exists(path(false)))ai_profile_write(path(false),ai_profile_encode(game));defaults=read(false);}
  catch(const std::exception& error){message=error.what();}
  if(std::filesystem::exists(path(true)))try{custom=read(true);custom_available=true;}
  catch(const std::exception& error){if(!message.empty())message+='\n';message+=error.what();}
  use_custom=requested_custom&&custom_available;
  if(requested_custom&&!custom_available&&message.empty())message="Custom KI profile unavailable; using default values.";
 }
 void select(bool custom_file){
  auto loaded=read(custom_file);if(custom_file){custom=loaded;custom_available=true;}else defaults=loaded;
  use_custom=custom_file;message.clear();
 }
 void save_custom(const AiProfileParameters& edited){
  if(edited==defaults&&!custom_available){use_custom=false;message="Values match the default profile; no custom file was needed.";return;}
  ai_profile_write(path(true),ai_profile_encode(edited));custom=edited;custom_available=true;use_custom=true;message="Saved ki.dat_custom.ini";
 }
};
}
