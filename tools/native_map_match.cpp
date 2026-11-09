#include "thandor/catalog.hpp"
#include "thandor/level.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
using namespace thandor;
// Input columns are deliberately raw: ACTR words0,1,4,5,19. Spatial and
// faction interpretations are tested against LEV records, not assumed.
int main(int argc,char** argv)try{
 if(argc!=3)throw std::runtime_error("usage: native_map_match PCK_DIRECTORY FIRST_ACTR_WORDS.tsv");
 struct Row{uint32_t word0,word1;int32_t word4,word5;uint32_t word19;};
 std::ifstream input(argv[2]);std::vector<Row> rows;Row row{};
 while(input>>row.word0>>row.word1>>row.word4>>row.word5>>row.word19)rows.push_back(row);
 if(rows.empty()||!input.eof())throw std::runtime_error("invalid/empty ACTR input");
 const auto assets=AssetCatalog::scan_archive_root(argv[1]);
 size_t best=0;std::string best_map;std::vector<LevelPlacement> best_placements;
 for(const auto& map:assets.map_groups()){
  const auto level=LevelAsset::parse(assets.load(map.level_path));
  std::map<std::pair<int32_t,int32_t>,uint32_t> native,serialized;
  for(const auto& r:rows)++native[{r.word4,r.word5}];
  for(const auto& p:level.placements())++serialized[{p.world_y_q12,p.world_x_q12}];
  size_t shared=0;for(const auto& [point,count]:native)if(serialized.contains(point))shared+=std::min(count,serialized.at(point));
  std::cout<<"MAP_MATCH lev="<<std::quoted(map.level_path)<<" placements="<<level.placements().size()
           <<" native_rows="<<rows.size()<<" exact_xy_matches="<<shared<<'\n';
  if(shared>best){best=shared;best_map=map.level_path;best_placements.assign(level.placements().begin(),level.placements().end());}
 }
 size_t forward_xy=0,reverse_xy=0;
 for(const auto& r:rows)if(r.word0>0&&r.word0<=best_placements.size()){
  const auto matches=[&](const LevelPlacement& p){return r.word4==p.world_y_q12&&r.word5==p.world_x_q12;};
  forward_xy+=matches(best_placements[r.word0-1]);
  reverse_xy+=matches(best_placements[best_placements.size()-r.word0]);
 }
 const bool reverse=reverse_xy>forward_xy;
 size_t order_xy=0,faction=0;
 for(const auto& r:rows)if(r.word0>0&&r.word0<=best_placements.size()){
  const auto& p=best_placements[reverse?best_placements.size()-r.word0:r.word0-1];
  const bool exact=r.word4==p.world_y_q12&&r.word5==p.world_x_q12;
  order_xy+=exact;faction+=exact&&r.word1==p.faction_index;
  if(exact)std::cout<<"INITIAL_CORRELATION word0="<<r.word0<<" arm="<<p.army_asset_id
      <<" lev_faction="<<p.faction_index<<" word1="<<r.word1<<" word19="<<r.word19<<'\n';
 }
 std::cout<<"BEST_NATIVE_MAP lev="<<std::quoted(best_map)<<" exact_xy="<<best<<" rows="<<rows.size()
          <<" forward_order_xy="<<forward_xy<<" reverse_order_xy="<<reverse_xy
          <<" reversed="<<reverse<<" actor_order_xy="<<order_xy<<" word1_equals_lev_faction="<<faction
          <<"; word1 is NOT established as faction; initial spatial correlation only,not a full recorder schema\n";
 return best==rows.size()&&order_xy==rows.size()?0:1;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
