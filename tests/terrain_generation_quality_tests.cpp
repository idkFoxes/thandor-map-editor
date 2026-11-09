#include "thandor/terrain_generation_quality.hpp"
#include "thandor/new_map_terrain.hpp"
#include "thandor/simulation_placement.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace thandor;
void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
int main()try{
    auto f=FieldGrid::create(67,67,float_to_q12(2),0);
    const TerrainRegionPoint ring[]{{10,10},{56,10},{56,56},{10,56}};
    auto selected=terrain_polygon_mask(67,67,ring);TerrainGeneratorSettings s;
    s.preset=TerrainGeneratorPreset::Lake;s.keep_water=false;s.water_level=30;s.base_height=2;s.height=12;s.blend_cells=0;
    const auto lake=generate_terrain_region(f,selected,s);
    check(lake.water_level_lowered&&lake.applied_water_level<2,"high requested lake water was not limited by the rim");
    std::size_t wet=0,coast=0;
    for(uint32_t y=1;y+1<f.height();++y)for(uint32_t x=1;x+1<f.width();++x){
        const auto i=static_cast<std::size_t>(y)*f.width()+x;const auto& cell=lake.field.at(x,y);
        if(!selected[i])check(cell.terrain_height_q12==f.at(x,y).terrain_height_q12&&cell.water_delta_q12==f.at(x,y).water_delta_q12,"lake modified terrain outside the area");
        if(cell.water_delta_q12<=0)continue;
        ++wet;const auto plane=cell.terrain_height_q12+cell.water_delta_q12;
        check(plane==float_to_q12(lake.applied_water_level),"lake water is not flat");
        for(const auto& offset:std::vector<std::pair<int,int>>{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}}){
            const auto& bank=lake.field.at(static_cast<unsigned>(static_cast<int>(x)+offset.first),static_cast<unsigned>(static_cast<int>(y)+offset.second));
            if(bank.water_delta_q12>0)continue;
            ++coast;check(plane<bank.terrain_height_q12,"water stands above the shoreline land");
            check(bank.terrain_height_q12+bank.water_delta_q12==plane,"dry shoreline corner forms a raised water wall");
        }
    }
    check(wet>200&&coast>30,"lake regression has no meaningful basin/shoreline");
    check(lake.field.at(33,33).terrain_height()>-4,"small lake made a twelve-unit vertical pit");
    // Every rendered mixed wet/dry triangle remains on one water plane.
    const auto vertices=lake.field.build_water_vertices();
    for(std::size_t i=0;i<vertices.size();i+=3)
        check(std::abs(vertices[i].position.z-vertices[i+1].position.z)<.0003F&&std::abs(vertices[i].position.z-vertices[i+2].position.z)<.0003F,"shore water packet is sloped into a wall");
    // Separate low and high basins receive separate spill limits, without raising either rim.
    auto split=f;std::vector<float> weights(split.cells().size());
    for(uint32_t y=0;y<67;++y)for(uint32_t x=0;x<67;++x){split.at(x,y).terrain_height_q12=float_to_q12(x<33?1:5);
        if(y>8&&y<58&&((x>7&&x<26)||(x>40&&x<59)))weights[static_cast<std::size_t>(y)*67+x]=1;}
    auto levels=contained_lake_levels(split,weights,20);
    check(levels[30*67+15]<1&&levels[30*67+48]>4&&levels[30*67+48]<5,"disconnected basins did not use their own spill heights");
    // Protection and pre-existing water remain immutable, including hidden dry planes.
    std::vector<uint8_t> protected_cells(selected.size());protected_cells[33*67+33]=1;
    const auto protected_lake=generate_terrain_region(f,selected,s,protected_cells);
    check(protected_lake.field.at(33,33).terrain_height_q12==f.at(33,33).terrain_height_q12&&protected_lake.field.at(33,33).water_delta_q12==f.at(33,33).water_delta_q12,"lake changed a protected cell");
    // Seeded islands are not radial discs and mixed shoreline triangles stay level.
    auto island=FieldGrid::create(91,91);auto initial=new_map_terrain_defaults(NewMapTerrainType::Island);initialize_new_map_terrain(island,initial);
    auto island2=FieldGrid::create(91,91);initial.seed=next_terrain_seed(initial.seed);initialize_new_map_terrain(island2,initial);
    check(island.serialize()!=island2.serialize(),"island coastline ignores the seed");
    const auto shore_vertices=island.build_water_vertices();
    for(const auto& v:shore_vertices)check(std::abs(v.position.z)<.0003F,"island shoreline has a raised water corner");
    // Use the real game terrain classifier and placement callback, not an imitation noise test.
    unsigned cases=0;
    for(auto size:{59U,91U,131U})for(auto pattern:{TerrainNoisePattern::Classic,TerrainNoisePattern::DomainWarped,TerrainNoisePattern::Cellular})
    for(uint32_t seed:{1U,9182U,20261027U}){
        auto source=FieldGrid::create(size,size,float_to_q12(2),0);std::vector<uint8_t> whole(source.cells().size(),1);
        TerrainGeneratorSettings mountains;mountains.preset=TerrainGeneratorPreset::Mountains;mountains.pattern=pattern;mountains.height=24;mountains.seed=seed;mountains.keep_water=false;
        const auto result=generate_terrain_region(source,whole,mountains);
        if(!result.playability_ok)std::cerr<<"failed layout "<<size<<" "<<static_cast<int>(pattern)<<" seed="<<seed<<" sites="<<result.building_sites.size()<<" connected="<<result.connected_building_sites<<'\n';
        check(result.playability_checked&&result.playability_ok&&result.connected_building_sites==4,"whole-map mountains lack four connected building areas");
        for(const auto& site:result.building_sites){const auto& cell=result.field.at(static_cast<uint32_t>(site.x),static_cast<uint32_t>(site.y));
            const Vec3f world{q12_to_float(cell.world_x_q12),q12_to_float(cell.world_y_q12),cell.terrain_height()};
            check(simulation_placement_terrain_clear(result.field,world,cell.terrain_height_q12,0xC00U,0),"reserved site fails actual native placement terrain callback");
        }
        auto loaded=FieldGrid::parse(result.field.serialize());auto roundtrip=result;roundtrip.field=std::move(loaded);assess_generated_ground(roundtrip);
        check(roundtrip.playability_ok,"native FLD roundtrip loses terrain connectivity");++cases;
    }
    // Impossible layouts must report failure, rather than bulldozing protected terrain.
    auto blocked=FieldGrid::create(59,59);std::vector<uint8_t> all(blocked.cells().size(),1);
    TerrainGeneratorSettings mountains;mountains.preset=TerrainGeneratorPreset::Mountains;
    const auto impossible=generate_terrain_region(blocked,all,mountains,std::vector<uint8_t>(all.size(),0));
    check(impossible.playability_ok,"flat starting map cannot reserve building areas");
    for(auto& cell:blocked.cells())cell.flags_and_material|=0x80000000U;
    const auto hard=generate_terrain_region(blocked,all,mountains);
    check(hard.playability_checked&&!hard.playability_ok,"hard-blocked map was advertised as playable");
    mountains.connected_building_areas=false;
    auto mandatory=generate_terrain_region(FieldGrid::create(59,59),all,mountains);
    check(mandatory.playability_checked&&mandatory.playability_ok,"whole-map mountains can disable required building access");
    std::vector<uint8_t> protected_start(all.size());
    for(unsigned y=28;y<=30;++y)for(unsigned x=28;x<=30;++x)protected_start[y*59+x]=1;
    const auto loaded_start=generate_terrain_region(FieldGrid::create(59,59),all,mountains,protected_start);
    check(loaded_start.protected_approaches.size()==1,"protected start/building approaches were not audited");
    check(loaded_start.field.at(29,29).terrain_height_q12==0,"protected start terrain was changed");
    auto disconnected=mandatory;disconnected.protected_approaches={{{1,1}}};assess_generated_ground(disconnected);
    check(disconnected.connected_building_sites==4&&disconnected.connected_protected_areas==0&&!disconnected.playability_ok,
        "a layout can pass despite a protected approach outside its clearance network");
    auto valley=FieldGrid::create(67,67,float_to_q12(2));
    for(uint32_t y=0;y<67;++y)for(uint32_t x=0;x<67;++x)
        valley.at(x,y).terrain_height_q12=float_to_q12(2+5*std::exp(-std::pow((static_cast<float>(x)-33)/10,2)));
    TerrainGeneratorSettings channel;channel.preset=TerrainGeneratorPreset::River;channel.water_level=.5F;
    channel.keep_water=false;channel.river_width_cells=5;channel.blend_cells=5;
    auto graded=generate_terrain_region(valley,std::vector<uint8_t>(67*67,1),channel);
    std::size_t checked_wet=0;
    for(uint32_t y=1;y+1<67;++y)for(uint32_t x=1;x+1<67;++x)if(graded.field.at(x,y).water_delta_q12>0){
        for(uint32_t band=1;band<7;++band)check(!graded.field.terrain_forbidden_for_traversal_band(x,y,band),"graded river channel fails native ground admission");
        ++checked_wet;
    }
    check(checked_wet>100,"river grading regression has no wet channel");
    // Thermal transport lowers a spike without changing total material or protected cells.
    TerrainGenerationResult talus;talus.field=FieldGrid::create(59,59);talus.weights.assign(59*59,1);talus.field.at(30,30).terrain_height_q12=float_to_q12(12);
    const auto flags=talus.field.at(30,30).flags_and_material;erode_generated_slopes(talus);
    double sum=0;for(const auto& c:talus.field.cells())sum+=c.terrain_height();
    check(talus.field.at(30,30).terrain_height()<6&&std::abs(sum-12)<.1,"talus relaxation does not smooth/conserve the terrain");
    check(talus.field.at(30,30).flags_and_material==flags,"erosion changed native flags");
    std::cout<<"V768_TERRAIN_QUALITY PASS contained lakes, flat water packets, protected boundaries, seeded islands; "<<cases<<" native mountain connectivity/placement/roundtrip cases\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
