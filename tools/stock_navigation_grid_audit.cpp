#include "thandor/catalog.hpp"
#include "thandor/map_document.hpp"
#include "thandor/retail_placement.hpp"
#include "thandor/simulation_lab.hpp"
#include <iostream>
int main(int argc,char**argv)try{
 if(argc!=2)throw std::runtime_error("Usage: stock_navigation_grid_audit PCK_ROOT");
 const auto catalog=thandor::AssetCatalog::scan_archive_root(argv[1]);
 uint64_t total=0,changed=0;unsigned maps=0;
 std::cout<<"map,scratch_cells,changed_terrain_cells,changed_percent\n";
 for(const auto& group:catalog.map_groups()){
  const auto doc=thandor::MapDocument::open(catalog,group);const auto& f=doc.field();
  const auto exact=thandor::retail_build_terrain_scratch(f);
  std::vector<uint32_t> cells;cells.reserve(f.cells().size());
  for(uint32_t y=0;y<f.height();++y)for(uint32_t x=0;x<f.width();++x)
   cells.push_back(f.terrain_compatibility(x,y).classification_bits);
  uint64_t mismatches=0,count=0;
  for(uint32_t y=2;y+2<exact.height;++y)for(uint32_t x=2;x+2<exact.width;++x){
   const auto old=cells[size_t((y-2)/4)*f.width()+(x-2)/4];
   // Compare terrain bands only; map-edge growth is recorded separately by tests.
   if((old&0x7F000000U)!=(exact.read(x,y)&0x7F000000U))++mismatches;
   ++count;
  }
  std::cout<<group.level_path<<','<<count<<','<<mismatches<<','<<100.*double(mismatches)/double(count)<<std::endl;
  total+=count;changed+=mismatches;++maps;
 }
 std::cerr<<"STOCK_GRID_AUDIT maps="<<maps<<" cells="<<total<<" changed="<<changed<<" percent="<<100.*double(changed)/double(total)<<'\n';
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
