// Developer-only Vulkan-call recorder probe. This executes the production renderer,
// with software API handles instead of a driver; it does not verify GPU pixels.
#include <bits/stdc++.h>
#include "object_texture_updates.hpp"
#define private public
#include "terrain_renderer.hpp"
#undef private
namespace upload_probe {
extern uint64_t idle_waits,frame_fence_waits,submits,copies,copied_bytes,images_created,buffers_created,descriptor_writes,barriers;
void require(bool,const char*);void inflight();void shaders(const std::filesystem::path&);
std::vector<uint8_t> buffer_data(VkBuffer,size_t);std::vector<uint8_t> image_layer(VkImage,uint32_t);
}
static std::vector<uint8_t> reference(const thandor::RgbaImage& image){
 std::vector<uint8_t> out(128*128*4);if(!image.width||!image.height||uint64_t(image.width)*image.height*4!=image.pixels.size())return out;
 for(size_t y=0;y<128;++y)for(size_t x=0;x<128;++x)for(size_t c=0;c<4;++c)out[(y*128+x)*4+c]=image.pixels[((y*image.height/128)*image.width+x*image.width/128)*4+c];return out;
}
int main()try{
 using namespace editor;using namespace upload_probe;
 const auto dir=std::filesystem::temp_directory_path()/("thandor-upload-probe-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));shaders(dir);
 VulkanContext vk(nullptr);TerrainRenderer renderer(vk);
 std::vector<ObjectVertex> vertices(3);vertices[1].position.x=1;vertices[2].position.y=1;
 std::vector<uint32_t> indices{0,1,2};std::vector<ObjectDrawRun> runs{{PrimitiveBlendPreset::AlphaNoDepthWrite,true,0,3,0,RetailWorldModelPass::PostGeneratedShading}};
 std::vector<ObjectVertex> shadows=vertices;std::vector<uint32_t> shadow_indices=indices;
 std::vector<thandor::RgbaImage> textures(3,thandor::RgbaImage{64,64,std::vector<uint8_t>(64*64*4,81)});
#if THANDOR_FRAME_VERSIONED_OBJECT_ATLAS
 #define PROBE_ATLAS renderer.object_texture_frame_images_[0]
#else
 #define PROBE_ATLAS renderer.object_textures_
#endif
 const auto initial_idle=idle_waits, initial_frame_fences=frame_fence_waits;
 uint64_t cases=0;auto verify=[&]{
  const auto compare=[&](VkBuffer buffer,const auto& data){if(data.empty())return;const auto got=buffer_data(buffer,data.size()*sizeof(data[0]));require(std::memcmp(got.data(),data.data(),got.size())==0,"retained buffer bytes differ");};
  compare(renderer.object_vertex_buffers_[0].buffer,vertices);compare(renderer.object_index_buffers_[0].buffer,indices);
  compare(renderer.shadow_vertex_buffers_[0].buffer,shadows);compare(renderer.shadow_index_buffers_[0].buffer,shadow_indices);
  require(renderer.object_index_count()==indices.size()&&renderer.shadow_index_count()==shadow_indices.size(),"logical geometry counts differ");
  require(renderer.object_texture_layer_count()==textures.size(),"logical texture count differs");
  for(uint32_t i=0;i<textures.size();++i)require(image_layer(PROBE_ATLAS.image,i)==reference(textures[i]),"texture bytes differ from full-upload oracle");++cases;
 };
 const auto update=[&]{inflight();renderer.update_objects(vertices,indices,runs,shadows,shadow_indices,textures);renderer.prepare_static_frame(0);verify();};
 update();
 const auto image=PROBE_ATLAS.image;const auto vertex=renderer.object_vertex_buffers_[0].buffer;const auto writes=descriptor_writes;
 auto waits=idle_waits,frame_waits=frame_fence_waits,transfers=copies;update();
 require(idle_waits==waits&&frame_fence_waits==frame_waits&&copies==transfers,"identical surface waited or transferred");
 require(renderer.static_transparent_triangles_.size()==1,"transparent source metadata missing");
 const auto before_metadata_waits=idle_waits, before_metadata_frame_waits=frame_fence_waits;
 runs[0].blend=PrimitiveBlendPreset::OpaqueDepthWrite;update();
 require(renderer.static_transparent_triangles_.empty()&&renderer.object_draw_runs_.size()==1,"metadata-only opaque update was skipped");
 runs[0].blend=PrimitiveBlendPreset::AlphaNoDepthWrite;update();
 require(renderer.static_transparent_triangles_.size()==1&&renderer.object_draw_runs_.empty(),"metadata-only alpha update was skipped");
 require(idle_waits==before_metadata_waits&&frame_fence_waits==before_metadata_frame_waits&&copies==transfers,"metadata-only update touched GPU resources");
 textures[1].pixels[0]^=1;update();
 require(PROBE_ATLAS.image==image&&descriptor_writes==writes,"one-layer mutation replaced image or descriptors");
 require(copies==transfers+1,"one-layer mutation uploaded more than one layer");
 textures.push_back(textures.back());update();require(PROBE_ATLAS.image==image,"append within capacity replaced array");
 textures.push_back(textures.back());update();require(PROBE_ATLAS.image!=image,"capacity growth failed to replace array");
 const auto capacity_image=PROBE_ATLAS.image;const auto after_growth=descriptor_writes;
 textures.resize(2);update();require(PROBE_ATLAS.image==capacity_image&&descriptor_writes==after_growth,"shrink replaced retained image");
 textures[0].pixels[0]^=1;vertices[0].packed_color_argb=0x11223344;update();require(renderer.object_vertex_buffers_[0].buffer==vertex,"same-size vertex mutation reallocated buffer");
 shadows.clear();shadow_indices.clear();update();
 shadows=vertices;shadow_indices=indices;update();
 for(unsigned i=0;i<36;++i){textures.resize(1+i%13,thandor::RgbaImage{32,64,std::vector<uint8_t>(32*64*4,91)});textures[i%textures.size()].pixels[0]^=uint8_t(i);update();}
 vertices.clear();indices.clear();runs.clear();shadows.clear();shadow_indices.clear();update();
 const auto stats=renderer.object_upload_stats();
 require(stats.idle_waits==idle_waits-initial_idle,"recorded device idle waits differ from Vulkan-context calls");
 require(stats.frame_fence_waits==frame_fence_waits-initial_frame_fences,
         "recorded frame fence waits differ from Vulkan-context calls");
 require(stats.skipped>=2&&stats.partial_uploads>20&&stats.array_recreates<8,"resource reuse counters unexpected");
 std::cout<<"OBJECT_UPLOAD_API_PROBE PASS cases="<<cases<<" recreates="<<stats.array_recreates<<" partial_uploads="<<stats.partial_uploads<<" skipped="<<stats.skipped<<" layers_uploaded="<<stats.layers_uploaded<<" buffer_writes="<<stats.buffer_writes<<" barrier_calls="<<barriers<<" GPU_verified=0\n";
 std::filesystem::remove_all(dir);
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
