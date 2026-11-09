// Explicit software Vulkan-call recorder. No GPU execution or shader validation.
#include "vulkan_context.hpp"
#include <map>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <cstdlib>
namespace upload_probe {
struct Buffer {VkDeviceSize size{};VkDeviceMemory memory{};};
struct Image {uint32_t extent{},layers{},mips{};std::vector<std::vector<uint8_t>> pixels;std::vector<VkImageLayout> layouts;};
std::map<VkBuffer,Buffer> buffers;std::map<VkDeviceMemory,std::vector<uint8_t>> memories;std::map<VkImage,Image> images;
uint64_t idle_waits{},frame_fence_waits{},submits{},copies{},copied_bytes{},images_created{},buffers_created{},descriptor_writes{},barriers{};bool pending{};
uintptr_t serial=4096;template<class T>T handle(){return reinterpret_cast<T>(++serial*16);}
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::vector<uint8_t> buffer_data(VkBuffer b,size_t size){const auto& v=memories.at(buffers.at(b).memory);return {v.begin(),v.begin()+size};}
std::vector<uint8_t> image_layer(VkImage image,uint32_t layer){return images.at(image).pixels.at(layer);}
void inflight(){pending=true;}
void shaders(const std::filesystem::path& dir){std::filesystem::create_directories(dir);for(const char* n:{"terrain.vert.spv","terrain.frag.spv","surface.vert.spv","surface.frag.spv","model.vert.spv","model.frag.spv","shadow.frag.spv"}){std::ofstream f(dir/n,std::ios::binary);uint32_t word=0x07230203;f.write(reinterpret_cast<const char*>(&word),4);}setenv("THANDOR_SHADER_DIR",dir.c_str(),1);}
}
namespace editor {
VulkanContext::VulkanContext(GLFWwindow* w):window_(w){max_image_array_layers_=1024;}
VulkanContext::~VulkanContext()=default;
void VulkanContext::wait_idle()const{++upload_probe::idle_waits;upload_probe::pending=false;}
void VulkanContext::wait_for_render_frames()const{++upload_probe::frame_fence_waits;upload_probe::pending=false;}
void VulkanContext::wait_for_render_frame(uint32_t)const{++upload_probe::frame_fence_waits;upload_probe::pending=false;}
uint32_t VulkanContext::find_memory_type(uint32_t,VkMemoryPropertyFlags)const{return 0;}
void VulkanContext::immediate_submit(const std::function<void(VkCommandBuffer)>& fn)const{++upload_probe::submits;fn(upload_probe::handle<VkCommandBuffer>());}
}
using namespace upload_probe;

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(
    VkDevice                                    device,
    const VkDescriptorSetAllocateInfo*          pAllocateInfo,
    VkDescriptorSet*                            pDescriptorSets){for(uint32_t i=0;i<pAllocateInfo->descriptorSetCount;++i)pDescriptorSets[i]=handle<VkDescriptorSet>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(
    VkDevice                                    device,
    const VkMemoryAllocateInfo*                 pAllocateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDeviceMemory*                             pMemory){*pMemory=handle<VkDeviceMemory>();memories[*pMemory].resize(pAllocateInfo->allocationSize);return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset){buffers.at(buffer).memory=memory;return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkBindImageMemory(
    VkDevice                                    device,
    VkImage                                     image,
    VkDeviceMemory                              memory,
    VkDeviceSize                                memoryOffset){return VK_SUCCESS;}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipelineLayout                            layout,
    uint32_t                                    firstSet,
    uint32_t                                    descriptorSetCount,
    const VkDescriptorSet*                      pDescriptorSets,
    uint32_t                                    dynamicOffsetCount,
    const uint32_t*                             pDynamicOffsets){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindIndexBuffer(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    buffer,
    VkDeviceSize                                offset,
    VkIndexType                                 indexType){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(
    VkCommandBuffer                             commandBuffer,
    VkPipelineBindPoint                         pipelineBindPoint,
    VkPipeline                                  pipeline){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdBindVertexBuffers(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstBinding,
    uint32_t                                    bindingCount,
    const VkBuffer*                             pBuffers,
    const VkDeviceSize*                         pOffsets){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(
    VkCommandBuffer                             commandBuffer,
    VkBuffer                                    srcBuffer,
    VkImage                                     dstImage,
    VkImageLayout                               dstImageLayout,
    uint32_t                                    regionCount,
    const VkBufferImageCopy*                    pRegions){auto& v=images.at(dstImage);const auto& src=memories.at(buffers.at(srcBuffer).memory);for(uint32_t i=0;i<regionCount;++i){const auto& c=pRegions[i];require(c.imageSubresource.layerCount==1,"unexpected copy span");const auto index=c.imageSubresource.baseArrayLayer*v.mips+c.imageSubresource.mipLevel;require(v.layouts.at(index)==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL&&dstImageLayout==VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,"copy layout mismatch");auto& dest=v.pixels.at(index);const auto bytes=size_t(c.imageExtent.width)*c.imageExtent.height*4;require(bytes==dest.size()&&c.bufferOffset+bytes<=src.size(),"copy payload mismatch");std::memcpy(dest.data(),src.data()+c.bufferOffset,bytes);++copies;copied_bytes+=bytes;}}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdDrawIndexed(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    indexCount,
    uint32_t                                    instanceCount,
    uint32_t                                    firstIndex,
    int32_t                                     vertexOffset,
    uint32_t                                    firstInstance){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlags                        srcStageMask,
    VkPipelineStageFlags                        dstStageMask,
    VkDependencyFlags                           dependencyFlags,
    uint32_t                                    memoryBarrierCount,
    const VkMemoryBarrier*                      pMemoryBarriers,
    uint32_t                                    bufferMemoryBarrierCount,
    const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
    uint32_t                                    imageMemoryBarrierCount,
    const VkImageMemoryBarrier*                 pImageMemoryBarriers){require(!pending,"image transition occurred while frames were in flight");for(uint32_t i=0;i<imageMemoryBarrierCount;++i){const auto& b=pImageMemoryBarriers[i];auto& v=images.at(b.image);require(b.subresourceRange.baseArrayLayer+b.subresourceRange.layerCount<=v.layers,"barrier layer range exceeds image");for(uint32_t l=b.subresourceRange.baseArrayLayer;l<b.subresourceRange.baseArrayLayer+b.subresourceRange.layerCount;++l)for(uint32_t m=b.subresourceRange.baseMipLevel;m<b.subresourceRange.baseMipLevel+b.subresourceRange.levelCount;++m){auto& layout=v.layouts.at(l*v.mips+m);require(b.oldLayout==VK_IMAGE_LAYOUT_UNDEFINED||layout==b.oldLayout,"old image layout mismatch");layout=b.newLayout;}}++barriers;}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdSetScissor(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstScissor,
    uint32_t                                    scissorCount,
    const VkRect2D*                             pScissors){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdSetViewport(
    VkCommandBuffer                             commandBuffer,
    uint32_t                                    firstViewport,
    uint32_t                                    viewportCount,
    const VkViewport*                           pViewports){}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(
    VkDevice                                    device,
    const VkBufferCreateInfo*                   pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkBuffer*                                   pBuffer){*pBuffer=handle<VkBuffer>();buffers[*pBuffer]={pCreateInfo->size,{}};++buffers_created;return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(
    VkDevice                                    device,
    const VkDescriptorPoolCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorPool*                           pDescriptorPool){*pDescriptorPool=handle<VkDescriptorPool>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(
    VkDevice                                    device,
    const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkDescriptorSetLayout*                      pSetLayout){*pSetLayout=handle<VkDescriptorSetLayout>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateGraphicsPipelines(
    VkDevice                                    device,
    VkPipelineCache                             pipelineCache,
    uint32_t                                    createInfoCount,
    const VkGraphicsPipelineCreateInfo*         pCreateInfos,
    const VkAllocationCallbacks*                pAllocator,
    VkPipeline*                                 pPipelines){for(uint32_t i=0;i<createInfoCount;++i)pPipelines[i]=handle<VkPipeline>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImage(
    VkDevice                                    device,
    const VkImageCreateInfo*                    pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImage*                                    pImage){*pImage=handle<VkImage>();Image v;v.extent=pCreateInfo->extent.width;v.layers=pCreateInfo->arrayLayers;v.mips=pCreateInfo->mipLevels;v.pixels.resize(v.layers*v.mips);v.layouts.resize(v.layers*v.mips,VK_IMAGE_LAYOUT_UNDEFINED);for(uint32_t l=0;l<v.layers;++l)for(uint32_t m=0;m<v.mips;++m){auto n=std::max(1U,v.extent>>m);v.pixels[l*v.mips+m].resize(size_t(n)*n*4);}images[*pImage]=std::move(v);++images_created;return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(
    VkDevice                                    device,
    const VkImageViewCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkImageView*                                pView){*pView=handle<VkImageView>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(
    VkDevice                                    device,
    const VkPipelineLayoutCreateInfo*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkPipelineLayout*                           pPipelineLayout){*pPipelineLayout=handle<VkPipelineLayout>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateSampler(
    VkDevice                                    device,
    const VkSamplerCreateInfo*                  pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkSampler*                                  pSampler){*pSampler=handle<VkSampler>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(
    VkDevice                                    device,
    const VkShaderModuleCreateInfo*             pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkShaderModule*                             pShaderModule){*pShaderModule=handle<VkShaderModule>();return VK_SUCCESS;}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    const VkAllocationCallbacks*                pAllocator){buffers.erase(buffer);}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(
    VkDevice                                    device,
    VkDescriptorPool                            descriptorPool,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(
    VkDevice                                    device,
    VkDescriptorSetLayout                       descriptorSetLayout,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImage(
    VkDevice                                    device,
    VkImage                                     image,
    const VkAllocationCallbacks*                pAllocator){images.erase(image);}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(
    VkDevice                                    device,
    VkImageView                                 imageView,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(
    VkDevice                                    device,
    VkPipeline                                  pipeline,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(
    VkDevice                                    device,
    VkPipelineLayout                            pipelineLayout,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroySampler(
    VkDevice                                    device,
    VkSampler                                   sampler,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(
    VkDevice                                    device,
    VkShaderModule                              shaderModule,
    const VkAllocationCallbacks*                pAllocator){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkFreeMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    const VkAllocationCallbacks*                pAllocator){memories.erase(memory);}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(
    VkDevice                                    device,
    VkBuffer                                    buffer,
    VkMemoryRequirements*                       pMemoryRequirements){*pMemoryRequirements={};pMemoryRequirements->size=buffers.at(buffer).size;pMemoryRequirements->alignment=16;pMemoryRequirements->memoryTypeBits=1;}

extern "C" VKAPI_ATTR void VKAPI_CALL vkGetImageMemoryRequirements(
    VkDevice                                    device,
    VkImage                                     image,
    VkMemoryRequirements*                       pMemoryRequirements){*pMemoryRequirements={};pMemoryRequirements->size=16;pMemoryRequirements->alignment=16;pMemoryRequirements->memoryTypeBits=1;}

extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory,
    VkDeviceSize                                offset,
    VkDeviceSize                                size,
    VkMemoryMapFlags                            flags,
    void**                                      ppData){require(!pending,"mapped a shared buffer while frames were in flight");auto& data=memories.at(memory);require(offset+size<=data.size(),"mapping exceeded capacity");*ppData=data.data()+offset;return VK_SUCCESS;}

extern "C" VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(
    VkDevice                                    device,
    VkDeviceMemory                              memory){}

extern "C" VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(
    VkDevice                                    device,
    uint32_t                                    descriptorWriteCount,
    const VkWriteDescriptorSet*                 pDescriptorWrites,
    uint32_t                                    descriptorCopyCount,
    const VkCopyDescriptorSet*                  pDescriptorCopies){descriptor_writes+=descriptorWriteCount;}
