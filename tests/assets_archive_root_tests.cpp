#include "thandor/catalog.hpp"
#include "thandor/pck.hpp"
#include "thandor/text_resource.hpp"
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace thandor;
int main()try{
 const auto root=std::filesystem::temp_directory_path()/
     ("thandor-v645-archives-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directory(root);
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code e;std::filesystem::remove_all(p,e);}}cleanup{root};
 auto require=[](bool value,const char* why){if(!value)throw std::runtime_error(why);};
 std::vector<PckWriteEntry> base{{"level/probe.str",TextResourceAsset::minimal_level_compatibility("base").serialize(),0}};
 std::vector<PckWriteEntry> patch{{"level/probe.str",TextResourceAsset::minimal_level_compatibility("patched").serialize(),0}};
 write_stored_pck(root/"DATEN.PCK",base);write_stored_pck(root/"PATCH00.PCK",patch);
 const auto catalog=AssetCatalog::scan_archive_root(root);
 require(TextResourceAsset::parse(catalog.load("level/probe.str")).compatibility_display_name()=="patched",
     "archive-only scan lost patch precedence");
 require(!has_supported_game_executable(root),"archive audit fabricated an executable");
 bool rejected=false;
 try{(void)AssetCatalog::scan_game_root(root);}catch(const std::runtime_error&){rejected=true;}
 require(rejected,"installation scan silently bypassed executable requirement");
 std::cout<<"V645_ARCHIVE_ROOT PASS archive-only scan,patch precedence,installation gate,no executable fabricated\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
