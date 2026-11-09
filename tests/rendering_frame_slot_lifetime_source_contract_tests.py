#!/usr/bin/env python3
"""Validate ownership invariants on the actual Vulkan sources (not a Vulkan SDK build)."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
renderer=(root/'src/editor/terrain_renderer.cpp').read_text()
header=(root/'src/editor/terrain_renderer.hpp').read_text()
context=(root/'src/editor/vulkan_context.cpp').read_text()
app=(root/'src/editor/editor_app.cpp').read_text()

def ensure(test,why):
    if not test: raise AssertionError(why)

ensure('renderer_.prepare_static_frame(frame->frame_index,frame->command_buffer);' in app,
       'frame-local asynchronous preparation not wired into selected-view draw')
ensure(app.index('auto frame=vulkan_.begin_frame();') <
       app.index('renderer_.prepare_static_frame(frame->frame_index,frame->command_buffer);') <
       app.index('vulkan_.begin_render_pass(*frame);'),
       'atlas copy must be recorded after slot fence and before render pass')
ensure(context.index('return Frame{frame.command_buffer,image_index,current_frame_};') <
       context.index('void VulkanContext::begin_render_pass('),
       'render pass must not begin until after async transfer recording')
ensure('if (frame_index>=kFramesInFlight || frame_recording_)' in context,'invalid/reset frame fence guard missing')
ensure('vkWaitForFences(device_,1,&frames_[frame_index].fence' in context,'slot completion must be proven')
ensure('object_vertex_buffers_[frame_index].buffer' in renderer,'static vertex draw not frame versioned')
ensure('shadow_index_buffers_[frame_index].buffer' in renderer,'shadow index draw not frame versioned')
ensure('static_buffer_frame_revisions_' in header,'per-frame geometry revision state missing')
ensure('vkWaitForFences(device_,1,&frame.fence' in context,'frame slot recycled without fence')
prepare=renderer.split('void TerrainRenderer::prepare_static_frame(',1)[1].split('void TerrainRenderer::update_dynamic_objects(',1)[0]
ensure('context_.wait_for_render_frame(frame_index);' not in prepare,'async frame still forces second fence wait')
ensure('destroy_buffer(object_texture_frame_staging_[frame_index]);' in prepare,'staging not retired on slot reuse')
ensure('object_texture_frame_images_' in header,'per-frame texture atlas missing')
ensure('object_texture_frame_caches_' in header,'missing per-frame pixel state')
ensure('cache.prepare_shared(layers,image.capacity_layers' in prepare,'no catch-up synchronization for inactive frames')
ensure('record_async_object_texture_copy(frame_index,command,image,update,pixels,false)' in prepare,'async partial updates missing')
ensure('create_async_object_frame_image(frame_index,update.capacity' in prepare and
       'record_async_object_texture_copy(frame_index,command,image,update,pixels,true)' in prepare,
       'asynchronous initial atlas upload missing')
ensure('destroy_image_array(image);' in prepare and
       app.index('auto frame=vulkan_.begin_frame();')<app.index('renderer_.prepare_static_frame('),
       'retirement must follow acquire frame fence')
ensure('const auto descriptor_set=descriptor_sets_[frame];' in renderer,'frame-specific texture descriptors missing')
ensure('if(descriptor_frame>=VulkanContext::kFramesInFlight)context_.wait_idle();' in renderer,'global uploader safety boundary changed')
update=renderer.split('void TerrainRenderer::update_objects(',1)[1].split('void TerrainRenderer::prepare_static_frame(',1)[0]
ensure('wait_for_render_frames()' not in update and 'wait_idle()' not in update,'CPU update blocks all submitted frames')
ensure('object_texture_cache_.commit(std::move(texture_update));' in update,'logical pixels are not retained')
ensure('image.layers=object_texture_layer_count_' in prepare,'shader logical layer bound is missing')
ensure('context_.immediate_submit(' not in prepare,'async path still blocks for immediate transfer')
print('FRAME_SLOT_LIFETIME_SOURCE_CONTRACT PASS buffers textures descriptors frame fence async copies')
