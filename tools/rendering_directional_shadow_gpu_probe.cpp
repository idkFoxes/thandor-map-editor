// Runs the production TerrainRenderer and shaders on a real offscreen Vulkan device.
// Only window/swapchain management is replaced; all images, pipelines and pixels are real.
#include <bits/stdc++.h>
#define private public
#include "terrain_renderer.hpp"
#undef private
using namespace editor;
static void check(VkResult r,const char* s){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(s)+": "+std::to_string(r));}
static void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
namespace editor {
void VulkanContext::check(VkResult r,const char* message)const{::check(r,message);}
VulkanContext::VulkanContext(GLFWwindow*) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Thandor offscreen shadow probe";app.apiVersion=VK_API_VERSION_1_0;
    VkInstanceCreateInfo instance{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instance.pApplicationInfo=&app;
    check(vkCreateInstance(&instance,nullptr,&instance_),"instance");
    uint32_t n=0;check(vkEnumeratePhysicalDevices(instance_,&n,nullptr),"enumerate");require(n>0,"no Vulkan device");
    std::vector<VkPhysicalDevice> devices(n);check(vkEnumeratePhysicalDevices(instance_,&n,devices.data()),"devices");physical_device_=devices[0];
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device_,&n,families.data());
    bool found=false;for(uint32_t i=0;i<n;++i)if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){graphics_queue_family_=i;found=true;break;}require(found,"no graphics queue");
    const float priority=1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=graphics_queue_family_;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;
    check(vkCreateDevice(physical_device_,&device,nullptr,&device_),"device");vkGetDeviceQueue(device_,graphics_queue_family_,0,&graphics_queue_);
    VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical_device_,&props);max_image_array_layers_=props.limits.maxImageArrayLayers;
    std::cout<<"Vulkan device: "<<props.deviceName<<'\n';
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=graphics_queue_family_;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    check(vkCreateCommandPool(device_,&pool,nullptr,&command_pool_),"pool");
    swapchain_extent_={512,512};swapchain_format_=VK_FORMAT_R8G8B8A8_UNORM;
    std::array<VkAttachmentDescription,2> a{};a[0].format=swapchain_format_;a[1].format=depth_format_;
    for(auto& x:a){x.samples=VK_SAMPLE_COUNT_1_BIT;x.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;x.storeOp=VK_ATTACHMENT_STORE_OP_STORE;x.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;x.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;}
    a[0].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;a[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;sub.pDepthStencilAttachment=&depth;
    VkSubpassDependency dep{};dep.srcSubpass=0;dep.dstSubpass=VK_SUBPASS_EXTERNAL;dep.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dep.dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dep.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dep.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};pass.attachmentCount=2;pass.pAttachments=a.data();pass.subpassCount=1;pass.pSubpasses=&sub;pass.dependencyCount=1;pass.pDependencies=&dep;
    check(vkCreateRenderPass(device_,&pass,nullptr,&render_pass_),"color pass");
}
VulkanContext::~VulkanContext(){vkDeviceWaitIdle(device_);vkDestroyRenderPass(device_,render_pass_,nullptr);vkDestroyCommandPool(device_,command_pool_,nullptr);vkDestroyDevice(device_,nullptr);vkDestroyInstance(instance_,nullptr);}
void VulkanContext::wait_idle()const{check(vkDeviceWaitIdle(device_),"idle");}
void VulkanContext::wait_for_render_frames()const{wait_idle();}
void VulkanContext::wait_for_render_frame(uint32_t)const{wait_idle();}
uint32_t VulkanContext::find_memory_type(uint32_t bits,VkMemoryPropertyFlags flags)const{
 VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical_device_,&p);
 for(uint32_t i=0;i<p.memoryTypeCount;++i)if((bits&(1U<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
 throw std::runtime_error("memory type");
}
void VulkanContext::immediate_submit(const std::function<void(VkCommandBuffer)>& fn)const{
 VkCommandBufferAllocateInfo a{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};a.commandPool=command_pool_;a.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;a.commandBufferCount=1;
 VkCommandBuffer c{};check(vkAllocateCommandBuffers(device_,&a,&c),"command");VkCommandBufferBeginInfo b{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};check(vkBeginCommandBuffer(c,&b),"begin");fn(c);check(vkEndCommandBuffer(c),"end");
 VkSubmitInfo s{VK_STRUCTURE_TYPE_SUBMIT_INFO};s.commandBufferCount=1;s.pCommandBuffers=&c;check(vkQueueSubmit(graphics_queue_,1,&s,VK_NULL_HANDLE),"submit");check(vkQueueWaitIdle(graphics_queue_),"wait");vkFreeCommandBuffers(device_,command_pool_,1,&c);
}
}
struct Target {
 VulkanContext& c;std::array<VkImage,2> image{};std::array<VkDeviceMemory,2> memory{};std::array<VkImageView,2> view{};VkFramebuffer frame{};
 Target(VulkanContext& v):c(v){for(int i=0;i<2;++i){
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=i?c.depth_format():c.color_format();info.extent={512,512,1};info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=i?VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  check(vkCreateImage(c.device(),&info,nullptr,&image[i]),"target image");VkMemoryRequirements req{};vkGetImageMemoryRequirements(c.device(),image[i],&req);
  VkMemoryAllocateInfo mem{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mem.allocationSize=req.size;mem.memoryTypeIndex=c.find_memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);check(vkAllocateMemory(c.device(),&mem,nullptr,&memory[i]),"target memory");check(vkBindImageMemory(c.device(),image[i],memory[i],0),"target bind");
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=info.format;vi.subresourceRange={i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};check(vkCreateImageView(c.device(),&vi,nullptr,&view[i]),"target view");
 }VkFramebufferCreateInfo f{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};f.renderPass=c.render_pass();f.attachmentCount=2;f.pAttachments=view.data();f.width=f.height=512;f.layers=1;check(vkCreateFramebuffer(c.device(),&f,nullptr,&frame),"target frame");}
 ~Target(){vkDestroyFramebuffer(c.device(),frame,nullptr);for(int i=0;i<2;++i){vkDestroyImageView(c.device(),view[i],nullptr);vkDestroyImage(c.device(),image[i],nullptr);vkFreeMemory(c.device(),memory[i],nullptr);}}
};
static std::vector<ObjectVertex> box(float x,float y,float yaw=0){
 std::vector<ObjectVertex> out;const std::array<Vec3f,8> p{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0},{-1,-1,5},{1,-1,5},{1,1,5},{-1,1,5}}};
 for(const auto f:std::array<std::array<int,3>,12>{{{0,2,1},{0,3,2},{4,5,6},{4,6,7},{0,1,5},{0,5,4},{1,2,6},{1,6,5},{2,3,7},{2,7,6},{3,0,4},{3,4,7}}}){
  auto a=p[f[0]],b=p[f[1]],c=p[f[2]];auto n=normalize(cross(sub(b,a),sub(c,a)));
  for(int j=0;j<3;++j){const auto q=p[f[j]];out.push_back({{x+q.x*std::cos(yaw)-q.y*std::sin(yaw),y+q.x*std::sin(yaw)+q.y*std::cos(yaw),q.z},n,j==1?1.0F:0.0F,j==2?1.0F:0.0F,0,0xFFFFFFFF,1,0,0,1});}
 }return out;
}
static void ppm(const std::filesystem::path& p,const std::vector<uint8_t>& pixels){std::ofstream f(p,std::ios::binary);f<<"P6\n512 512\n255\n";for(size_t i=0;i<pixels.size();i+=4)f.write(reinterpret_cast<const char*>(pixels.data()+i),3);}
int main(int argc,char** argv)try{
 require(argc==2||argc==3,"Usage: rendering_directional_shadow_gpu_probe OUTPUT_DIR [STOCK_SCENE_BIN]");const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
 VulkanContext c(nullptr);TerrainRenderer r(c);Target target(c);
 auto field=thandor::FieldGrid::create(129,129,0,0);thandor::TerrainLighting lighting{};r.update_mesh(field,lighting,nullptr,nullptr,false);
 for(auto& vertex:r.cpu_vertices_)vertex.packed_light_argb=0xFFFFFFFFU;
 r.update_buffer(r.vertex_buffer_,{reinterpret_cast<const std::byte*>(r.cpu_vertices_.data()),r.cpu_vertices_.size()*sizeof(thandor::TerrainVertex)},VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
 r.shadow_ray_={0.7F,0.35F,-1.0F};
 thandor::RgbaImage white{4,4,std::vector<uint8_t>(64,255)};r.update_materials(std::span(&white,1));
 auto red=white;for(size_t i=0;i<red.pixels.size();i+=4){red.pixels[i]=210;red.pixels[i+1]=50;red.pixels[i+2]=50;}
 r.update_objects({}, {}, {}, {}, {},std::span(&red,1));
 Camera camera;camera.target={40,-30,0};camera.distance=40;camera.yaw=-2.0F;camera.pitch=0.9F;
 auto vertices=box(40,-30);std::vector<uint32_t> indices(vertices.size());std::iota(indices.begin(),indices.end(),0);
 ObjectDrawRun run{PrimitiveBlendPreset::OpaqueDepthWrite,true,0,static_cast<uint32_t>(indices.size())};
 std::vector<ObjectDrawRun> runs{run};
 bool hide_authored=false;
 auto readback=r.create_buffer(512*512*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
 auto render=[&](uint32_t slot,bool enabled){
  r.update_dynamic_objects(slot,vertices,indices,runs,{},{});
  c.immediate_submit([&](VkCommandBuffer cmd){r.prepare_static_frame(slot,cmd);r.prepare_shadow_frame(slot,cmd,camera,1.0F,enabled);
   std::array<VkClearValue,2> clears{};clears[0].color={{0.08F,0.1F,0.15F,1}};clears[1].depthStencil={1,0};
   VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=c.render_pass();begin.framebuffer=target.frame;begin.renderArea.extent={512,512};begin.clearValueCount=2;begin.pClearValues=clears.data();vkCmdBeginRenderPass(cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
   r.render(cmd,camera,false,false,slot,false,hide_authored,false,false,false,{0,0,512,512},{512,512});vkCmdEndRenderPass(cmd);
   VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={512,512,1};vkCmdCopyImageToBuffer(cmd,target.image[0],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,readback.buffer,1,&copy);
  });std::vector<uint8_t> pixels(512*512*4);void* p{};check(vkMapMemory(c.device(),readback.memory,0,pixels.size(),0,&p),"map");std::memcpy(pixels.data(),p,pixels.size());vkUnmapMemory(c.device(),readback.memory);return pixels;
 };
 auto off=render(0,false),on=render(1,true);ppm(out/"disabled.ppm",off);ppm(out/"enabled.ppm",on);
 const auto ground_difference=[](const auto& a,const auto& b){size_t n=0;for(size_t i=0;i<a.size();i+=4)if(a[i]==a[i+1]&&a[i+1]==a[i+2]&&int(a[i])-int(b[i])>8)++n;return n;};
 const auto shadow_pixels=ground_difference(off,on);require(shadow_pixels>80,"no ground shadow was rendered");
 // A caster removed from the current pose stream must disappear on the very next slot.
 for(auto& v:vertices)v.casts_shadow=0;auto removed=render(0,true);require(ground_difference(off,removed)==0,"stale caster survived removal");
 vertices=box(36,-30,1.0F);auto moved_off=render(1,false),moved_on=render(0,true);ppm(out/"moved_rotated.ppm",moved_on);require(ground_difference(moved_off,moved_on)>80,"moving caster lost shadow");
 // Check the actual terrain pixel at the fixed-light projected center, independent of box yaw.
 float sx{},sy{},depth{};require(project_to_screen({39.5F,-28.25F,0},camera.view_projection(1),512,512,sx,sy,depth),"receiver outside camera");
 size_t dark=0;for(int y=int(sy)-3;y<=int(sy)+3;++y)for(int x=int(sx)-3;x<=int(sx)+3;++x){const auto i=(size_t(y)*512+x)*4;if(int(moved_off[i])-int(moved_on[i])>8)++dark;}
 require(dark>5,"shadow direction changed with model yaw");
 // A fully transparent cutout casts no opaque rectangle.
 for(size_t i=3;i<red.pixels.size();i+=4)red.pixels[i]=0;r.update_objects({}, {}, {}, {}, {},std::span(&red,1));auto transparent=render(1,true),transparent_off=render(0,false);require(ground_difference(transparent_off,transparent)==0,"transparent texture casts a rectangle");
 // A partially transparent billboard must preserve holes in its silhouette.
 vertices={{{38,-32,5},{0,0,1},0,0,0,0xFFFFFFFF,1,0,0,1},
           {{42,-32,5},{0,0,1},1,0,0,0xFFFFFFFF,1,0,0,1},
           {{42,-28,5},{0,0,1},1,1,0,0xFFFFFFFF,1,0,0,1},
           {{38,-28,5},{0,0,1},0,1,0,0xFFFFFFFF,1,0,0,1}};
 indices={0,1,2,0,2,3};runs={run};runs[0].index_count=6;
 for(size_t i=3;i<red.pixels.size();i+=4)red.pixels[i]=255;
 r.update_objects({}, {}, {}, {}, {},std::span(&red,1));auto full_off=render(0,false),full_on=render(1,true);
 const auto full_shadow=ground_difference(full_off,full_on);
 for(uint32_t y=0;y<4;++y)for(uint32_t x=0;x<2;++x)red.pixels[(y*4+x)*4+3]=0;
 r.update_objects({}, {}, {}, {}, {},std::span(&red,1));auto hole_off=render(0,false),hole_on=render(1,true);
 const auto cutout_shadow=ground_difference(hole_off,hole_on);
 require(cutout_shadow>30&&cutout_shadow<full_shadow*0.85,"alpha-cutout holes were filled in shadow map");
 // Static casters and authored/dynamic replacement suppression share the main visibility rules.
 for(size_t i=3;i<red.pixels.size();i+=4)red.pixels[i]=255;
 auto static_vertices=box(40,-30);for(auto& v:static_vertices)v.direct_tint_bypass|=2U;
 std::vector<uint32_t> static_indices(static_vertices.size());std::iota(static_indices.begin(),static_indices.end(),0);
 r.update_objects(static_vertices,static_indices,std::span(&run,1),{},{},std::span(&red,1));
 vertices.clear();indices.clear();runs.clear();auto static_off=render(0,false),static_on=render(1,true);
 require(ground_difference(static_off,static_on)>80,"static model does not cast");
 hide_authored=true;auto hidden_off=render(0,false),hidden_on=render(1,true);
 require(ground_difference(hidden_off,hidden_on)==0,"hidden static replacement leaves phantom shadow");hide_authored=false;
 r.update_objects({}, {}, {}, {}, {},std::span(&red,1));
 // Sloping terrain receives the same fixed-light silhouette without floating geometry.
 for(auto& v:r.cpu_vertices_){v.position.z=(v.position.x-40.0F)*0.15F;v.normal=normalize({-0.15F,0,1});}
 r.update_buffer(r.vertex_buffer_,{reinterpret_cast<const std::byte*>(r.cpu_vertices_.data()),r.cpu_vertices_.size()*sizeof(thandor::TerrainVertex)},VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
 vertices=box(40,-30);indices=static_indices;runs={run};auto slope_off=render(0,false),slope_on=render(1,true);
 require(ground_difference(slope_off,slope_on)>80,"sloping terrain loses received shadow");ppm(out/"slope.ppm",slope_on);
 if(argc==3){
  std::ifstream stream(argv[2],std::ios::binary);
  auto read=[&](auto& value){stream.read(reinterpret_cast<char*>(&value),sizeof(value));};
  auto vector=[&](auto& values){uint64_t count{};read(count);require(count<10000000,"invalid stock scene vector");values.resize(count);if(count)stream.read(reinterpret_cast<char*>(values.data()),count*sizeof(values[0]));};
  read(camera);read(r.shadow_ray_);vector(vertices);vector(indices);vector(runs);
  uint64_t count{};read(count);require(count<4096,"invalid stock texture count");std::vector<thandor::RgbaImage> textures(count);
  for(auto& im:textures){read(im.width);read(im.height);vector(im.pixels);}require(bool(stream),"stock scene read failed");
  for(auto& v:r.cpu_vertices_){v.position.z=0;v.normal={0,0,1};}
  r.update_buffer(r.vertex_buffer_,{reinterpret_cast<const std::byte*>(r.cpu_vertices_.data()),r.cpu_vertices_.size()*sizeof(thandor::TerrainVertex)},VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  r.update_objects({}, {}, {}, {}, {},textures);
  auto stock_off=render(0,false),stock_on=render(1,true);ppm(out/"stock_disabled.ppm",stock_off);ppm(out/"stock_enabled.ppm",stock_on);
  const auto count_shadow=ground_difference(stock_off,stock_on);require(count_shadow>3,"stock model does not cast visible GPU shadow");
  std::cout<<"STOCK_GPU_SHADOW PASS scene="<<argv[2]<<" shadow_pixels="<<count_shadow<<'\n';
 }
 // Secondary-surface sorting must reuse exact CPU order across unchanged frames/slots.
 r.cpu_surface_vertices_.assign(r.cpu_vertices_.begin(),r.cpu_vertices_.end());
 for(auto& v:r.cpu_surface_vertices_){v.blend_materials={0x12345678U,0x00090007U,0x02000100U};}
 r.sorted_surface_valid_=false;r.presentation_tick_=17;
 auto verify_surface=[&](uint32_t slot,bool animate){
  r.update_depth_sorted_surface(slot,camera,animate);
  require(r.sorted_surface_indices_==retail_surface_back_to_front_indices(r.cpu_surface_vertices_,camera,r.presentation_tick_,animate),"cached surface order differs from fresh oracle");
 };
 verify_surface(0,true);const auto revision=r.sorted_surface_revision_;
 verify_surface(1,true);verify_surface(0,true);require(r.sorted_surface_revision_==revision,"unchanged frame or second slot re-sorted surfaces");
 camera.yaw+=0.1F;verify_surface(0,true);require(r.sorted_surface_revision_==revision+1,"camera change did not invalidate surface order");
 ++r.presentation_tick_;verify_surface(1,true);require(r.sorted_surface_revision_==revision+2,"wave tick did not invalidate surface order");
 r.presentation_tick_=0;verify_surface(0,false);const auto paused_revision=r.sorted_surface_revision_;
 verify_surface(1,true);require(r.sorted_surface_revision_==paused_revision+1,"animation toggle at tick zero reused a different pose");
 const auto begin_uncached=std::chrono::steady_clock::now();size_t checksum=0;
 for(int i=0;i<100;++i)checksum+=retail_surface_back_to_front_indices(r.cpu_surface_vertices_,camera,0,true).size();
 const auto uncached_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin_uncached).count();
 const auto begin_cached=std::chrono::steady_clock::now();
 for(int i=0;i<100;++i)r.update_depth_sorted_surface(uint32_t(i)%2,camera,true);
 const auto cached_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin_cached).count();
 require(checksum>0,"surface benchmark has no packets");
 std::cout<<"SURFACE_CACHE PASS unchanged_frames=100 frame_slots=2 camera_wave_toggle_invalidation=1 oracle_equal=1 fresh_ms="<<uncached_ms<<" cached_ms="<<cached_ms<<'\n';
 r.destroy_buffer(readback);
 std::cout<<"DIRECTIONAL_SHADOW_GPU PASS ground_shadow_pixels="<<shadow_pixels<<" fixed_light_pixels="<<dark<<" frame_slots=2 removal=1 alpha_cutout=1 cutout_holes=1 static_replacement=1 slope_receiver=1 Vulkan_pixels_verified=1\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
