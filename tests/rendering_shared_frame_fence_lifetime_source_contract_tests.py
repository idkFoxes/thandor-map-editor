#!/usr/bin/env python3
"""Static guard for the V690 graphics-fence resource update synchronization.

Not a substitute for Vulkan validation. Runtime tests must exercise multiple frames
in flight and swapchain recreation on a native GPU.
"""
from pathlib import Path
root = Path(__file__).resolve().parent.parent
ctx_h = (root / 'src/editor/vulkan_context.hpp').read_text()
ctx = (root / 'src/editor/vulkan_context.cpp').read_text()
renderer = (root / 'src/editor/terrain_renderer.cpp').read_text()
backend = (root / 'tools/rendering_object_upload_backend.cpp').read_text()
log = (root / 'src/editor/editor_performance_log.hpp').read_text()


def require(ok, why):
    if not ok:
        raise AssertionError(why)

require('void wait_for_render_frames() const;' in ctx_h, 'missing fence-aware API')
require('bool frame_recording_{};' in ctx_h, 'missing pending-submit safety guard')
begin = ctx[ctx.index('std::optional<VulkanContext::Frame> VulkanContext::begin_frame()'):ctx.index('void VulkanContext::end_frame(')]
end = ctx[ctx.index('void VulkanContext::end_frame('):ctx.index('void VulkanContext::wait_idle()')]
require(begin.index('vkResetFences(') < begin.index('frame_recording_=true;') < begin.index('vkBeginCommandBuffer'),
        'fence reset must mark an unsubmitted recording interval')
require(end.index('vkQueueSubmit(') < end.index('frame_recording_=false;'),
        'must not unblock fence waits before the frame is submitted')
wait = ctx[ctx.index('void VulkanContext::wait_for_render_frames() const'):ctx.index('void VulkanContext::immediate_submit(')]
for token in ('if (frame_recording_)', 'std::logic_error', 'frames_[i].fence',
              'vkWaitForFences(device_,kFramesInFlight,fences.data(),VK_TRUE,UINT64_MAX)'):
    require(token in wait, f'frame fence wait must contain {token}')
require('vkDeviceWaitIdle' not in wait and 'vkQueueWaitIdle' not in wait,
        'resource wait must not idle the device/queue')
update = renderer[renderer.index('void TerrainRenderer::update_objects('):renderer.index('void TerrainRenderer::update_dynamic_objects(')]
if '#define THANDOR_FRAME_VERSIONED_OBJECT_ATLAS 1' in (root/'src/editor/terrain_renderer.hpp').read_text():
    prepare = renderer[renderer.index('void TerrainRenderer::prepare_static_frame('):renderer.index('void TerrainRenderer::update_dynamic_objects(')]
    require('context_.wait_for_render_frame(frame_index);' not in prepare, 'async frame double-waits its fence')
    require('record_async_object_texture_copy(' in prepare, 'versioned atlas missing queued copies')
    require('object_texture_frame_images_' in prepare, 'frame-specific atlas missing')
    require('context_.wait_for_render_frames();' not in update, 'CPU update still waits every frame')
else:
    require('context_.wait_for_render_frames();++object_upload_stats_.frame_fence_waits;' in update,
            'fallback shared atlas updates need graphics fence wait and counter')
    require('++object_upload_stats_.idle_waits; // Full uploader retains its own idle boundary.' in update,
            'fallback array recreation lost strong synchronization')
require('context_.wait_idle();++object_upload_stats_.idle_waits;' not in update,
        'device-idle wait accidentally reintroduced for all partial uploads')
require('VulkanContext::wait_for_render_frames()const' in backend and
        'VulkanContext::wait_for_render_frame(uint32_t)const' in backend,
        'software Vulkan-call recorder must emulate the new fence wait')
require('object_frame_fence_waits' in log and 'object_idle_waits' in log,
        'must report device-idle and frame-fence waits separately')
require('vkDeviceWaitIdle(device_);' in ctx, 'swapchain or teardown still needs device idle')
print('RENDER_SHARED_FRAME_FENCE_LIFETIME_PASS')
