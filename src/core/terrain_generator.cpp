#include "thandor/terrain_generator.hpp"
#include "thandor/terrain_generation_quality.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace thandor {
namespace {
float smooth(float v){v=std::clamp(v,0.0F,1.0F);return v*v*(3.0F-2.0F*v);}
uint32_t mix(uint32_t v){v^=v>>16;v*=0x7FEB352DU;v^=v>>15;v*=0x846CA68BU;return v^(v>>16);}
float random_value(int x,int y,uint32_t seed){return static_cast<float>(mix(static_cast<uint32_t>(x)*0x9E3779B9U^static_cast<uint32_t>(y)*0x85EBCA6BU^seed)>>8)/16777215.0F;}
float noise(float x,float y,uint32_t seed){
    const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
    const float tx=smooth(x-static_cast<float>(ix)),ty=smooth(y-static_cast<float>(iy));
    return std::lerp(std::lerp(random_value(ix,iy,seed),random_value(ix+1,iy,seed),tx),
                     std::lerp(random_value(ix,iy+1,seed),random_value(ix+1,iy+1,seed),tx),ty);
}
float fractal(float x,float y,uint32_t seed){
    float sum=0,weight=0,gain=1;
    for(unsigned i=0;i<4;++i){sum+=noise(x,y,seed+i*7151U)*gain;weight+=gain;x*=2.0F;y*=2.0F;gain*=0.45F;}
    return sum/weight;
}
float pattern_noise(float x,float y,const TerrainGeneratorSettings& s){
    switch(s.pattern){
    case TerrainNoisePattern::Classic:return fractal(x,y,s.seed);
    case TerrainNoisePattern::DomainWarped:{
        const float dx=(fractal(x*0.65F+19.1F,y*0.65F-7.3F,s.seed^0xA341316CU)*2-1)*s.warp_strength;
        const float dy=(fractal(x*0.65F-31.7F,y*0.65F+13.8F,s.seed^0xC8013EA4U)*2-1)*s.warp_strength;
        return fractal(x+dx,y+dy,s.seed);
    }
    case TerrainNoisePattern::Cellular:{
        const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
        float nearest=std::numeric_limits<float>::max();
        // Jittered Voronoi sites; the second ring includes sites beyond neighbouring cells.
        for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){
            const int cx=ix+dx,cy=iy+dy;
            const float px=static_cast<float>(cx)+random_value(cx,cy,s.seed);
            const float py=static_cast<float>(cy)+random_value(cx,cy,s.seed^0xAD90777DU);
            nearest=std::min(nearest,std::hypot(x-px,y-py));
        }
        return std::clamp(nearest,0.0F,1.0F);
    }
    }
    return 0;
}
struct RiverPathSample {float distance{},along{};};
struct RiverPath {
    std::vector<TerrainRegionPoint> points;
    std::vector<float> lengths;
    float length{};
    RiverPathSample sample(float x,float y) const {
        RiverPathSample result{std::numeric_limits<float>::max(),0};
        for(std::size_t i=1;i<points.size();++i){
            const auto a=points[i-1],b=points[i];const float dx=b.x-a.x,dy=b.y-a.y,l2=dx*dx+dy*dy;
            if(l2<=0.000001F)continue;
            const float t=std::clamp(((x-a.x)*dx+(y-a.y)*dy)/l2,0.0F,1.0F);
            const float distance=std::hypot(x-a.x-t*dx,y-a.y-t*dy);
            if(distance<result.distance)result={distance,std::lerp(lengths[i-1],lengths[i],t)};
        }
        return result;
    }
};
RiverPath make_river_path(const TerrainGeneratorSettings& s){
    auto controls=s.river_path;
    // Chaikin corner cutting keeps endpoints fixed and rounds freehand corners.
    for(unsigned pass=0;pass<2;++pass){
        std::vector<TerrainRegionPoint> next;next.push_back(controls.front());
        for(std::size_t i=1;i<controls.size();++i){const auto a=controls[i-1],b=controls[i];
            next.push_back({std::lerp(a.x,b.x,0.25F),std::lerp(a.y,b.y,0.25F)});
            next.push_back({std::lerp(a.x,b.x,0.75F),std::lerp(a.y,b.y,0.75F)});
        }
        next.push_back(controls.back());controls=std::move(next);
    }
    std::vector<float> lengths(controls.size());
    for(std::size_t i=1;i<controls.size();++i)lengths[i]=lengths[i-1]+std::hypot(controls[i].x-controls[i-1].x,controls[i].y-controls[i-1].y);
    if(lengths.back()<0.25F)throw FormatError("draw a longer river path");
    RiverPath result;
    const unsigned count=static_cast<unsigned>(std::clamp(std::ceil(lengths.back())+1.0F,2.0F,256.0F));
    std::size_t segment=1;
    for(unsigned i=0;i<count;++i){
        const float along=lengths.back()*static_cast<float>(i)/static_cast<float>(count-1);
        while(segment+1<lengths.size()&&lengths[segment]<along)++segment;
        const auto a=controls[segment-1],b=controls[segment];
        const float span=lengths[segment]-lengths[segment-1],t=span>0?(along-lengths[segment-1])/span:0;
        const float dx=b.x-a.x,dy=b.y-a.y,norm=std::max(std::hypot(dx,dy),0.00001F);
        const float bend=(noise(along/std::max(s.feature_size_cells,4.0F),0,s.seed)*2-1)*s.river_bend*s.river_width_cells*
            std::sin(std::numbers::pi_v<float>*static_cast<float>(i)/static_cast<float>(count-1));
        result.points.push_back({std::lerp(a.x,b.x,t)-dy/norm*bend,std::lerp(a.y,b.y,t)+dx/norm*bend});
        result.lengths.push_back(i?result.lengths.back()+std::hypot(result.points[i].x-result.points[i-1].x,result.points[i].y-result.points[i-1].y):0);
    }
    result.length=result.lengths.back();return result;
}
void validate(const FieldGrid& f,std::span<const uint8_t> mask,const TerrainGeneratorSettings& s,std::span<const uint8_t> protect){
    if(f.width()<2||f.height()<2||f.cells().size()!=mask.size())throw FormatError("terrain selection does not match the map");
    if(!protect.empty()&&protect.size()!=mask.size())throw FormatError("terrain protection mask does not match the map");
    for(float v:{s.height,s.base_height,s.feature_size_cells,s.warp_strength,s.blend_cells,s.water_level,s.river_width_cells,s.river_bend,s.river_angle_degrees,s.shallow_depth})
        if(!std::isfinite(v))throw FormatError("terrain generator settings must be finite");
    if(s.height<0||s.height>256||std::abs(s.base_height)>4096||std::abs(s.water_level)>4096||s.feature_size_cells<1||s.feature_size_cells>4096||
       s.blend_cells<0||s.blend_cells>256||s.river_width_cells<1||s.river_width_cells>4096||s.river_bend<0||s.river_bend>1||s.ford_count>8||
       s.shallow_depth<=0||s.shallow_depth>0.1F||s.material>=26||static_cast<unsigned>(s.preset)>6||static_cast<unsigned>(s.pattern)>2||
       s.warp_strength<0||s.warp_strength>4||(s.terrace_steps!=0&&(s.terrace_steps<2||s.terrace_steps>32))||s.river_path.size()>512)
        throw FormatError("terrain generator setting is outside its supported range");
    for(const auto& p:s.river_path)if(!std::isfinite(p.x)||!std::isfinite(p.y)||p.x<0||p.y<0||p.x>static_cast<float>(f.width()-1)||p.y>static_cast<float>(f.height()-1))
        throw FormatError("river path point is outside the map");
}
}
uint32_t next_terrain_seed(uint32_t seed) noexcept {return seed*1664525U+1013904223U;}
std::vector<uint8_t> terrain_polygon_mask(uint32_t width,uint32_t height,std::span<const TerrainRegionPoint> polygon){
    if(width<2||height<2||static_cast<uint64_t>(width)*height>16777216U)throw FormatError("invalid terrain selection dimensions");
    if(polygon.size()<3||polygon.size()>2048)throw FormatError("draw a closed area with at least three points");
    for(const auto& p:polygon)if(!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>32768||std::abs(p.y)>32768)throw FormatError("invalid terrain outline point");
    std::vector<uint8_t> result(static_cast<std::size_t>(width)*height);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x){
        bool inside=false,on_edge=false;
        const float px=static_cast<float>(x),py=static_cast<float>(y);
        for(std::size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
            const auto a=polygon[j],b=polygon[i];const float dx=b.x-a.x,dy=b.y-a.y;
            const float length2=dx*dx+dy*dy;
            if(length2>0){const float t=std::clamp(((px-a.x)*dx+(py-a.y)*dy)/length2,0.0F,1.0F);
                if(std::hypot(px-(a.x+t*dx),py-(a.y+t*dy))<0.0001F)on_edge=true;}
            if((a.y>py)!=(b.y>py)&&px<(b.x-a.x)*(py-a.y)/(b.y-a.y)+a.x)inside=!inside;
        }
        result[static_cast<std::size_t>(y)*width+x]=(inside||on_edge)?1U:0U;
    }
    return result;
}
void combine_terrain_region(std::vector<uint8_t>& mask,std::span<const uint8_t> shape,TerrainRegionCombine operation){
    if(mask.size()!=shape.size())throw FormatError("terrain selection masks differ in size");
    if(static_cast<unsigned>(operation)>2)throw FormatError("invalid terrain selection operation");
    for(std::size_t i=0;i<mask.size();++i)mask[i]=operation==TerrainRegionCombine::Replace?(shape[i]?1U:0U):
        operation==TerrainRegionCombine::Add?((mask[i]||shape[i])?1U:0U):((mask[i]&&!shape[i])?1U:0U);
}
std::vector<float> terrain_region_weights(const FieldGrid& f,std::span<const uint8_t> mask,const TerrainGeneratorSettings& s,std::span<const uint8_t> protect){
    validate(f,mask,s,protect);const auto w=f.width(),h=f.height();std::vector<float> d(mask.size(),0);
    const float far=static_cast<float>(w+h)+1.0F;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;
        const bool selected=(mask[i]!=0)!=s.outside;
        if(selected&&(protect.empty()||!protect[i])&&(!s.keep_water||f.cells()[i].water_delta_q12<0))
            d[i]=(x==0||y==0||x+1==w||y+1==h)?1.0F:far;
    }
    constexpr float diagonal=1.41421356237F;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;if(d[i]==0)continue;
        if(x)d[i]=std::min(d[i],d[i-1]+1);
        if(y)d[i]=std::min(d[i],d[i-w]+1);
        if(x&&y)d[i]=std::min(d[i],d[i-w-1]+diagonal);
        if(y&&x+1<w)d[i]=std::min(d[i],d[i-w+1]+diagonal);
    }
    for(uint32_t y=h;y-->0;)for(uint32_t x=w;x-->0;){const auto i=static_cast<std::size_t>(y)*w+x;if(d[i]==0)continue;
        if(x+1<w)d[i]=std::min(d[i],d[i+1]+1);
        if(y+1<h)d[i]=std::min(d[i],d[i+w]+1);
        if(x+1<w&&y+1<h)d[i]=std::min(d[i],d[i+w+1]+diagonal);
        if(x&&y+1<h)d[i]=std::min(d[i],d[i+w-1]+diagonal);
    }
    for(float& v:d)v=v==0?0:s.blend_cells==0?1:smooth((v-1.0F)/s.blend_cells);
    return d;
}
TerrainGenerationResult generate_terrain_region(const FieldGrid& source,std::span<const uint8_t> mask,const TerrainGeneratorSettings& s,std::span<const uint8_t> protect){
    TerrainGenerationResult r;r.field=source;r.weights=terrain_region_weights(source,mask,s,protect);r.blocked_river_cells.resize(mask.size());
    const auto w=source.width(),h=source.height();
    const bool lake=s.preset==TerrainGeneratorPreset::Lake,river=s.preset==TerrainGeneratorPreset::River;
    const auto basin_levels=lake?contained_lake_levels(source,r.weights,s.water_level):std::vector<float>{};
    const auto basin_distances=lake?terrain_basin_distances(source,r.weights):std::vector<float>{};
    float river_level=s.water_level;
    if(river)for(std::size_t i=0;i<mask.size();++i)if(r.weights[i]>0){
        const auto& cell=source.cells()[i];
        river_level=std::min(river_level,cell.water_delta_q12>0?cell.water_height():cell.terrain_height()-0.02F);
    }
    r.applied_water_level=river?river_level:s.water_level;
    if(lake)for(std::size_t i=0;i<mask.size();++i)if(r.weights[i]>0)r.applied_water_level=std::min(r.applied_water_level,basin_levels[i]);
    r.water_level_lowered=(lake||river)&&r.applied_water_level<s.water_level-1.0F/4096.0F;
    RiverPath path;
    if(s.preset==TerrainGeneratorPreset::River&&s.use_river_path){
        if(s.river_path.size()<2)throw FormatError("draw a river path before previewing");
        path=make_river_path(s);
    }
    float min_x=static_cast<float>(w),max_x=0,min_y=static_cast<float>(h),max_y=0;
    bool any=false;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)if(r.weights[static_cast<std::size_t>(y)*w+x]>0){
        any=true;min_x=std::min(min_x,static_cast<float>(x));max_x=std::max(max_x,static_cast<float>(x));min_y=std::min(min_y,static_cast<float>(y));max_y=std::max(max_y,static_cast<float>(y));}
    if(!any)throw FormatError("no editable terrain remains in this selection");
    const float center_x=(min_x+max_x)*0.5F,center_y=(min_y+max_y)*0.5F;
    const float angle=s.river_angle_degrees*(std::numbers::pi_v<float>/180.0F),ax=std::cos(angle),ay=std::sin(angle);
    float u_min=std::numeric_limits<float>::max(),u_max=-u_min;
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)if(r.weights[static_cast<std::size_t>(y)*w+x]>0){
        const float u=(static_cast<float>(x)-center_x)*ax+(static_cast<float>(y)-center_y)*ay;u_min=std::min(u_min,u);u_max=std::max(u_max,u);}
    const float u_length=std::max(u_max-u_min,1.0F);
    std::vector<uint8_t> river_footprint(mask.size());
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;const float blend=r.weights[i];if(blend<=0)continue;
        const auto& before=source.cells()[i];auto& after=r.field.cells()[i];const float old=before.terrain_height();
        const float n=s.preset==TerrainGeneratorPreset::River?0:pattern_noise(static_cast<float>(x)/s.feature_size_cells,static_cast<float>(y)/s.feature_size_cells,s);
        const float base=s.build_on_existing?old:s.base_height;
        float target=old;bool water=false;float influence=blend;
        switch(s.preset){
        case TerrainGeneratorPreset::Hills:target=base+(n-0.5F)*2*s.height;break;
        case TerrainGeneratorPreset::Mountains:{
            const float ridges=1-std::abs(n*2-1);
            const float massif=smooth((fractal(static_cast<float>(x)/(s.feature_size_cells*2.3F),
                static_cast<float>(y)/(s.feature_size_cells*2.3F),s.seed^0xD1B54A35U)-0.35F)/0.35F);
            target=base+s.height*massif*massif*ridges*ridges;break;}
        case TerrainGeneratorPreset::Depression:target=base-s.height*(0.65F+0.35F*n);break;
        case TerrainGeneratorPreset::Plateau:target=s.base_height+s.height*(0.98F+0.02F*n);break;
        case TerrainGeneratorPreset::Plains:target=s.base_height+s.height*(n-0.5F)*0.08F;break;
        case TerrainGeneratorPreset::Lake:{
            const float level=basin_levels[i];
            // A distance-limited basin, rather than a flat excavation behind a polygon wall.
            // Small selections get shallow lakes; a deep bed requires space for its banks.
            const float run=std::max(0.0F,basin_distances[i]-(1.5F+1.5F*n));
            const float bed=std::min(s.base_height,level)-s.height*(0.8F+0.2F*n);
            target=std::min(old,std::max(bed,old-run*0.22F));water=true;break;}
        case TerrainGeneratorPreset::River:{
            const float dx=static_cast<float>(x)-center_x,dy=static_cast<float>(y)-center_y;
            float u=dx*ax+dy*ay;const float v=-dx*ay+dy*ax;
            const float bend=(noise(u/std::max(s.feature_size_cells,4.0F),0,s.seed)*2-1)*s.river_bend*s.river_width_cells;
            float distance=std::abs(v-bend);
            if(s.use_river_path){const auto nearest=path.sample(static_cast<float>(x),static_cast<float>(y));distance=nearest.distance;u=nearest.along;}
            const float half=s.river_width_cells*0.5F,bank=std::max(2.0F,s.blend_cells);
            const float profile=1-smooth((distance-half)/bank);
            if(profile<=0)continue;
            const float depth=s.drivable_river?s.shallow_depth:s.height;
            target=river_level-depth*profile;influence*=profile;
            for(unsigned ford=0;ford<s.ford_count;++ford){const float u_ford=(s.use_river_path?0:u_min)+(s.use_river_path?path.length:u_length)*static_cast<float>(ford+1)/static_cast<float>(s.ford_count+1);
                const float ramp=1-smooth((std::abs(u-u_ford)-2.0F)/3.0F);target=std::lerp(target,river_level+0.06F,ramp);}
            water=true;river_footprint[i]=profile>=0.98F?1U:0U;break;}
        }
        if(s.terrace_steps&&s.height>0&&s.preset!=TerrainGeneratorPreset::Lake&&s.preset!=TerrainGeneratorPreset::River){
            const float step=s.height/static_cast<float>(s.terrace_steps);
            target=s.base_height+std::round((target-s.base_height)/step)*step;
        }
        const float final_height=std::clamp(std::lerp(old,target,influence),-8192.0F,8192.0F);
        after.terrain_height_q12=float_to_q12(final_height);
        if(before.water_delta_q12>=0){
            const int64_t surface=static_cast<int64_t>(before.terrain_height_q12)+before.water_delta_q12;
            after.water_delta_q12=static_cast<int32_t>(std::clamp<int64_t>(surface-after.terrain_height_q12,std::numeric_limits<int32_t>::min(),std::numeric_limits<int32_t>::max()));
        } // Dry cells retain their negative depth, so lowering dry ground does not invent water.
        if(water){const int64_t delta=static_cast<int64_t>(float_to_q12(lake?basin_levels[i]:river_level))-after.terrain_height_q12;
            after.water_delta_q12=delta>0?static_cast<int32_t>(delta):FieldGrid::kDryWaterDeltaQ12;}
        if(river&&water&&after.water_delta_q12>0)river_footprint[i]=1;
        if(s.paint_material&&influence>=0.5F)after.set_material(s.material);

    }
    if(river&&s.drivable_river)grade_drivable_river_banks(r,river_footprint,river_level);
    if(s.preset==TerrainGeneratorPreset::River){
        // A dry bank still contributes its hidden water plane to the native six-neighbour normal.
        // Align a one-vertex dry collar with the surface and terminate wet cells inside the mask.
        // Without this, a default -1-unit dry delta makes a shallow ford classify as steep water.
        constexpr std::array<std::pair<int,int>,6> neighbours{{{1,0},{0,1},{0,-1},{-1,0},{-1,1},{1,-1}}};
        std::vector<uint8_t> collar=river_footprint;
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;if(!river_footprint[i])continue;
            bool at_border=false;
            for(const auto& [dx,dy]:neighbours){const int nx=static_cast<int>(x)+dx,ny=static_cast<int>(y)+dy;
                if(nx<0||ny<0||nx>=static_cast<int>(w)||ny>=static_cast<int>(h))continue;
                const auto j=static_cast<std::size_t>(ny)*w+static_cast<unsigned>(nx);
                if(r.weights[j]<=0)at_border=true;else collar[j]=1;
            }
            if(at_border&&r.field.cells()[i].water_delta_q12>0)r.field.cells()[i].water_delta_q12=-1;
        }
        for(std::size_t i=0;i<collar.size();++i)if(collar[i]&&r.weights[i]>0&&r.field.cells()[i].water_delta_q12<=0){
            auto& cell=r.field.cells()[i];const auto delta=static_cast<int64_t>(float_to_q12(river_level))-cell.terrain_height_q12;
            cell.water_delta_q12=static_cast<int32_t>(std::clamp<int64_t>(delta,std::numeric_limits<int32_t>::min(),-1));
        }
    }
    const bool whole_map=std::all_of(mask.begin(),mask.end(),[&](uint8_t v){return (v!=0)!=s.outside;});
    const bool land=s.preset==TerrainGeneratorPreset::Hills||s.preset==TerrainGeneratorPreset::Mountains||
        s.preset==TerrainGeneratorPreset::Plateau||s.preset==TerrainGeneratorPreset::Plains;
    if(whole_map&&!s.terrace_steps&&(s.preset==TerrainGeneratorPreset::Hills||s.preset==TerrainGeneratorPreset::Mountains))erode_generated_slopes(r);
    if(whole_map&&land&&(s.connected_building_areas||s.preset==TerrainGeneratorPreset::Mountains))reserve_connected_building_areas(source,r,protect);
    align_generated_shores(r.field,r.weights);
    if(s.preset==TerrainGeneratorPreset::River)for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){const auto i=static_cast<std::size_t>(y)*w+x;if(!river_footprint[i])continue;
        ++r.river_cells;if(r.field.cells()[i].water_delta_q12>0)++r.river_water_cells;bool blocked=false;for(uint32_t band=1;band<7;++band)blocked|=r.field.terrain_forbidden_for_traversal_band(x,y,band);
        if(blocked){r.blocked_river_cells[i]=1;++r.blocked_river_count;}
    }
    for(std::size_t i=0;i<source.cells().size();++i){const auto& before=source.cells()[i];const auto& after=r.field.cells()[i];
        if(after.terrain_height_q12!=before.terrain_height_q12||after.water_delta_q12!=before.water_delta_q12||after.material()!=before.material())++r.changed_cells;}
    return r;
}
} // namespace thandor
