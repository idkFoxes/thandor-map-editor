#include THANDOR_NATIVE_PLACEMENT
#include THANDOR_NATIVE_WORKSPACES
#include THANDOR_NATIVE_RANDOM
#include THANDOR_NATIVE_PLANNING
#include THANDOR_NATIVE_VECTOR
#include "thandor/retail_special_site.hpp"
#include "thandor/retail_random.hpp"
#include "thandor/pioneer_site_reservation.hpp"
#include "thandor/retail_pressure_building.hpp"
#include "thandor/retail_integer_sqrt.hpp"
#include <iostream>
#include <random>
#include <vector>
#include <tuple>
GameFactionRuntimeImage g_GameFactionRuntimeImage{};
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void Thandor_Log(const char*,...){}
static AiKnowledgeDataImage knowledge{};
static AiTerrainFeatureWorkspaceEntry features[8]{};
static FieldGridCell cells[8]{},anchors[4]{};
static FieldGridCell* anchor_ptrs[4]{};
static AiStructureWorkspaceEntry structures[128]{};
static AiRuntimeWorkspaceEntry units[1]{},visible[1]{},unseen[1]{};
static ModelRuntimeSlot models[3]{};
static ModelRuntimeNode nodes[2]{};
static AiCandidateWorkspaceEntry entries[128]{};
static ArmyRuntimeSlot probes[4]{};
static bool enabled{},mode3[8]{},used[4]{};
static std::array<bool,3> building_enabled{};
static uint32_t clearance[8]{},offsets[8][4]{},create_limit{},created{},destroyed{},live{},current{};
static std::vector<size_t> tokens;
using Query=std::tuple<int32_t,int32_t,uint32_t,int32_t,int32_t,uint32_t>;
static std::vector<Query> queries;
void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}
bool ArmyAssetRegistry_FindEnabledById(PckArmyAssetIdCatalog id){if(id>=321&&id<=323)return !building_enabled[size_t(id-321)];require(id==50,"unexpected enabled lookup");return !enabled;}
static void select_site(uint32_t i){
    current=i;
    for(size_t a=0;a<4;++a){anchors[a].worldX=cells[i].worldX+int32_t(offsets[i][a]);anchors[a].worldY=cells[i].worldY;used[a]=false;}
    tokens.clear();live=0;
}
bool ArmyPlacement_CanPlaceAssetAtFieldPoint(ArmyPlacementMode mode,ArmyPlacementClearancePaddingQ12 padding,
    uint32_t heading,Q12 y,Q12 x,PckArmyAssetIdCatalog id,FactionRuntimeIndex,
    WorldRuntimeContext*,uint32_t* value){
    queries.emplace_back(mode,padding,heading,y,x,id);
    if(mode==3){const uint32_t i=uint32_t(x/100000);select_site(i);if(value)*value=clearance[i];return mode3[i];}
    require(mode==1&&id==333,"unexpected placement query");
    for(size_t a=0;a<4;++a)if(anchors[a].worldX==x&&anchors[a].worldY==y)return !used[a];
    return false;
}
ArmyRuntimeSlot* ArmyRuntime_CreateInstanceFromAsset(WorldObjectAllocationFlags,AngleTurn32,Q12 y,Q12 x,
    FactionRuntimeIndex,PckArmyAssetIdCatalog id,WorldRuntimeContext*,uint32_t*){
    require(id==333,"unexpected probe asset");
    if(live>=create_limit)return nullptr;
    for(size_t a=0;a<4;++a)if(anchors[a].worldX==x&&anchors[a].worldY==y){used[a]=true;tokens.push_back(a);++live;++created;return &probes[a];}
    throw std::runtime_error("probe has no anchor");
}
void ArmyRuntime_DestroyInstanceAndRefreshUi(WorldRuntimeContext*,GameEntityRuntime* p){
    const auto index=size_t(reinterpret_cast<ArmyRuntimeSlot*>(p)-probes);
    require(!tokens.empty()&&tokens.back()==index,"native probe cleanup not LIFO");
    tokens.pop_back();used[index]=false;--live;++destroyed;
}
int main() try {
    static_assert(offsetof(AiKnowledgeParameters,specialSiteMinimumWorkspaceDistanceQ12)==89*4);
    g_AiKnowledgeData=&knowledge;g_AiWorkspace08TerrainFeatureSites=features;
    g_AiWorkspace00Structures=structures;g_AiWorkspace01Units=units;
    g_AiWorkspace02VisibleHostiles=visible;g_AiWorkspace03UnseenHostiles=unseen;
    g_AiWorkspace09Cells=anchor_ptrs;g_AiWorkspace13Candidates=entries;
    for(size_t i=0;i<4;++i)anchor_ptrs[i]=&anchors[i];
    for(size_t i=0;i<2;++i){models[i].rootModelNodeOrSavedOffset.modelNode=&nodes[i];}
    visible[0].modelRuntime=&models[0];unseen[0].modelRuntime=&models[1];
    WorldRuntimeContext world{};std::mt19937 rng(0x651);
    uint32_t selected=0,small_chain=0,large_cluster=0,tech=0,pioneer=0;
    for(uint32_t n=0;n<10000;++n){
        g_AiWorkspace00Count=0;g_AiWorkspace01Count=(rng()%5)==0;
        units[0].armyAssetId=50;const bool factory=(rng()&1)!=0;enabled=(rng()&1)!=0;
        if(factory)structures[g_AiWorkspace00Count++]={nullptr,301};
        const uint32_t count330=rng()%5,count332=rng()%5;
        for(uint32_t i=0;i<count330;++i)structures[g_AiWorkspace00Count++]={&models[2],330};
        for(uint32_t i=0;i<count332;++i)structures[g_AiWorkspace00Count++]={&models[2],332};
        g_AiWorkspace08Count=rng()%7;g_AiWorkspace09Count=rng()%5;
        g_AiWorkspace02Count=rng()&1;g_AiWorkspace03Count=rng()&1;
        knowledge.parameters.specialSiteSeparationQuantumQ12=1U+rng()%10000;
        knowledge.parameters.placementClearancePaddingQ12=60000;
        knowledge.parameters.specialSiteMinimumWorkspaceDistanceQ12=rng()%200000;
        knowledge.parameters.specialSite14aBaseWeight=(n%4==0)?uint32_t(rng()):uint32_t(rng()%20000);
        knowledge.parameters.specialSite14cBaseWeight=(n%4==0)?uint32_t(rng()):uint32_t(rng()%20000);
        auto& f=g_GameFactionRuntimeImage.records[1];
        f.technologyMasks256Bits[0]=(rng()&1)?1U<<11:0;
        f.tritiumCurrentQ4=(n%3==0)?0:uint32_t(rng());f.suppliedEnergyDemandQ4=rng();f.unpoweredEnergyDemandQ4=rng();
        nodes[0].worldTransform.translation={int32_t(rng()%600000),int32_t(rng()%10000),0};
        nodes[1].worldTransform.translation={int32_t(rng()%600000),int32_t(rng()%10000),0};
        create_limit=rng()%5;
        struct Site{uint32_t army_asset_id;};std::vector<Site> sites;
        for(uint32_t i=0;i<g_AiWorkspace08Count;++i){
            cells[i].worldX=int32_t(i*100000);cells[i].worldY=int32_t(i*500);
            cells[i].triangle0NormalAngles=uint32_t(rng());
            features[i].cell=&cells[i];features[i].armyAssetId=(rng()&1)?330:332;
            sites.push_back({uint32_t(features[i].armyAssetId)});mode3[i]=(rng()%4)!=0;
            clearance[i]=rng()%(knowledge.parameters.specialSiteSeparationQuantumQ12*7U);
            for(auto& offset:offsets[i])offset=rng()%50000;
        }
        const uint32_t seed=rng();Random_SetBothSeeds(seed);Random_SelectPrimaryStream();
        g_AiCandidateWorkspaceEntryCount=0;queries.clear();created=destroyed=0;
        AiCandidatePlanning_AddSpecialSiteCandidate(1,&world);
        const auto native_queries=queries;const auto native_created=created;
        const auto native_rng=g_RandomGeneratorState.primarySeed;const auto native_count=g_AiCandidateWorkspaceEntryCount;
        const auto native_entry=entries[0];require(created==destroyed&&live==0,"native probe leak");
        queries.clear();created=destroyed=0;
        const auto c=thandor::retail_special_site_candidate(g_AiWorkspace01Count!=0,factory,enabled,
            (f.technologyMasks256Bits[0]&(1U<<11))!=0,sites,
            knowledge.parameters.specialSite14aBaseWeight,knowledge.parameters.specialSite14cBaseWeight,
            knowledge.parameters.specialSiteMinimumWorkspaceDistanceQ12,f.tritiumCurrentQ4,
            f.suppliedEnergyDemandQ4,f.unpoweredEnergyDemandQ4,
            [&](size_t i){
                queries.emplace_back(3,60000,uint16_t(cells[i].triangle0NormalAngles),cells[i].worldY,cells[i].worldX,sites[i].army_asset_id);
                select_site(uint32_t(i));
                return thandor::retail_special_site_cluster_rejected(mode3[i],clearance[i],
                    knowledge.parameters.specialSiteSeparationQuantumQ12,[&]{
                        ++small_chain;
                        std::vector<thandor::MovementPoint> points;
                        for(uint32_t a=0;a<g_AiWorkspace09Count;++a)points.push_back({anchors[a].worldX,anchors[a].worldY});
                        return thandor::pioneer_reserve_separated_site_chain<size_t>(
                            {cells[i].worldX,cells[i].worldY},knowledge.parameters.specialSiteSeparationQuantumQ12,
                            [&](auto p){return thandor::pioneer_nearest_mode1_anchor(p,points,[&](auto q){return ArmyPlacement_CanPlaceAssetAtFieldPoint(1,0,0,q.y,q.x,333,1,&world,nullptr);});},
                            [&](auto p)->std::optional<size_t>{auto* token=ArmyRuntime_CreateInstanceFromAsset(1,0,p.y,p.x,1,333,&world,nullptr);if(!token)return {};return size_t(token-probes);},
                            [&](size_t token){ArmyRuntime_DestroyInstanceAndRefreshUi(&world,reinterpret_cast<GameEntityRuntime*>(&probes[token]));});
                    });
            },[&](size_t i,uint32_t ws){return ws==2?AiHostileWorkspace_GetNearestVisibleHostileDistance(cells[i].worldY,cells[i].worldX):AiHostileWorkspace_GetNearestUnseenHostileDistance(cells[i].worldY,cells[i].worldX);},
            [&](uint32_t id){return id==330?count330:count332;});
        uint32_t state=seed;uint32_t score=0;bool admitted=false;
        if(c){++selected;if(c->id==11)++tech;if(c->id==50)++pioneer;
            const auto random=thandor::retail_random_next(state);
            if(c->weight>1){score=((c->weight*5U+random%c->weight)<<4U)|c->kind;admitted=true;}}
        // Mode1 heading comes from each anchor cell; fixtures leave that angle zero.
        if(queries!=native_queries||created!=native_created||created!=destroyed||live!=0||
           state!=native_rng||native_count!=uint32_t(admitted)||
           (admitted&&(native_entry.weightedScoreAndKind!=score||(native_entry.entityIdAndMultiplicity&0xFFFFU)!=c->id))){
            std::cerr<<"case="<<n<<" native_count="<<native_count<<" selected="<<bool(c)<<" native_queries="<<native_queries.size()<<" queries="<<queries.size()<<'\n';
            throw std::runtime_error("native special-site policy/query/RNG mismatch");
        }
        for(uint32_t i=0;i<g_AiWorkspace08Count;++i)large_cluster+=mode3[i]&&((clearance[i]-1U+knowledge.parameters.specialSiteSeparationQuantumQ12)/knowledge.parameters.specialSiteSeparationQuantumQ12>=5);
    }
    std::cout<<"V651_NATIVE_SPECIAL_SITE PASS cases=10000 selected="<<selected<<" TEC11="<<tech<<" ARM50="<<pioneer<<" small_chain_calls="<<small_chain<<" large_input_clusters="<<large_cluster<<"; original placement/workspace/random bodies, controlled placement and probe creation callbacks\n";
    for(uint32_t n=0;n<10000;++n){
        const uint64_t value=n<2000?n:(uint64_t(rng())<<32)|rng();
        require(thandor::retail_integer_sqrt(value)==FixedMath_UInt64Sqrt(uint32_t(value>>32),uint32_t(value)),"native three-step square root differs");
        std::array<int32_t,3> pressure{};
        std::array<std::array<uint32_t,3>,3> coefficients{};
        std::array<bool,3> present{};
        g_AiWorkspace00Count=0;
        for(size_t i=0;i<3;++i){
            pressure[i]=std::bit_cast<int32_t>(uint32_t(rng()));
            g_GameFactionRuntimeImage.records[1].aiPressureValues[2+i]=pressure[i];
            present[i]=(rng()&1)!=0;building_enabled[i]=(rng()&1)!=0;
            if(present[i])structures[g_AiWorkspace00Count++]={nullptr,int32_t(321+i)};
            for(size_t j=0;j<3;++j){coefficients[i][j]=rng();reinterpret_cast<uint32_t*>(&knowledge.parameters)[0x74+i*4+j]=coefficients[i][j];}
        }
        if(uint32_t(pressure[0])+uint32_t(pressure[1])+uint32_t(pressure[2])+1U==0U)continue;
        const uint32_t seed=rng();Random_SetBothSeeds(seed);Random_SelectPrimaryStream();
        const auto native=AiStrategicClass_SelectPressureWeightedBuilding(1,&world);
        uint32_t state=seed;const auto port=thandor::retail_pressure_building(pressure,coefficients,
            thandor::retail_random_next(state),present,building_enabled);
        require(native.selectedRuntimeToken==port.id&&native.existingCountOrPressure==port.existing&&
            state==g_RandomGeneratorState.primarySeed,"native pressure-building wrapping/enable gate differs");
    }
    std::cout<<"V651_NATIVE_PRESSURE_AND_SQRT PASS pressure_cases=10000 sqrt_cases=10000 signed_pressure=1 unsigned_wrap=1 disabled_assets=1\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
