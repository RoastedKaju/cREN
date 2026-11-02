#include "cREN_buffer.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static VkBufferUsageFlags fn_usage(uint32_t usage) {
  // every buffer gets a device address, since the allocator enables BDA
  VkBufferUsageFlags flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
  if (usage & CREN_BUFFER_VERTEX) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  if (usage & CREN_BUFFER_INDEX) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  if (usage & CREN_BUFFER_STORAGE) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  if (usage & CREN_BUFFER_UNIFORM) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  if (usage & CREN_BUFFER_INDIRECT) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
  if (usage & CREN_BUFFER_TRANSFER_SRC) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  if (usage & CREN_BUFFER_TRANSFER_DST) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  return flags;
}

cREN_Buffer *cREN_buffer_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, const cREN_BufferDesc *desc) {
  if (ctx == NULL || device == NULL || allocator == NULL || desc == NULL || desc->size == 0) {
    return NULL;
  }

  cREN_Buffer *buffer = calloc(1, sizeof(*buffer));
  if (buffer == NULL) {
    return NULL;
  }

  VkBufferCreateInfo buffer_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = desc->size,
      .usage = fn_usage(desc->usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};
  switch (desc->memory) {
  case CREN_MEMORY_UPLOAD:
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    break;
  case CREN_MEMORY_READBACK:
    alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    break;
  case CREN_MEMORY_GPU_ONLY:
  default:
    break;
  }

  VmaAllocationInfo info = {0};
  if (vmaCreateBuffer(allocator->handle, &buffer_info, &alloc_info, &buffer->handle, &buffer->allocation, &info) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Buffer creation failed (%llu bytes)\n", (unsigned long long)desc->size);
    free(buffer);
    return NULL;
  }

  buffer->size = desc->size;
  buffer->usage = desc->usage;
  buffer->mapped = info.pMappedData; // NULL when the memory was not mapped

  VkBufferDeviceAddressInfo address_info = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = buffer->handle,
  };
  buffer->address = vkGetBufferDeviceAddress(device->logical, &address_info);

  return buffer;
}

void cREN_buffer_destroy(cREN_Context *ctx, cREN_Allocator *allocator, cREN_Buffer *buffer) {
  if (ctx == NULL || allocator == NULL || buffer == NULL) {
    return;
  }
  vmaDestroyBuffer(allocator->handle, buffer->handle, buffer->allocation);
  free(buffer);
}

void *cREN_buffer_mapped(const cREN_Buffer *buffer) {
  return buffer != NULL ? buffer->mapped : NULL;
}

uint64_t cREN_buffer_address(const cREN_Buffer *buffer) {
  return buffer != NULL ? buffer->address : 0;
}

uint64_t cREN_buffer_size(const cREN_Buffer *buffer) {
  return buffer != NULL ? buffer->size : 0;
}

bool cREN_buffer_upload(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Buffer *dst, uint64_t offset, const void *data, uint64_t size) {
  if (ctx == NULL || device == NULL || allocator == NULL || dst == NULL || data == NULL || size == 0) {
    return false;
  }

  if (offset > dst->size || size > dst->size - offset) {
    fprintf(stderr, "[cREN] Upload out of range\n");
    return false;
  }

  // host-visible destination: no GPU copy needed
  if (dst->mapped != NULL) {
    memcpy((uint8_t *)dst->mapped + offset, data, (size_t)size);
    vmaFlushAllocation(allocator->handle, dst->allocation, offset, size);
    return true;
  }

  if (!(dst->usage & CREN_BUFFER_TRANSFER_DST)) {
    fprintf(stderr, "[cREN] Upload destination lacks CREN_BUFFER_TRANSFER_DST\n");
    return false;
  }

  bool ok = false;
  VkCommandPool pool = VK_NULL_HANDLE;

  // staging buffer
  cREN_BufferDesc staging_desc = {
      .size = size,
      .usage = CREN_BUFFER_TRANSFER_SRC,
      .memory = CREN_MEMORY_UPLOAD,
  };

  cREN_Buffer *staging = cREN_buffer_create(ctx, device, allocator, &staging_desc);
  if (staging == NULL || staging->mapped == NULL) {
    goto cleanup;
  }
  memcpy(staging->mapped, data, (size_t)size);
  vmaFlushAllocation(allocator->handle, staging->allocation, 0, size);

  // one shot command buffer
  VkCommandPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = device->universal_family,
  };
  if (vkCreateCommandPool(device->logical, &pool_info, NULL, &pool) != VK_SUCCESS) {
    goto cleanup;
  }

  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
  };
  if (vkAllocateCommandBuffers(device->logical, &alloc_info, &cmd) != VK_SUCCESS) {
    goto cleanup;
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
    goto cleanup;
  }

  VkBufferCopy region = {.srcOffset = 0, .dstOffset = offset, .size = size};
  vkCmdCopyBuffer(cmd, staging->handle, dst->handle, 1, &region);

  // make the copy visible to whatever uses the buffer next
  VkBufferMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .buffer = dst->handle,
      .offset = offset,
      .size = size,
  };
  VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .bufferMemoryBarrierCount = 1,
      .pBufferMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cmd, &dependency);

  if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
    goto cleanup;
  }

  // submit, signaling the next timeline value
  const uint64_t signal_value = device->timeline_value + 1;
  VkCommandBufferSubmitInfo cb = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = cmd,
  };
  VkSemaphoreSubmitInfo signal = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = device->timeline,
      .value = signal_value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  };
  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .commandBufferInfoCount = 1,
      .pCommandBufferInfos = &cb,
      .signalSemaphoreInfoCount = 1,
      .pSignalSemaphoreInfos = &signal,
  };
  if (vkQueueSubmit2(device->universal_queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
    goto cleanup;
  }
  device->timeline_value = signal_value;

  // wait, so the staging buffer and pool can be freed right away
  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &device->timeline,
      .pValues = &signal_value,
  };
  ok = vkWaitSemaphores(device->logical, &wait_info, UINT64_MAX) == VK_SUCCESS;

cleanup:
  if (pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device->logical, pool, NULL); // also frees the command buffer
  }
  cREN_buffer_destroy(ctx, allocator, staging); // NULL-safe
  if (!ok) {
    fprintf(stderr, "[cREN] Buffer upload failed\n");
  }
  return ok;
}
