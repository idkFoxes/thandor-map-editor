// Campaign audit helper. Does not modify the game root.
#include "thandor/pck.hpp"
#include "thandor/text_resource.hpp"
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <fstream>
int main(int argc,char** argv){
 if(argc!=3){std::cerr<<"Usage: campaign_inventory GAME_ROOT EXTRACTED_OUTPUT\n";return 2;}
 const std::filesystem::path root=argv[1],output=argv[2];
 for(const auto& file:std::filesystem::directory_iterator(root)){
  if(thandor::lower_ascii(file.path().extension().string())!=".pck")continue;
  auto p=thandor::PckArchive::open(file.path());
  for(const auto& e:p.entries()){
   const auto& n=e.normalized_name;
   if(n.ends_with(".cgn")||n.ends_with(".lev")||n.ends_with(".fld")||n.ends_with(".flm")||n=="texte/level.str"||(n.ends_with(".dat")&&n.starts_with("level/")))
    std::cout<<"ENTRY\t"<<file.path().filename().string()<<'\t'<<n<<'\t'<<e.name<<'\t'<<e.unpacked_size<<'\n';
   if(n.starts_with("level/")&&(n.ends_with(".cgn")||n.ends_with(".lev")||n.ends_with(".dat"))){
    // Only the named campaign/map files are materialized; inventory still covers terrain/movies.
    (void)thandor::canonical_pck_entry_path(n);
    const auto path=output/n;std::filesystem::create_directories(path.parent_path());
    thandor::write_file_atomic(path,p.extract(e));
   }
   if(n=="texte/level.str"){
    auto t=thandor::TextResourceAsset::parse(p.extract(e));
    for(auto i:{0U,1U,7U,12U})std::cout<<"TITLE\t"<<i<<'\t'<<std::quoted(t.string(0x20+i))<<'\n';
   }
  }
 }
}
