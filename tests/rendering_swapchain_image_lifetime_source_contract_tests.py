#!/usr/bin/env python3
"""Guards against sharing depth and present-wait semaphores between in-flight images."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parent.parent
header = (root / 'src/editor/vulkan_context.hpp').read_text()
source = (root / 'src/editor/vulkan_context.cpp').read_text()

def require(predicate, detail):
    if not predicate:
        raise AssertionError(detail)

for name in ('depth_images_', 'depth_memories_', 'depth_views_',
             'render_finished_for_image_'):
    require(f'std::vector<' in header and name in header,
            f'{name} must be owned by the swapchain image array')
    require(f'{name}.resize(image_count, VK_NULL_HANDLE)' in source,
            f'{name} must have one slot per swapchain image')
require('VkImage depth_image_' not in header and 'VkImageView depth_view_' not in header,
        'legacy depth attachment shared across swapchain images')
require('VkSemaphore render_finished' not in header,
        'frame-slot render-finished semaphore is not present-lifetime safe')
require('for (uint32_t i = 0; i < image_count; ++i)' in source,
        'missing per-swapchain resource allocation loop')
for name in ('depth_images_[i]', 'depth_memories_[i]', 'depth_views_[i]',
             'render_finished_for_image_[i]'):
    require(name in source, f'no per-image allocation/lookup for {name}')
require('std::array<VkImageView,2> views{swapchain_views_[i],depth_views_[i]}' in source,
        'multiple framebuffers may still share a writable depth image')
require('const VkSemaphore& render_finished = render_finished_for_image_.at(submitted.image_index)' in source,
        'render-finished semaphore must be indexed by acquired image')
require('submit.pSignalSemaphores=&render_finished' in source and
        'present.pWaitSemaphores=&render_finished' in source,
        'present must wait on the semaphore signaled for that exact image')
require('frame.render_finished' not in source, 'legacy per-frame render-finished synchronization remains')
for name in ('render_finished_for_image_', 'depth_views_', 'depth_images_', 'depth_memories_'):
    require(f'{name}.clear()' in source, f'swapchain destruction does not clear {name}')
require(source.index('for (auto framebuffer : framebuffers_)') <
        source.index('for (auto view : depth_views_)') <
        source.index('for (auto depth : depth_images_)') <
        source.index('for (auto memory : depth_memories_)'),
        'swapchain destruction order violates framebuffer/image lifetime')
require('vkDeviceWaitIdle(device_);' in source and 'destroy_swapchain_resources();' in source,
        'swapchain recreation must wait for prior submissions/presentation')
print('SWAPCHAIN_IMAGE_LIFETIME_PASS')
