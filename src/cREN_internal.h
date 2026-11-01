#pragma once

#include "cREN_types.h"

#include <volk.h>

typedef struct cREN_Context {
  uint32_t api_ver;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
} cREN_Context;

typedef struct cREN_Surface {
  void *handle;
  cREN_SurfaceCreateFn create_fn;
  cREN_SurfaceDestroyFn destroy_fn;
  void *user_data;
} cREN_Surface;

typedef struct cREN_Device {
  VkPhysicalDevice physical;
  VkPhysicalDeviceProperties props;
  VkDevice logical;
  uint32_t universal_family;
  VkQueue universal_queue;
} cREN_Device;

typedef struct cREN_Swapchain {
  VkSwapchainKHR handle;
  VkSurfaceKHR surface;
} cREN_Swapchain;