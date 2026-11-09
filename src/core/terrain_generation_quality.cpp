#include "thandor/terrain_generation_quality.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace thandor {
namespace {
constexpr std::array<std::pair<int,int>,6> directions{{{1,0},{0,1},{-1,1},{-1,0},{0,-1},{1,-1}}};
template<class F>void adjacent(const FieldGrid& f,std::size_t i,F visit){
    const int x=static_cast<int>(i%f.width()),y=static_cast<int>(i/f.width());
    for(const auto& [dx,dy]:directions){const int xx=x+dx,yy=y+dy;
        if(xx>=0&&yy>=0&&xx<static_cast<int>(f.width())&&yy<static_cast<int>(f.height()))
            visit(static_cast<std::size_t>(yy)*f.width()+static_cast<unsigned>(xx));}
}
float ease(float t){t=std::clamp(t,0.0F,1.0F);return t*t*(3-2*t);}
float distance(const FieldGrid& f,std::size_t a,std::size_t b){
    const auto& ca=f.cells()[a];const auto& cb=f.cells()[b];
    return std::hypot(q12_to_float(ca.world_x_q12-cb.world_x_q12),q12_to_float(ca.world_y_q12-cb.world_y_q12));
}
void set_dry_height(FieldCell& cell,float height){
    cell.terrain_height_q12=float_to_q12(height);cell.water_delta_q12=FieldGrid::kDryWaterDeltaQ12;
}
// Conservative, dry building footprint: every cell in a six-cell-radius native hexagon
// must be within the game's +/-0x400 Q12 terrain-height tolerance. No support bits invented.
bool building_footprint(const FieldGrid& f,std::size_t i){
    const int x=static_cast<int>(i%f.width()),y=static_cast<int>(i/f.width());
    constexpr int radius=5;
    if(x<radius||y<radius||x+radius>=static_cast<int>(f.width())||y+radius>=static_cast<int>(f.height()))return false;
    const auto reference=f.cells()[i].terrain_height_q12;
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
        if(std::abs(dx+dy)>radius)continue;
        const auto& c=f.at(static_cast<unsigned>(x+dx),static_cast<unsigned>(y+dy));
        if(c.water_delta_q12>=0||(c.flags_and_material&0x88006000U)!=0U||
            std::abs(static_cast<int64_t>(c.terrain_height_q12)-reference)>0x400)return false;
    }
    return true;
}
}

std::vector<float> contained_lake_levels(const FieldGrid& source,std::span<const float> weights,float requested){
    std::vector<float> levels(weights.size(),requested);std::vector<uint8_t> seen(weights.size());
    for(std::size_t start=0;start<weights.size();++start){
        if(weights[start]<=0||seen[start])continue;
        std::vector<std::size_t> cells{start};seen[start]=1;float spill=requested;
        for(std::size_t pos=0;pos<cells.size();++pos){const auto i=cells[pos];const auto& cell=source.cells()[i];
            const auto x=i%source.width(),y=i/source.width();
            if(x==0||y==0||x+1==source.width()||y+1==source.height())spill=std::min(spill,cell.terrain_height()-0.04F);
            adjacent(source,i,[&](std::size_t j){
                if(weights[j]<=0){const auto& rim=source.cells()[j];
                    spill=std::min(spill,rim.water_delta_q12>0?rim.water_height():rim.terrain_height()-0.04F);
                }else if(!seen[j]){seen[j]=1;cells.push_back(j);}
            });
        }
        for(const auto i:cells)levels[i]=spill;
    }
    return levels;
}

std::vector<float> terrain_basin_distances(const FieldGrid& f,std::span<const float> weights){
    std::vector<float> distances(weights.size(),std::numeric_limits<float>::max());std::vector<std::size_t> queue;
    for(std::size_t i=0;i<weights.size();++i){
        const auto x=i%f.width(),y=i/f.width();bool rim=weights[i]<=0||x==0||y==0||x+1==f.width()||y+1==f.height();
        if(!rim)adjacent(f,i,[&](std::size_t j){rim|=weights[j]<=0;});
        if(rim){distances[i]=0;queue.push_back(i);}
    }
    for(std::size_t p=0;p<queue.size();++p){const auto i=queue[p];adjacent(f,i,[&](std::size_t j){
        if(distances[j]>distances[i]+1){distances[j]=distances[i]+1;queue.push_back(j);}});}
    return distances;
}

