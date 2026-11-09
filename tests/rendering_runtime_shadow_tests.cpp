#include "runtime_shadow.hpp"
#include "thandor/catalog.hpp"
#include <iostream>
#include <map>

using namespace editor;
using namespace thandor;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
template<class Fn> void rejects(Fn fn){bool threw=false;try{fn();}catch(const std::exception&){threw=true;}
    require(threw,"malformed dedicated shadow mesh was accepted");}
}
int main(int argc,char** argv) try {
    SpriteMesh mesh;
    mesh.vertices.resize(6);
    mesh.vertices[0].position={1,0,0};mesh.vertices[1].position={0,1,0};mesh.vertices[2].position={0,0,1};
    mesh.vertices[3].position={2,0,0};mesh.vertices[4].position={0,2,0};mesh.vertices[5].position={0,0,2};
    for(size_t i=0;i<3;i++)mesh.vertices[i].mesh_record_flags=1;
    mesh.indices={2,0,1,3,4,5};
    ModelHierarchyTransform transform;transform.translation={10,20,30};
    transform.basis={0,-1,0,1,0,0,0,0,1};
    std::vector<GeneratedShadowTriangle> triangles;
    append_runtime_shadow_triangles(mesh,transform,triangles);
    require(triangles.size()==2&&triangles[0].blurred_group&&!triangles[1].blurred_group,
        "dedicated soft/hard groups or record order changed");
    const auto& a=triangles[0].world;
    require(a[0].x==10&&a[0].y==20&&a[0].z==31&&a[1].x==10&&a[1].y==21&&
        a[1].z==30&&a[2].x==9&&a[2].y==20&&a[2].z==30,"posed caster transform/corner order changed");
    append_runtime_shadow_triangles({},transform,triangles);
    require(triangles.size()==2,"an absent dedicated table invented a shadow");
    auto malformed=mesh;malformed.indices.pop_back();
    rejects([&]{append_runtime_shadow_triangles(malformed,transform,triangles);});
    malformed=mesh;malformed.indices[0]=100;
    rejects([&]{append_runtime_shadow_triangles(malformed,transform,triangles);});
    malformed=mesh;malformed.vertices[1].mesh_record_flags=0;
    rejects([&]{append_runtime_shadow_triangles(malformed,transform,triangles);});
    const RuntimeShadowOwner unit{RuntimeShadowSource::LabEntity,8,0};
    require(unit!=RuntimeShadowOwner{RuntimeShadowSource::LabEntity,9,0}&&
        unit!=RuntimeShadowOwner{RuntimeShadowSource::LabEntity,8,1}&&
        unit!=RuntimeShadowOwner{RuntimeShadowSource::Produced,8,0},
        "runtime shadow ownership aliases another object/run/source");
    if(argc>1){
        const auto catalog=AssetCatalog::scan_archive_root(argv[1]);
        std::map<std::string,SpriteAsset> sprites;
        size_t units=0,casters=0,missing=0,nodes=0,triangles_count=0,soft=0,hard=0;
        for(const auto& preset:catalog.army_presets()){
            if(preset.browser_kind!=PlacementBrowserKind::Unit)continue;
            ++units;if(!preset.casts_generated_shadow)continue;++casters;
            std::vector<GeneratedShadowTriangle> posed;
            const auto& hierarchy=preset.resolved_model_hierarchy.empty()?preset.model_hierarchy:preset.resolved_model_hierarchy;
            for(const auto& node:hierarchy){
                if(!node.creates_runtime_node()||node.sprite_path.empty())continue;
                auto found=sprites.find(node.sprite_path);
                if(found==sprites.end())found=sprites.emplace(node.sprite_path,SpriteAsset::parse(catalog.load(node.sprite_path))).first;
                // Identity pose isolates asset decoding and dedicated-table validation.
                // This is not an articulated-pose or GPU rendering assertion.
                append_runtime_shadow_triangles(found->second.generated_shadow_mesh(),{},posed);++nodes;
            }
            if(posed.empty())++missing;
            triangles_count+=posed.size();
            for(const auto& t:posed)t.blurred_group?++soft:++hard;
        }
        require(units>0&&casters>0&&triangles_count>0,"stock unit shadow census did not inspect any caster geometry");
        std::cout<<"STOCK_UNIT_SHADOW_ASSETS PASS units="<<units<<" caster_presets="<<casters
            <<" caster_presets_without_geometry="<<missing<<" nodes="<<nodes<<" dedicated_triangles="<<triangles_count
            <<" soft="<<soft<<" hard="<<hard<<" articulated_pose_verified=0 gpu_verified=0\n";
    }
    std::cout<<"RUNTIME_SHADOW PASS posed_dedicated_mesh=1 groups=1 malformed_rejected=1 ownership=1\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
