#include THANDOR_NATIVE_AUDIO
#include THANDOR_NATIVE_FACTION_RUNTIME
#include THANDOR_NATIVE_PLANNING
#include "aviation_airport_alert_fixture.hpp"
#include "thandor/simulation_lab_ai_grid_classes.hpp"
#include <iostream>
#include <random>

GameFactionRuntimeImage g_GameFactionRuntimeImage{};
AudioMixerGainQ15 g_SoundEffectsGainQ15=0x5432;
static uint32_t calls{};
static bool play(uint32_t left,uint32_t right,SoundVoiceSet*,SoundVoice** out){
    v650::check(left==0x5432&&right==0x5432&&!out,"native alert used positioned/stereo gains");
    ++calls;return true;
}
SoundPlayVoiceProc* g_SoundPlayOneShot=play;
void Thandor_Ptr32Overflow(uintptr_t){std::abort();}
void Thandor_Log(const char*,...){}
alignas(FieldGridAsset) static unsigned char field_bytes[sizeof(FieldGridAsset)+32*32*sizeof(FieldGridCell)];
static uintptr_t slots[16]{};
static SoundVoiceSet* voice{};
static ModelRuntimeSlot class_models[64]{};
static ModelDefinition class_definitions[64]{};
static AiRuntimeWorkspaceEntry class_workspace[64]{};
AiRuntimeWorkspaceEntry* g_AiWorkspace01Units=class_workspace;
uint32_t g_AiWorkspace01Count{};

int main() try {
    const auto field=thandor::FieldGrid::create(32,32,0,0);
    auto* native=reinterpret_cast<FieldGridAsset*>(field_bytes);
    native->gridWidth=32;native->gridHeight=32;
    WorldRuntimeContext world{};world.fieldGrid=native;
    slots[9]=reinterpret_cast<uintptr_t>(&voice);
    std::mt19937 rng(0x650);
    for(uint32_t n=0;n<5000;++n){
        v650::AlertCase c;
        c.source=rng()%8;c.listener=rng()%8;c.batch=1+rng()%5;c.timer=rng()%26;
        c.friendly=rng()&1;c.reverse_friendly=rng()&1;c.occupancy=uint8_t(rng());
        c.sound_valid=(rng()%4)!=0;c.countdown=int32_t(rng()%9)-3;
        const auto& cell=field.at(rng()%32,rng()%32);c.x=cell.world_x_q12;c.y=cell.world_y_q12;
        if(n%7==0)c.x=-100000000;
        for(uint32_t i=0;i<32*32;++i)native->cells[i].occupancyMask=uint64_t(c.occupancy)<<(c.listener*8);
        world.activeFactionRuntimeIndex=c.listener;
        world.dwordArray=c.sound_valid?slots:nullptr;world.dwordArrayCount=16;
        auto& source=g_GameFactionRuntimeImage.records[c.source];
        source.capabilityFlags=c.friendly?1U<<c.listener:0U;
        // Keep reverse direction distinct to catch swapped argument order.
        if(c.source!=c.listener)g_GameFactionRuntimeImage.records[c.listener].capabilityFlags=c.reverse_friendly?1U<<c.source:0U;
        g_GameFactionRuntimeImage.records[c.listener].relationTransitionTick=c.timer;
        calls=0;auto count=c.countdown;
        for(uint32_t step=0;step<c.batch;++step){
            --count;
            ArmyRuntime_TryPlayMappedTerrainSoundAtWorldPoint(c.source,
                count<0?256*4096:c.y,count<0?-256*4096:c.x,9,&world);
        }
        auto lab=v650::alert_lab(field,c);lab.step(field,1);
        const auto events=v650::alerts(lab.runs()[0]);
        if(events.size()!=calls||lab.runs()[0].faction_relation_transition_ticks[c.listener]!=
              g_GameFactionRuntimeImage.records[c.listener].relationTransitionTick){
            std::cerr<<"case="<<n<<" source="<<c.source<<" listener="<<c.listener<<" batch="<<c.batch<<" timer="<<c.timer<<" countdown="<<c.countdown<<" native="<<calls<<" lab="<<events.size()<<'\n';
            throw std::runtime_error("original mapped-terrain audio callback differs from Lab");
        }
    }
    std::cout<<"V650_NATIVE_AIRPORT_ALERT PASS cases=5000 original_audio_body=1 original_capability_body=1 batches=1..5 asymmetric_relations=1\n";
    for(uint32_t n=0;n<5000;++n){
        thandor::SimulationLabRun run;run.level_runtime_enabled=true;
        const uint32_t count=uint32_t(rng()%86);
        for(uint32_t i=0;i<count;++i){
            thandor::SimulationLabEntity unit;unit.army_asset_id=2;unit.faction_index=1;
            unit.has_traversal_profile=true;unit.motion.speed_q12_per_tick=(rng()%3)?128:0;
            unit.traversal_low_band_index=rng()%8;unit.traversal_high_band_index=rng()%7;
            run.entities.push_back(unit);
        }
        g_AiWorkspace01Count=std::min(count,64U);
        for(uint32_t i=0;i<g_AiWorkspace01Count;++i){
            const auto& unit=run.entities[count-1-i];
            class_models[i]={};class_definitions[i]={};
            class_definitions[i].accelerationPerTick=unit.motion.movable()?1:0;
            class_definitions[i].footprintRadiusClass=unit.traversal_low_band_index;
            class_definitions[i].terrainTraversalClass=unit.traversal_high_band_index;
            class_models[i].definitionOrSavedId.runtimeDefinition=&class_definitions[i];
            class_workspace[i].modelRuntime=&class_models[i];
        }
        AiPlanning_CollectActiveGridMaskClasses();
        const auto masks=thandor::simulation_lab_ai_active_grid_classes(run,1);
        for(size_t i=0;i<4;++i)v650::check(masks[i]==g_AiActiveGridMaskClasses[i],"native active movement class order/capacity differs");
    }
    std::cout<<"V650_NATIVE_AI_GRID_CLASSES PASS cases=5000 roots=0..85 workspace_capacity=64 masks=4 newest_first=1\n";
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
