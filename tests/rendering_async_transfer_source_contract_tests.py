#!/usr/bin/env python3
"""V693 non-GPU lifetime and ordering check; not native Vulkan runtime validation."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text()
renderer=(root/'src/editor/terrain_renderer.cpp').read_text()
context=(root/'src/editor/vulkan_context.cpp').read_text()
header=(root/'src/editor/terrain_renderer.hpp').read_text()
log=(root/'src/editor/editor_performance_log.hpp').read_text()

def require(condition,why):
    if not condition:raise AssertionError(why)

begin=context.split('std::optional<VulkanContext::Frame> VulkanContext::begin_frame()',1)[1].split('void VulkanContext::begin_render_pass(',1)[0]
pass_section=context.split('void VulkanContext::begin_render_pass(',1)[1].split('void VulkanContext::end_frame(',1)[0]
require('vkWaitForFences(' in begin,'begin_frame must wait for recyclable slot')
require('vkCmdBeginRenderPass(' not in begin,'began render pass before transfer commands')
require('vkCmdBeginRenderPass(' in pass_section,'missing explicit render-pass start')
require(app.index('auto frame=vulkan_.begin_frame();') < app.index('renderer_.prepare_static_frame(') <
        app.index('vulkan_.begin_render_pass(*frame);') < app.index('renderer_.render('),
        'transfers must precede rendering')
prepare=renderer.split('void TerrainRenderer::prepare_static_frame(',1)[1].split('void TerrainRenderer::update_dynamic_objects(',1)[0]
require('context_.wait_for_render_frame(' not in prepare,'redundant slot wait')
require('immediate_submit' not in prepare and 'upload_image_array(' not in prepare and
        'upload_object_texture_changes(' not in prepare,'blocking uploader in async path')
require('destroy_buffer(object_texture_frame_staging_[frame_index]);' in prepare,
        'staging not retained until next frame fence')
require('create_async_object_frame_image(' in prepare and 'record_async_object_texture_copy(' in prepare,
        'new and partial uploads do not use async queue')
copy=renderer.split('void TerrainRenderer::record_async_object_texture_copy(',1)[1].split('void TerrainRenderer::prepare_static_frame(',1)[0]
require('VK_IMAGE_LAYOUT_UNDEFINED' in copy and 'VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL' in copy,
        'first-use and partial texture layouts missing')
require('vkCmdCopyBufferToImage(command,staging.buffer' in copy,'missing queued copy')
require('vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT' in copy,'fragment read barrier missing')
require('context_.immediate_submit' not in copy and 'vkWaitForFences' not in copy,
        'copy recorder should not wait')
require('object_texture_frame_staging_' in header,'per-frame staging storage missing')
require('object_async_transfer_copies' in log,'missing async upload metric')
print('RENDER_ASYNC_TRANSFER_SOURCE_CONTRACT PASS frame acquire copy before pass barriers staging retirement')