void align_generated_shores(FieldGrid& field,std::span<const float> weights){
    // Read wet cells only, so two iterations/call order cannot grow or flood a shoreline.
    std::vector<int32_t> planes(field.cells().size(),std::numeric_limits<int32_t>::max());
    for(std::size_t i=0;i<field.cells().size();++i){const auto& wet=field.cells()[i];if(wet.water_delta_q12<=0)continue;
        const int32_t surface=wet.terrain_height_q12+wet.water_delta_q12;
        adjacent(field,i,[&](std::size_t j){if(weights[j]>0&&field.cells()[j].water_delta_q12<0)planes[j]=std::min(planes[j],surface);});
    }
    for(std::size_t i=0;i<planes.size();++i)if(planes[i]!=std::numeric_limits<int32_t>::max()){
        auto& cell=field.cells()[i];cell.water_delta_q12=static_cast<int32_t>(std::min<int64_t>(-1,
            static_cast<int64_t>(planes[i])-cell.terrain_height_q12));
    }
}

void assess_generated_ground(TerrainGenerationResult& r){
    const auto& f=r.field;std::vector<uint8_t> pass(f.cells().size());
    r.traversable_cells=0;r.reachable_cells=0;r.connected_building_sites=0;r.connected_protected_areas=0;
    for(uint32_t y=1;y+1<f.height();++y)for(uint32_t x=1;x+1<f.width();++x){
        const auto i=static_cast<std::size_t>(y)*f.width()+x;const auto c=f.terrain_compatibility(x,y);
        pass[i]=!c.hard_boundary_or_blocker&&(c.classification_bits&0x7E000000U)==0;
        r.traversable_cells+=pass[i]!=0;
    }
    // Erode by one native neighbour: connectivity is checked with clearance, not just point cells.
    auto clear=pass;
    for(std::size_t i=0;i<pass.size();++i)if(pass[i])adjacent(f,i,[&](std::size_t j){if(!pass[j])clear[i]=0;});
    std::vector<uint8_t> reached(pass.size());std::vector<std::size_t> best;
    for(std::size_t i=0;i<clear.size();++i)if(clear[i]&&!reached[i]){
        std::vector<std::size_t> component{i};reached[i]=1;
        for(std::size_t p=0;p<component.size();++p)adjacent(f,component[p],[&](std::size_t j){
            if(clear[j]&&!reached[j]){reached[j]=1;component.push_back(j);}});
        if(component.size()>best.size())best=std::move(component);
    }
    std::fill(reached.begin(),reached.end(),0);for(auto i:best)reached[i]=1;r.reachable_cells=best.size();
    for(const auto& p:r.building_sites){const auto i=static_cast<std::size_t>(p.y)*f.width()+static_cast<unsigned>(p.x);
        if(i<reached.size()&&reached[i]&&building_footprint(f,i))++r.connected_building_sites;}
    for(const auto& approaches:r.protected_approaches){
        const bool connected=std::any_of(approaches.begin(),approaches.end(),[&](const TerrainRegionPoint& p){
            const auto i=static_cast<std::size_t>(p.y)*f.width()+static_cast<unsigned>(p.x);return i<reached.size()&&reached[i];});
        r.connected_protected_areas+=connected;
    }
    r.playability_ok=r.building_sites.size()>=4&&r.connected_building_sites==r.building_sites.size()&&
        r.connected_protected_areas==r.protected_approaches.size();
}

void erode_generated_slopes(TerrainGenerationResult& r,unsigned iterations){
    auto& f=r.field;std::vector<float> deltas(f.cells().size());
    for(unsigned pass=0;pass<iterations;++pass){
        std::fill(deltas.begin(),deltas.end(),0);
        for(std::size_t i=0;i<f.cells().size();++i){
            if(r.weights[i]<=0||f.cells()[i].water_delta_q12>=0)continue;
            adjacent(f,i,[&](std::size_t j){if(j<=i||r.weights[j]<=0||f.cells()[j].water_delta_q12>=0)return;
                const float difference=f.cells()[i].terrain_height()-f.cells()[j].terrain_height();
                const float excess=std::max(0.0F,std::abs(difference)-distance(f,i,j)*0.55F);
                const float flow=std::copysign(excess*0.075F*std::min(r.weights[i],r.weights[j]),difference);
                deltas[i]-=flow;deltas[j]+=flow;
            });
        }
        for(std::size_t i=0;i<deltas.size();++i)if(deltas[i]!=0)
            f.cells()[i].terrain_height_q12=float_to_q12(f.cells()[i].terrain_height()+deltas[i]);
    }
}

