#include "cREN_device.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fn_has_device_extension(VkPhysicalDevice device, const char *ext_name) {
  uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS || count == 0) {
    return false;
  }

  VkExtensionProperties *props = malloc(count * sizeof(*props));
  if (props == NULL) {
    return false;
  }

  bool found = false;
  if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, props) == VK_SUCCESS) {
    for (uint32_t i = 0; i < count && !found; ++i) {
      found = strcmp(props[i].extensionName, ext_name) == 0;
    }
  }

  free(props);
  return found;
}

static bool fn_find_universal_family(VkPhysicalDevice device, uint32_t *out_family) {
  /**
   * create separate, dedicated functions for individual queues later on:
   * find transfer and find compute
   * universal queue should support all graphics, compute and present
   */
  uint32_t queue_fam_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_fam_count, NULL);
  if (queue_fam_count == 0) {
    return false;
  }
  VkQueueFamilyProperties *queue_families = malloc(queue_fam_count * sizeof(*queue_families));
  if (queue_families == NULL) {
    return false;
  }
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_fam_count, queue_families);

  bool found = false;
  for (uint32_t i = 0; i < queue_fam_count; ++i) {
    bool has_graphics = (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    bool has_compute = (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

    // presentation capability can be verified later when creating specific surfaces
    if (has_graphics && has_compute) {
      *out_family = i;
      found = true;
      break;
    }
  }

  free(queue_families);
  return found;
}

static bool fn_has_required_features(VkPhysicalDevice device, uint32_t api_ver) {
  if (api_ver < VK_API_VERSION_1_3) {
    return false;
  }

  VkPhysicalDeviceVulkan13Features features_13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
  };
  VkPhysicalDeviceVulkan12Features features_12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &features_13,
  };
  VkPhysicalDeviceVulkan11Features features_11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &features_12,
  };
  VkPhysicalDeviceFeatures2 features_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features_11,
  };

  vkGetPhysicalDeviceFeatures2(device, &features_2);

  return features_2.features.samplerAnisotropy &&
         features_11.shaderDrawParameters &&
         features_12.timelineSemaphore &&
         features_12.bufferDeviceAddress &&
         features_12.scalarBlockLayout &&
         features_12.descriptorIndexing &&
         features_12.runtimeDescriptorArray &&
         features_12.descriptorBindingPartiallyBound &&
         features_12.descriptorBindingVariableDescriptorCount &&
         features_12.descriptorBindingUpdateUnusedWhilePending &&
         features_12.descriptorBindingSampledImageUpdateAfterBind &&
         features_12.descriptorBindingStorageBufferUpdateAfterBind &&
         features_12.shaderSampledImageArrayNonUniformIndexing &&
         features_12.shaderStorageBufferArrayNonUniformIndexing &&
         features_13.dynamicRendering &&
         features_13.synchronization2;
}

cREN_Device *cREN_device_init(cREN_Context *ctx) {
  if (ctx == NULL || ctx->instance == VK_NULL_HANDLE) {
    return NULL;
  }

  // enumerate
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
  if (count == 0) {
    fprintf(stderr, "[cREN] No physical devices found\n");
    return NULL;
  }
  VkPhysicalDevice *devices = malloc(count * sizeof(*devices));
  if (devices == NULL) {
    return NULL;
  }
  vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

  // pick the best device
  cREN_Device *device = calloc(1, sizeof(*device));
  if (device == NULL) {
    free(devices);
    return NULL;
  }

  const char *device_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  const uint32_t device_ext_count = 1;

  uint64_t best_score = 0;
  for (uint32_t i = 0; i < count; ++i) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(devices[i], &props);

    uint32_t uni_family;
    if (props.apiVersion < ctx->api_ver) { continue; }
    if (!fn_has_device_extension(devices[i], device_exts[0])) { continue; }
    if (!fn_find_universal_family(devices[i], &uni_family)) { continue; }
    if (!fn_has_required_features(devices[i], ctx->api_ver)) { continue; }

    uint64_t score = 1 + props.limits.maxImageDimension2D;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score += 100000;
    } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score += 1000;
    }

    if (score > best_score) {
      best_score = score;
      device->physical = devices[i];
      device->props = props;
      device->universal_family = uni_family;
    }
  }
  free(devices);

  if (device->physical == VK_NULL_HANDLE) {
    fprintf(stderr, "[cREN] No suitable physical device found\n");
    free(device);
    return NULL;
  }

  fprintf(stderr, "[cREN] Selected GPU: %s\n", device->props.deviceName);

  // queue info
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = device->universal_family,
      .queueCount = 1,
      .pQueuePriorities = &priority,
  };

  // features
  VkPhysicalDeviceVulkan13Features features_13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE,
      .synchronization2 = VK_TRUE,
  };

  VkPhysicalDeviceVulkan12Features features_12 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &features_13,
      .timelineSemaphore = VK_TRUE,
      .bufferDeviceAddress = VK_TRUE,
      .scalarBlockLayout = VK_TRUE,
      .descriptorIndexing = VK_TRUE,
      .runtimeDescriptorArray = VK_TRUE,
      .descriptorBindingPartiallyBound = VK_TRUE,
      .descriptorBindingVariableDescriptorCount = VK_TRUE,
      .descriptorBindingUpdateUnusedWhilePending = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
      .shaderStorageBufferArrayNonUniformIndexing = VK_TRUE,
  };

  VkPhysicalDeviceVulkan11Features features_11 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
      .pNext = &features_12,
      .shaderDrawParameters = VK_TRUE,
  };

  VkPhysicalDeviceFeatures2 features_2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features_11,
      .features = {
          .samplerAnisotropy = VK_TRUE,
      },
  };

  // creation
  VkDeviceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features_2,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &queue_info,
      .enabledExtensionCount = device_ext_count,
      .ppEnabledExtensionNames = device_exts,
      .pEnabledFeatures = NULL,
  };

  if (vkCreateDevice(device->physical, &create_info, NULL, &device->logical) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Failed to create logical device\n");
    free(device);
    return NULL;
  }

  volkLoadDevice(device->logical);

  vkGetDeviceQueue(device->logical, device->universal_family, 0, &device->universal_queue);

  return device;
}

void cREN_device_deinit(cREN_Device *device) {
  if (device == NULL) { return; }

  if (device->logical != VK_NULL_HANDLE) {
    vkDestroyDevice(device->logical, NULL);
  }

  free(device);
}
