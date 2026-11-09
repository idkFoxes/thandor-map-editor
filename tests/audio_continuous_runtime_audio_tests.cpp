#include "thandor/catalog.hpp"
#include "thandor/level.hpp"
#include "thandor/simulation.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);std::ostringstream text;text<<stream.rdbuf();return text.str();
}
std::array<uint32_t,2> select(uint32_t cls,uint32_t d0,uint32_t d8,uint32_t primary,uint32_t secondary,
                              bool enabled,bool translated,bool turned,bool secondary_state=false){
    return thandor::simulation_runtime_looping_sound_indices(
        {cls,d0,d8,primary,secondary},{enabled,translated,turned,secondary_state});
}
bool contains(std::array<uint32_t,2> values,uint32_t value){return values[0]==value||values[1]==value;}
}

int main(int argc,char** argv) try {
    // Movement callbacks. D0 is the persistent movement channel for the stock class-1/2/17/18/19
    // roots; the dual callback's D8 channel is only asserted by its turn-state branch.
    for(uint32_t cls:{1U,2U,17U,18U,19U}){
        require(select(cls,39,51,0,0,true,false,false)==std::array<uint32_t,2>{},
                "stopped dual-movement class requested a loop");
        const auto moving=select(cls,39,51,0,0,true,true,false);
        require(moving[0]==39U&&moving[1]==0U,"moving dual-movement class missed D0");
        const auto turning=select(cls,39,51,0,0,true,false,true);
        require(contains(turning,39U)&&contains(turning,51U),"turning dual-movement class missed D0/D8");
    }
    for(uint32_t cls:{5U,6U,7U,8U}){
        // ArmyRuntimeAudio_UpdateTurretTurnSound tests axis turn velocities,
        // not translation of the owning vehicle.
        require(select(cls,39,51,0,0,true,false,true)[0]==51U,
                "turning turret missed D8 projected loop");
        require(select(cls,39,51,0,0,true,true,false)==std::array<uint32_t,2>{},
                "root translation triggered a stationary turret motor");
        require(select(cls,39,51,0,0,true,false,false)==std::array<uint32_t,2>{},
                "idle D8 movement class requested a loop");
    }

    // Active/ambient owners use +1AC. Classes 13/22 have a separate +274 state branch which must
    // remain silent until that exact gameplay state is represented by the editor.
    for(uint32_t cls:{4U,10U,11U,14U,16U,21U})
        require(select(cls,0,0,27,45,true,false,false)[0]==27U,
                "active primary projected-sound class missed +1AC");
    for(uint32_t cls:{13U,22U}){
        const auto primary=select(cls,0,0,27,45,true,false,false,false);
        require(primary[0]==27U&&primary[1]==0U,"stateful class invented secondary loop");
        const auto secondary=select(cls,0,0,27,45,true,false,false,true);
        require(contains(secondary,27U)&&contains(secondary,45U),"explicit secondary state missed +274");
    }
    require(select(23,0,0,43,0,true,false,false)==std::array<uint32_t,2>{},
            "class-23 binary-proven no-op played serialized +1AC");
    require(select(3,0,0,48,0,true,true,true)==std::array<uint32_t,2>{},
            "class-3 contact one-shot class incorrectly requested a persistent loop");
    require(select(11,0,0,27,0,false,false,false)==std::array<uint32_t,2>{},
            "disabled runtime requested ambient sound");

    // Source integration contract: persistent loops must be reconciled from callback-selected IDs,
    // stopped when sound/simulation is disabled, and must not replace the class-3 one-shot path.
    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    const auto audio=read_text(source/"src/core/audio.cpp");
    const auto model=read_text(source/"src/core/model.cpp");
    require(model.find("projected_sound_asset_id_d0 = read_u32(data, offset + 0xD0U)")!=std::string::npos&&
            model.find("projected_sound_asset_id_d8 = read_u32(data, offset + 0xD8U)")!=std::string::npos,
            "MDL D0/D8 projected sound parser missing");
    require(editor.find("active_looping_sounds_")!=std::string::npos&&
            editor.find("reconcile_runtime_looping_sounds")!=std::string::npos&&
            editor.find("simulation_runtime_looping_sound_indices")!=std::string::npos,
            "editor persistent runtime sound reconciliation missing");
    require(editor.find("document_->level().paths().sounds")!=std::string::npos&&
            editor.find("individual SAMs are not LEV resource_paths entries")!=std::string::npos,
            "editor sound-table resolver does not use the LEV soundBasePath wildcard");
    require(editor.find("if(!sound_playback_enabled_)stop_all_runtime_looping_sounds()")!=std::string::npos&&
            editor.find("Building/unit animation preview is a live-runtime visualization")!=std::string::npos,
            "sound toggle stop or non-command ambient preview integration missing");
    require(editor.find("play_runtime_spatial_sound(preset->articulated_contact_sound_asset_id,actor.position")!=std::string::npos,
            "class-3 completed-contact positioned one-shot path was lost");
    require(audio.find("LoopingPcmVoice::set_gains")!=std::string::npos&&
            audio.find("std::atomic<uint32_t> left_gain_q15")!=std::string::npos&&
            audio.find("snd_pcm_drop")!=std::string::npos,
            "host loop lifetime/dynamic stereo gain backend missing");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t stock_levels=0U;
        for(const auto& group:catalog.map_groups()){
            const auto level=thandor::LevelAsset::parse(catalog.load(group.level_path));
            require(level.paths().sounds=="sound/sound??",
                    "stock LEV soundBasePath wildcard drifted: "+group.level_path);
            ++stock_levels;
        }
        require(stock_levels==56U,"stock level sound-template census changed");
        for(uint32_t sound_index:{23U,24U,27U,28U,29U,36U,37U,38U,39U,40U,46U,47U,48U,49U,52U,53U,54U}){
            const std::string path=std::string("sound/sound")+(sound_index<10U?"0":"")+
                                   std::to_string(sound_index)+".sam";
            const auto* record=catalog.find(path);
            require(record&&record->kind==thandor::AssetKind::Sound,
                    "stock runtime SAM missing: "+path);
        }
        auto exact=[&](uint32_t id,uint32_t cls,uint32_t d0,uint32_t primary){
            const auto* preset=catalog.army_preset(id);
            require(preset!=nullptr,"missing stock ARM "+std::to_string(id));
            require(preset->runtime_model_class_id==cls,"stock runtime class drift ARM "+std::to_string(id));
            require(preset->projected_sound_asset_id_d0==d0,"stock D0 sound drift ARM "+std::to_string(id));
            require(preset->projected_sound_asset_id_primary==primary,"stock +1AC sound drift ARM "+std::to_string(id));
            return preset;
        };
        exact(1,1,39,0);       // Weasel
        exact(60,2,54,0);      // Badger
        exact(80,2,37,0);      // Panther
        exact(100,2,53,0);     // Goliath
        exact(170,17,38,0);    // Mosquito
        exact(180,17,52,0);    // Dragonfly
        exact(220,17,49,0);    // deployed Hornet family
        exact(250,19,40,0);    // Dolphin
        exact(295,18,40,0);    // Typhoon variant
        exact(240,21,0,47);    // Crow persistent asset channel
        exact(242,21,0,46);    // Hawk

        exact(300,11,0,27);    // Headquarters
        exact(301,13,0,27);    // Light weapons factory
        exact(302,13,0,27);    // Armour factory
        exact(303,13,0,27);    // Walker factory
        exact(304,13,0,27);    // Glider factory
        exact(305,13,0,28);    // Harbour
        exact(306,22,0,27);    // Airport
        exact(310,16,0,29);    // Power plant
        for(uint32_t id=320;id<=323;++id)exact(id,4,0,48); // research labs
        exact(330,14,0,24);    // Xenite mine
        exact(332,14,0,23);    // Tritium pump
        for(uint32_t id=380;id<=382;++id){
            const auto* wall=exact(id,23,0,43);
            require(select(wall->runtime_model_class_id,wall->projected_sound_asset_id_d0,
                           wall->projected_sound_asset_id_d8,wall->projected_sound_asset_id_primary,
                           wall->projected_sound_asset_id_secondary,true,false,false)==
                    std::array<uint32_t,2>{},"stock class-23 wall escaped no-op selector");
        }
        std::cout<<" stock_audio_census=PASS";
    }

    std::cout<<" V451_CONTINUOUS_RUNTIME_AUDIO PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 continuous runtime audio checks failed: "<<error.what()<<'\n';
    return 1;
}
