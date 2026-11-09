#include "shadow_receiver_pose_cache.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Vertex { float x{},y{},z{}; uint32_t layer{}; };
}
int main()try{
    editor::ShadowReceiverPoseCache<Vertex> cache;
    const editor::Vec3f first{12.0F,-8.0F,1.0F};
    const editor::Vec3f moved{13.0F,-8.0F,1.0F};
    require(cache.find(first,0)==nullptr,"empty cache falsely hit");
    cache.store(first,65535U,{{1.0F,2.0F,3.0F,15U},{4.0F,5.0F,6.0F,18U}});
    const auto* match=cache.find(first,65535U);
    require(match!=nullptr&&match->size()==2&&(*match)[1].layer==18U,"cached receiver lost geometry");
    require(cache.find(first,0U)==nullptr,"yaw changed without invalidation");
    require(cache.find(moved,65535U)==nullptr,"translation changed without invalidation");
    require(cache.find({12.0F,-8.0F,1.5F},65535U)==nullptr,"height changed without invalidation");
    cache.store(moved,0U,{{0.0F,0.0F,0.0F,4U}});
    require(cache.find(first,65535U)==nullptr,"old pose survived replacement");
    require(cache.find(moved,0U)!=nullptr&&cache.find(moved,0U)->size()==1,"new pose not cached");
    cache.clear();
    require(cache.find(moved,0U)==nullptr,"cleared cache falsely hit");
    std::cout<<"rendering_shadow_receiver_pose_cache_tests PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
