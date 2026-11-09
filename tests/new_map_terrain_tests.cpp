#include "thandor/new_map_terrain.hpp"
#include "thandor/terrain_generator.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace thandor;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}check(caught,"invalid initial terrain accepted");}
int main()try{
    for(const auto type:{NewMapTerrainType::Land,NewMapTerrainType::ShallowWater,NewMapTerrainType::DeepWater,NewMapTerrainType::Island}){
        auto field=FieldGrid::create(67,59,0,7);field.at(20,20).flags_and_material|=0x20000000U;
        const auto flags=field.at(20,20).flags_and_material;
        auto settings=new_map_terrain_defaults(type);initialize_new_map_terrain(field,settings);
        std::size_t wet=0,dry=0;
        for(const auto& cell:field.cells()){
            check(cell.material()==7,"new-map terrain changed materials");
            if(cell.water_delta_q12>0){++wet;check(cell.terrain_height_q12+cell.water_delta_q12==float_to_q12(settings.surface_level),"new-map surface is not level");}
            else ++dry;
        }
        check(field.at(20,20).flags_and_material==flags,"new-map terrain changed unrelated native flags");
        if(type==NewMapTerrainType::Land){
            check(wet==0&&dry==field.cells().size()&&field.at(30,30).terrain_height_q12==8192,"land does not start dry above surface level");
            TerrainGeneratorSettings lake;lake.preset=TerrainGeneratorPreset::Lake;lake.base_height=2;lake.height=3;lake.water_level=0.5F;
            const std::vector<uint8_t> selection(field.cells().size(),1);
            const auto generated=generate_terrain_region(field,selection,lake);
            check(generated.field.at(30,30).water_delta_q12>0,"default land cannot be carved into a lake");
        }else if(type==NewMapTerrainType::Island){
            check(wet>0&&dry>0,"island needs both water and dry land");
            check(field.at(33,29).terrain_height_q12>0&&field.at(33,29).water_delta_q12<0,"island center is not dry land");
            check(field.at(0,0).water_delta_q12>0,"island has no surrounding water");
        }else{
            check(wet==field.cells().size()&&dry==0,"water start has dry cells");
            if(type==NewMapTerrainType::ShallowWater){
                check(field.at(30,30).water_delta_q12>0&&field.at(30,30).water_delta_q12<500,"shallow start exceeds native shallow-water cutoff");
                for(uint32_t y=1;y+1<field.height();++y)for(uint32_t x=1;x+1<field.width();++x)
                    for(uint32_t band=1;band<7;++band)check(!field.terrain_forbidden_for_traversal_band(x,y,band),"flat shallow start fails native terrain admission");
            }else check(field.at(30,30).water_delta_q12==8192,"deep-water depth is wrong");
        }
        const auto bytes=field.serialize();check(FieldGrid::parse(bytes).serialize()==bytes,"initial terrain did not roundtrip through native FLD");
        settings.surface_level=-3;settings.land_height=-1;initialize_new_map_terrain(field,settings);
        for(const auto& cell:field.cells())if(cell.water_delta_q12>0)check(cell.terrain_height_q12+cell.water_delta_q12==float_to_q12(-3),"nonzero surface plane was ignored");
    }
    // Zero is a valid ground elevation; a higher water plane can cover it.
    auto zero=FieldGrid::create(59,59,0,0);std::vector<uint8_t> all(zero.cells().size(),1);
    check(zero.set_water_selection(all,0.5F,true)==all.size()&&zero.at(20,20).water_delta_q12==2048,"water over zero-height ground is impossible");
    auto zero_settings=new_map_terrain_defaults(NewMapTerrainType::Land);zero_settings.land_height=0;zero_settings.surface_level=0.5F;
    initialize_new_map_terrain(zero,zero_settings);
    check(std::all_of(zero.cells().begin(),zero.cells().end(),[](const auto& cell){return cell.terrain_height_q12==0&&cell.water_delta_q12==2048;}),"custom zero-height start ignored its higher surface plane");
    auto field=FieldGrid::create(59,59);const auto before=field.serialize();auto s=new_map_terrain_defaults(NewMapTerrainType::Island);
    s.land_height=s.surface_level;rejects([&]{initialize_new_map_terrain(field,s);});
    s=new_map_terrain_defaults(NewMapTerrainType::ShallowWater);s.water_depth=0.2F;rejects([&]{initialize_new_map_terrain(field,s);});
    s=new_map_terrain_defaults(NewMapTerrainType::Land);s.surface_level=std::numeric_limits<float>::quiet_NaN();rejects([&]{initialize_new_map_terrain(field,s);});
    s=new_map_terrain_defaults(NewMapTerrainType::Land);s.type=static_cast<NewMapTerrainType>(9);rejects([&]{initialize_new_map_terrain(field,s);});
    check(field.serialize()==before,"invalid initial terrain partially mutated the field");
    std::cout<<"V764_NEW_MAP_TERRAIN PASS land, shallows, deep water, island, level surfaces, lake carving, zero-height water, native admission, flags, validation and FLD roundtrip\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
