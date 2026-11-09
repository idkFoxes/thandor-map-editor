#include "generated_shadow_reuse.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
int main()try {
    editor::GeneratedShadowReuse cache;
    auto field=thandor::FieldGrid::create(59U,59U);
    std::array<thandor::GeneratedShadowTriangle,1> triangles{{
        {{{{1.0F,2.0F,3.0F},{2.0F,2.0F,3.0F},{1.0F,3.0F,3.0F}}},false}
    }};
    const thandor::Vec3f light{0.2F,0.3F,-1.0F};
    thandor::GeneratedShadowSettings settings;
    int builds=0;
    const auto build=[&]()->std::optional<thandor::GeneratedObjectShadow>{
        auto result=thandor::GeneratedObjectShadow{};
        result.mask={32,32,std::vector<uint8_t>(32U*32U*4U,static_cast<uint8_t>(++builds))};
        result.receiver_indices={0,1,2};
        result.receiver_vertices.resize(3);
        return result;
    };
    cache.begin(field);
    check(cache.obtain(42,triangles,light,settings,build)->mask.pixels[0]==1,"initial shadow missing");
    check(cache.obtain(42,triangles,light,settings,build)->mask.pixels[0]==1&&builds==1,"stable result recomputed");
    check(cache.hits()==1&&cache.misses()==1&&cache.entries()==1,"cache counters incorrect");
    // Another author-owned placement requires an independent entry.
    check(cache.obtain(43,triangles,light,settings,build)->mask.pixels[0]==2,"placement ownership incorrectly aliased");
    // Each transformation must invalidate its own cached result.
    triangles[0].world[0].x+=1.0F;
    check(cache.obtain(42,triangles,light,settings,build)->mask.pixels[0]==3,"triangle modification hidden");
    triangles[0].blurred_group=true;
    check(cache.obtain(42,triangles,light,settings,build)->mask.pixels[0]==4,"soft/hard caster mode hidden");
    auto next_light=light;next_light.z-=0.1F;
    check(cache.obtain(42,triangles,next_light,settings,build)->mask.pixels[0]==5,"lighting change hidden");
    settings.surface_bias=0.25F;
    check(cache.obtain(42,triangles,next_light,settings,build)->mask.pixels[0]==6,"shadow settings change hidden");
    field.at(1,2).terrain_height_q12+=4096;
    cache.begin(field);
    check(cache.entries()==0,"terrain changes retained stale shadows");
    check(cache.obtain(42,triangles,next_light,settings,build)->mask.pixels[0]==7,"terrain changes reused stale shadows");
    field.at(1,2).flags_and_material^=0x200U;
    cache.begin(field);
    check(cache.entries()==0,"cell flag changes retained stale shadows");
    check(cache.obtain(42,triangles,next_light,settings,build)->mask.pixels[0]==8,"cell flag changes reused stale shadows");
    const auto bad=[&]()->std::optional<thandor::GeneratedObjectShadow>{throw std::runtime_error("error constructing shadow");};
    triangles[0].world[0].y+=1;
    bool failed=false;
    try{(void)cache.obtain(42,triangles,next_light,settings,bad);}catch(const std::runtime_error&){failed=true;}
    check(failed,"builder exception suppressed");
    triangles[0].world[0].y-=1;
    check(cache.obtain(42,triangles,next_light,settings,build)->mask.pixels[0]==8,
          "failed shadow generation corrupted previous success");
    // An empty geometry result is also reusable, for maps with no receiver contact.
    auto null_builder=[&]()->std::optional<thandor::GeneratedObjectShadow>{++builds;return std::nullopt;};
    check(!cache.obtain(99,triangles,next_light,settings,null_builder),"null caster result unexpected");
    check(!cache.obtain(99,triangles,next_light,settings,null_builder),"null result not cached");
    check(builds==9,"null shadow was rebuilt");
    field.at(4,5).water_delta_q12-=100;
    cache.begin(field);
    check(cache.entries()==0,"water delta changes retained stale shadows");
    // The real stock contact/rasterizer path must produce byte-identical cached
    // masks and receiver packets, not merely matching mocked build counters.
    auto real_field=thandor::FieldGrid::create(59,59,0,0);
    const auto& cell=real_field.at(29,29);
    const float x=thandor::q12_to_float(cell.world_x_q12);
    const float y=thandor::q12_to_float(cell.world_y_q12);
    const thandor::Vec3f p0{x-1.3F,y-.4F,0.15F},p1{x+1.3F,y+.4F,0.15F};
    const thandor::Vec3f p2{x+1.1F,y+.5F,3.2F},p3{x-1.2F,y-.3F,3.2F};
    std::array<thandor::GeneratedShadowTriangle,2> real_triangles{{
        {{p0,p1,p2},false},{{p0,p2,p3},true}
    }};
    const thandor::Vec3f real_direction{0.32F,0.12F,-0.94F};
    editor::GeneratedShadowReuse real_cache;
    real_cache.begin(real_field);
    int real_builds=0;
    auto real_builder=[&](){++real_builds;return thandor::build_game_generated_object_shadow(
        real_field,real_triangles,real_direction,thandor::GeneratedShadowSettings{});};
    const auto expected=real_builder();
    check(expected.has_value(),"real shadow fixture has no terrain contact");
    const auto& first=real_cache.obtain(7,real_triangles,real_direction,{},real_builder);
    const auto& second=real_cache.obtain(7,real_triangles,real_direction,{},real_builder);
    check(&first==&second&&real_builds==2,"real shadow cache did not hit");
    check(first->mask.pixels==expected->mask.pixels&&
          first->receiver_indices==expected->receiver_indices&&
          first->receiver_vertices.size()==expected->receiver_vertices.size(),
          "cached output differs from rasterizer/receiver oracle");
    for(std::size_t i=0;i<expected->receiver_vertices.size();++i){
        const auto& a=first->receiver_vertices[i];
        const auto& b=expected->receiver_vertices[i];
        check(a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
              a.u==b.u&&a.v==b.v&&a.distance_attenuation==b.distance_attenuation&&
              a.contact_path==b.contact_path,"cached shadow receiver changed");
    }
    // Mutating terrain invalidates the cache and reruns the retail algorithm.
    real_field.at(29,29).terrain_height_q12+=2048;
    real_cache.begin(real_field);
    check(real_cache.entries()==0,"real changed-field cache was not cleared");
    (void)real_cache.obtain(7,real_triangles,real_direction,{},real_builder);
    check(real_builds==3,"real field modification did not regenerate shadow");
    std::cout<<"RENDERING_GENERATED_SHADOW_REUSE_PASS\n";
}catch(const std::exception& e){std::cerr<<"RENDERING_GENERATED_SHADOW_REUSE_FAIL: "<<e.what()<<'\n';return 1;}
