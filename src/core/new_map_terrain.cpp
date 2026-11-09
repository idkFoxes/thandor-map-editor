#include "thandor/new_map_terrain.hpp"
#include "thandor/terrain_generator.hpp"
#include "thandor/terrain_generation_quality.hpp"
#include <algorithm>
#include <cmath>

namespace thandor {
NewMapTerrainSettings new_map_terrain_defaults(NewMapTerrainType type){
    if(static_cast<unsigned>(type)>3)throw FormatError("unknown new-map terrain type");
    NewMapTerrainSettings result;result.type=type;
    if(type==NewMapTerrainType::ShallowWater)result.water_depth=0.08F;
    return result;
}
void initialize_new_map_terrain(FieldGrid& field,const NewMapTerrainSettings& s){
    if(static_cast<unsigned>(s.type)>3||field.width()<2||field.height()<2)
        throw FormatError("invalid new-map terrain type or dimensions");
    if(!std::isfinite(s.land_height)||!std::isfinite(s.surface_level)||!std::isfinite(s.water_depth)||
       std::abs(s.land_height)>4096||std::abs(s.surface_level)>4096||s.water_depth<0.02F||s.water_depth>64)
        throw FormatError("new-map heights must be finite and within the supported range");
    if(s.type==NewMapTerrainType::ShallowWater&&s.water_depth>0.1F)
        throw FormatError("shallow-water depth must be between 0.02 and 0.10");
    if(s.type==NewMapTerrainType::Island&&s.land_height<=s.surface_level)
        throw FormatError("island land height must be above the surface level");
    const auto surface=float_to_q12(s.surface_level);
    const auto& first=field.at(0,0);const auto& last=field.at(field.width()-1,field.height()-1);
    const float cx=q12_to_float(first.world_x_q12+last.world_x_q12)*0.5F;
    const float cy=q12_to_float(first.world_y_q12+last.world_y_q12)*0.5F;
    const float radius=static_cast<float>(std::min(field.width(),field.height())-1)*0.19F;
    FieldGrid coast_noise;
    if(s.type==NewMapTerrainType::Island){
        TerrainGeneratorSettings noise;noise.pattern=TerrainNoisePattern::DomainWarped;noise.seed=s.seed;
        noise.height=1;noise.feature_size_cells=17;noise.blend_cells=0;noise.keep_water=false;
        noise.build_on_existing=false;noise.connected_building_areas=false;
        coast_noise=generate_terrain_region(field,std::vector<uint8_t>(field.cells().size(),1),noise).field;
    }
    for(uint32_t y=0;y<field.height();++y)for(uint32_t x=0;x<field.width();++x){
        auto& cell=field.at(x,y);float ground=s.land_height;
        if(s.type!=NewMapTerrainType::Land){
            ground=s.surface_level-s.water_depth;
            if(s.type==NewMapTerrainType::Island){
                const float wx=q12_to_float(cell.world_x_q12)-cx,wy=q12_to_float(cell.world_y_q12)-cy;
                const float angle=std::atan2(wy,wx),phase=static_cast<float>(s.seed&65535U)*0.0001F;
                const float coast=radius*(1+0.09F*std::sin(3*angle+phase)+0.045F*std::sin(7*angle-phase)+
                    0.12F*coast_noise.at(x,y).terrain_height());
                const float signed_distance=coast-std::hypot(wx,wy);
                const float relief=signed_distance>=0?s.land_height-s.surface_level:s.water_depth;
                float t=std::clamp(std::abs(signed_distance)/std::max(2.0F,relief/0.23F),0.0F,1.0F);
                t=t*t*(3-2*t);ground=s.surface_level+(signed_distance>=0?1.0F:-1.0F)*relief*t;
            }
        }
        cell.terrain_height_q12=float_to_q12(ground);
        const auto delta=surface-cell.terrain_height_q12;
        cell.water_delta_q12=delta>0?delta:FieldGrid::kDryWaterDeltaQ12;
    }
    if(s.type==NewMapTerrainType::Island)
        align_generated_shores(field,std::vector<float>(field.cells().size(),1));
}
} // namespace thandor
