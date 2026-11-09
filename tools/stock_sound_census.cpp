#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/audio.hpp"
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
using namespace thandor;
int main(int argc,char** argv)try{
 if(argc!=2)throw std::runtime_error("usage: stock_sound_census PCK_DIRECTORY");
 const auto assets=AssetCatalog::scan_archive_root(argv[1]);
 const auto runtime=simulation_lab_prepare_runtime_catalog(assets);
 std::set<uint32_t> slots;
 const auto add=[&](uint32_t slot){if(slot)slots.insert(slot);};
 for(const auto& [id,model]:runtime->definitions){
  (void)id;const auto cls=model.runtime_class_id;const auto& p=model.simulation;
  if(cls==1||cls==2||cls==17||cls==18||cls==19){add(p.projected_sound_asset_id_d0);add(p.projected_sound_asset_id_d8);}
  if(cls>=5&&cls<=8)add(p.projected_sound_asset_id_d8);
  if(cls==4||cls==10||cls==11||cls==13||cls==14||cls==16||cls==21||cls==22)add(p.projected_sound_asset_id_primary);
  if(cls==13||cls==22)add(p.projected_sound_asset_id_secondary);
  if(cls==3)add(p.articulated_contact_sound_asset_id);
 }
 for(const auto& [id,effect]:runtime->effects){(void)id;add(effect.definition.positioned_sound_table_index());}
 for(const auto& [id,shot]:runtime->shots){(void)id;add(shot.positioned_sound_table_index());}
 std::map<uint32_t,std::vector<std::string>> samples;
 for(const auto& asset:assets.assets())if(asset.kind==AssetKind::Sound)
  if(const auto slot=sam_trailing_decimal_index(asset.normalized_path))samples[*slot].push_back(asset.normalized_path);
 uint32_t missing=0,decoded=0,ambiguous=0,unsupported_templates=0;
 for(const auto& map:assets.map_groups()){
  const auto level=LevelAsset::parse(assets.load(map.level_path));
  if(level.paths().sounds!="sound/sound??"){
   ++unsupported_templates;std::cout<<"UNSUPPORTED_SOUND_TEMPLATE lev="<<map.level_path
       <<" pattern="<<level.paths().sounds<<'\n';
  }
 }
 for(const auto slot:slots){
  // The default stock LEV template is sound/sound??. Do not substitute another
  // directory's sample merely because its trailing number matches.
  std::string path="sound/sound"+(slot<10?std::string("0"):std::string{})+std::to_string(slot)+".sam";
  if(!assets.find(path)){++missing;std::cout<<"MISSING slot="<<slot<<'\n';continue;}
  ambiguous+=samples[slot].size()>1;
  const auto pcm=decode_sam_to_pcm(assets.load(path));
  if(pcm.empty())throw std::runtime_error("empty SAM: "+path);
  ++decoded;std::cout<<"SAMPLE slot="<<slot<<" frames="<<pcm.interleaved_stereo.size()/2<<'\n';
 }
 std::cout<<"V645_STOCK_SOUND referenced_slots="<<slots.size()<<" decoded="<<decoded
          <<" missing="<<missing<<" duplicate_suffix_slots="<<ambiguous
          <<" levels_checked="<<assets.map_groups().size()<<" unsupported_templates="<<unsupported_templates
          <<"; native model/effect/shot references and stock-template PCM decode,not speaker output\n";
 return missing||unsupported_templates?1:0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
