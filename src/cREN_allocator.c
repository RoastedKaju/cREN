#include "cREN_allocator.h"
#include "cREN_internal.h"

#include <stdlib.h>

cREN_Allocator *cREN_allocator_init(const cREN_Context *const ctx, const cREN_Device *const device) {
  if (!ctx || !device) { return NULL; }

  VmaVulkanFunctions fns = {0};
  fns.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
  fns.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

  VmaAllocatorCreateInfo info = {
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = device->physical,
      .device = device->logical,
      .instance = ctx->instance,
      .vulkanApiVersion = ctx->api_ver,
      .pVulkanFunctions = &fns,
  };

  cREN_Allocator *alloc = calloc(1, sizeof(*alloc));
  if (!alloc) {
    return NULL;
  }

  if (vmaCreateAllocator(&info, &alloc->handle) != VK_SUCCESS) {
    return NULL;
  }

  return alloc;
}

void cREN_allocator_deinit(cREN_Allocator *allocator) {
  if (!allocator) { return; }

  vmaDestroyAllocator(allocator->handle);
  allocator->handle = VK_NULL_HANDLE;
  
  free(allocator);
}