void grade_drivable_river_banks(TerrainGenerationResult& r,std::vector<uint8_t>& footprint,float surface){
    auto& f=r.field;using Entry=std::pair<int32_t,std::size_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    const auto editable=[&](std::size_t i){return r.weights[i]>0&&f.cells()[i].water_delta_q12<0&&
        (f.cells()[i].flags_and_material&0x88006000U)==0U;};
    for(std::size_t i=0;i<footprint.size();++i)if(footprint[i]&&r.weights[i]>0)
        queue.emplace(f.cells()[i].terrain_height_q12,i);
    // Monotone angle-of-repose projection, in world distances on the native hex lattice.
    // Both sides of an active edge are considered: an older low crease must pull the new
    // approach down too. Fixed water beds, protected cells and hard blockers are immutable.
    const auto lower=[&](std::size_t i,int32_t limit){
        if(!editable(i))return;
        const auto target=std::max(float_to_q12(surface+0.02F),limit);auto& cell=f.cells()[i];
        if(target>=cell.terrain_height_q12)return;
        cell.terrain_height_q12=target;cell.water_delta_q12=std::min(-1,float_to_q12(surface)-target);
        footprint[i]=1;queue.emplace(target,i);
    };
    while(!queue.empty()){
        const auto [height,i]=queue.top();queue.pop();if(height!=f.cells()[i].terrain_height_q12)continue;
        adjacent(f,i,[&](std::size_t j){
            const auto step=std::max(1,float_to_q12(distance(f,i,j)*0.30F));
            lower(i,f.cells()[j].terrain_height_q12+step);
            lower(j,f.cells()[i].terrain_height_q12+step);
        });
    }
}

