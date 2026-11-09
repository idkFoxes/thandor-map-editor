#include "thandor/level.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
void require(bool ok,const char* message){if(!ok){std::cerr<<"G410 failure: "<<message<<'\n';std::exit(1);}}
void put_u32(std::vector<std::byte>& data,std::size_t offset,uint32_t value){
    for(std::size_t i=0;i<4;++i)data[offset+i]=static_cast<std::byte>(value>>(i*8U));
}
std::vector<std::byte> minimal_level(){
    std::vector<std::byte> data(0x1540U);
    put_u32(data,0x00U,0x0076656CU); put_u32(data,0x04U,static_cast<uint32_t>(data.size()));
    put_u32(data,0xD8U,0U); put_u32(data,0xDCU,0x1540U);
    for(std::size_t i=0;i<10;++i)put_u32(data,0xB0U+i*4U,0x100U+static_cast<uint32_t>(i*0x20U));
    put_u32(data,0x140U,static_cast<uint32_t>('1')); put_u32(data,0x148U,static_cast<uint32_t>('1'));
    put_u32(data,0x310U,1U); put_u32(data,0x314U,1U);
    return data;
}
std::string read_text(const char* path){std::ifstream in(path);return {std::istreambuf_iterator<char>(in),{}};}
}
int main(){
    auto level=thandor::LevelAsset::parse(minimal_level());
    thandor::LevelScheduledCondition condition{};
    condition.status_and_kind=static_cast<uint8_t>(thandor::LevelScheduledConditionKind::XeniteCurrentAtLeast)|1U;
    condition.set_operand(0,2U); condition.set_operand(1,6400U); condition.set_operand(2,0xAABBCCDDU);
    level.set_scheduled_condition(7U,condition);
    const auto read=level.scheduled_condition(7U);
    require(!read.satisfied(),"serialized condition must clear runtime satisfied bit");
    require(read.kind()==thandor::LevelScheduledConditionKind::XeniteCurrentAtLeast,"condition kind drift");
    require(read.operand(0)==2U&&read.operand(1)==6400U&&read.operand(2)==0xAABBCCDDU,"condition operand drift");

    thandor::LevelScheduledCondition expression{};
    expression.status_and_kind=static_cast<uint8_t>(thandor::LevelScheduledConditionKind::BooleanPostfixExpression);
    expression.payload_bytes.fill(0U);
    expression.payload_bytes[0]=1U; expression.payload_bytes[1]=2U; expression.payload_bytes[2]=0xFEU;
    expression.payload_bytes[3]=0xFCU;
    level.set_scheduled_condition(8U,expression);
    require(level.scheduled_condition(8U).payload_bytes[3]==0xFCU,"postfix END token drift");

    thandor::LevelEndConditionTrigger trigger{};
    trigger.state_flags=3U; trigger.movie_variant_selector=1U; trigger.skip_army_disable_when_one=1U;
    trigger.faction_runtime_index=2U; trigger.end_movie_selection_index=4U; trigger.condition_index=8U;
    level.set_end_condition_trigger(0U,trigger);
    const auto trigger_read=level.end_condition_trigger(0U);
    require(trigger_read.active()&&!trigger_read.processed(),"serialized trigger must clear runtime processed bit");
    require(trigger_read.faction_runtime_index==2U&&trigger_read.condition_index==8U&&
            trigger_read.end_movie_selection_index==4U,"trigger field drift");
    require(level.active_scheduled_condition_count()==2U,"active condition count drift");
    require(level.active_end_condition_trigger_count()==1U,"active trigger count drift");

#ifdef THANDOR_SOURCE_DIR
    const std::string root=THANDOR_SOURCE_DIR;
    const auto app=read_text((root+"/src/editor/editor_app.cpp").c_str());
    const auto renderer=read_text((root+"/src/editor/terrain_renderer.cpp").c_str());
    const auto primitive=read_text((root+"/src/editor/primitive_render.hpp").c_str());
    require(app.find("Fog is uploaded as per-cell runtime visibility")!=std::string::npos &&
            app.find("FogOverlayTerrainDepthTest")==std::string::npos,
            "G410 hill-shadow workaround was not superseded by per-cell terrain visibility");
    require(renderer.find("update_fog_visibility")!=std::string::npos &&
            primitive.find("FogOverlayTerrainDepthTest")==std::string::npos,
            "fog must not return as coplanar terrain-depth-tested geometry");
    require(app.find("Campaign / mission script (LEV conditions)")!=std::string::npos,
            "campaign script editor missing");
    require(app.find("Notification MDL %u")!=std::string::npos,
            "model notification ids missing from asset UI");
#endif
    std::cout<<"G410 campaign-script and fog-shadow composition tests PASS\n";
}