void reserve_connected_building_areas(const FieldGrid& source,TerrainGenerationResult& r,std::span<const uint8_t> protect){
    auto& f=r.field;const auto count=f.cells().size();r.playability_checked=true;
    // A loaded map's protected starts/buildings need an approach into the same ground network.
    // Check their original traversable perimeter, rather than asking units to occupy a building.
    if(!protect.empty()){
        std::vector<uint8_t> seen(count);
        for(std::size_t start=0;start<count;++start)if(protect[start]&&!seen[start]){
            std::vector<std::size_t> component{start};seen[start]=1;std::vector<TerrainRegionPoint> approaches;
            for(std::size_t p=0;p<component.size();++p)adjacent(source,component[p],[&](std::size_t j){
                if(protect[j]){if(!seen[j]){seen[j]=1;component.push_back(j);}return;}
                const auto x=static_cast<uint32_t>(j%f.width()),y=static_cast<uint32_t>(j/f.width());
                const auto c=source.terrain_compatibility(x,y);
                if(!c.hard_boundary_or_blocker&&(c.classification_bits&0x7E000000U)==0)
                    approaches.push_back({static_cast<float>(x),static_cast<float>(y)});
            });
            if(!approaches.empty())r.protected_approaches.push_back(std::move(approaches));
        }
    }
    std::vector<uint8_t> editable(count),pad_candidate(count),route_allowed(count);
    for(std::size_t i=0;i<count;++i)editable[i]=r.weights[i]>=0.999F&&f.cells()[i].water_delta_q12<0&&
        (f.cells()[i].flags_and_material&0x88006000U)==0U;
    for(int y=4;y+4<static_cast<int>(f.height());++y)for(int x=4;x+4<static_cast<int>(f.width());++x){
        bool fits=true;
        for(int dy=-4;dy<=4&&fits;++dy)for(int dx=-4;dx<=4;++dx){if(std::abs(dx+dy)>4)continue;
            if(!editable[static_cast<std::size_t>(y+dy)*f.width()+static_cast<unsigned>(x+dx)]){fits=false;break;}}
        route_allowed[static_cast<std::size_t>(y)*f.width()+static_cast<unsigned>(x)]=fits;
    }
    // A ten-cell apron keeps pads and their transitions wholly inside editable terrain.
    for(int y=10;y+10<static_cast<int>(f.height());y+=2)for(int x=10;x+10<static_cast<int>(f.width());x+=2){
        bool fits=true;
        for(int dy=-10;dy<=10&&fits;++dy)for(int dx=-10;dx<=10;++dx){if(std::abs(dx+dy)>10)continue;
            if(!editable[static_cast<std::size_t>(y+dy)*f.width()+static_cast<unsigned>(x+dx)]){fits=false;break;}}
        if(fits)pad_candidate[static_cast<std::size_t>(y)*f.width()+static_cast<unsigned>(x)]=1;
    }
    std::vector<std::size_t> sites;
    float separation=12.0F;
    while(sites.size()<4){float best=std::numeric_limits<float>::max();std::size_t chosen=count;
        for(std::size_t i=0;i<count;++i)if(pad_candidate[i]){
            float nearest=1000;for(const auto j:sites)nearest=std::min(nearest,distance(f,i,j));
            if(!sites.empty()&&nearest<separation)continue;
            // Low, widely separated valley sites, rather than a square imposed on the map.
            const float score=f.cells()[i].terrain_height()-std::min(nearest,35.0F)*0.22F;
            if(score<best){best=score;chosen=i;}}
        if(chosen==count){if(separation>6.0F){separation=std::max(6.0F,separation*0.8F);continue;}break;}
        sites.push_back(chosen);pad_candidate[chosen]=0;
    }
    if(sites.empty()){assess_generated_ground(r);return;}
    float floor=0;for(auto i:sites)floor+=f.cells()[i].terrain_height();floor/=static_cast<float>(sites.size());
    // Track a single height target and maximum weight per cell, making overlapping shoulders
    // independent of path/pad iteration order. Cuts become broad valley floors, with smooth banks.
    std::vector<float> weights(count),targets(count,floor);
    const auto stamp=[&](std::size_t center,float core,float apron){
        const int x=static_cast<int>(center%f.width()),y=static_cast<int>(center/f.width());
        const int radius=static_cast<int>(std::ceil(core+apron));
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
            const int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=static_cast<int>(f.width())||yy>=static_cast<int>(f.height()))continue;
            const auto j=static_cast<std::size_t>(yy)*f.width()+static_cast<unsigned>(xx);if(!editable[j])continue;
            const float dist=static_cast<float>(std::max({std::abs(dx),std::abs(dy),std::abs(dx+dy)}));
            weights[j]=std::max(weights[j],1-ease((dist-core)/apron));
        }
    };
    // Least-cost connections favour low terrain. Water, hard blockers and protected footprints
    // are impassable to planning, so a loaded map's islands/buildings are never silently bridged.
    using QueueEntry=std::pair<float,std::size_t>;
    for(std::size_t target=1;target<sites.size();++target){
        std::vector<float> cost(count,std::numeric_limits<float>::max());std::vector<std::size_t> previous(count,count);
        std::priority_queue<QueueEntry,std::vector<QueueEntry>,std::greater<QueueEntry>> queue;
        for(std::size_t j=0;j<target;++j){cost[sites[j]]=0;queue.emplace(0,sites[j]);}
        while(!queue.empty()){const auto [value,i]=queue.top();queue.pop();if(value!=cost[i])continue;if(i==sites[target])break;
            adjacent(f,i,[&](std::size_t j){if(!route_allowed[j])return;
                const float elevation=f.cells()[j].terrain_height();
                const float next=value+1.0F+std::abs(elevation-floor)*1.6F+std::abs(elevation-f.cells()[i].terrain_height())*2;
                if(next<cost[j]){cost[j]=next;previous[j]=i;queue.emplace(next,j);}});
        }
        if(previous[sites[target]]==count)continue;
        for(auto i=sites[target];i<count;i=previous[i]){stamp(i,3,5);if(cost[i]==0)break;}
    }
    for(auto i:sites){stamp(i,6,4);r.building_sites.push_back({static_cast<float>(i%f.width()),static_cast<float>(i/f.width())});}
    for(std::size_t i=0;i<count;++i)if(weights[i]>0)set_dry_height(f.cells()[i],std::lerp(f.cells()[i].terrain_height(),targets[i],weights[i]));
    // Preserve the exact original-cell protection promise, including hidden dry planes.
    for(std::size_t i=0;i<count;++i)if(r.weights[i]<=0)f.cells()[i]=source.cells()[i];
    assess_generated_ground(r);
}
} // namespace thandor
