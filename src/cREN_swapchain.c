#include "cREN_swapchain.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t fn_clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static VkExtent2D fn_choose_extent(const VkSurfaceCapabilitiesKHR *caps, uint32_t width, uint32_t height) {
  if (caps->currentExtent.width != UINT32_MAX) {
    return caps->currentExtent;
  }

  VkExtent2D extent = {
      .width = fn_clamp_u32(width, caps->minImageExtent.width, caps->maxImageExtent.width),
      .height = fn_clamp_u32(height, caps->minImageExtent.height, caps->maxImageExtent.height),
  };

  return extent;
}

static bool fn_choose_format(VkPhysicalDevice device, VkSurfaceKHR surface, VkSurfaceFormatKHR *out) {
  uint32_t count = 0;
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, NULL) != VK_SUCCESS || count == 0) {
    return false;
  }
  VkSurfaceFormatKHR *formats = malloc(count * sizeof(*formats));
  if (formats == NULL) {
    return false;
  }
  if (vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &count, formats) != VK_SUCCESS) {
    free(formats);
    return false;
  }

  *out = formats[0];
  for (uint32_t i = 0; i < count; ++i) {
    if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB && formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      *out = formats[i];
      break;
    }
  }

  free(formats);
  return true;
}

static VkPresentModeKHR fn_choose_present_mode(VkPhysicalDevice device, VkSurfaceKHR surface, bool vsync) {
  if (vsync) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  uint32_t count = 0;
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, NULL) != VK_SUCCESS || count == 0) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }
  VkPresentModeKHR *modes = malloc(count * sizeof(*modes));
  if (modes == NULL) {
    return VK_PRESENT_MODE_FIFO_KHR;
  }

  VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
  if (vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &count, modes) == VK_SUCCESS) {
    for (uint32_t i = 0; i < count; ++i) {
      if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
        result = VK_PRESENT_MODE_MAILBOX_KHR;
        break;
      }
    }
  }

  free(modes);
  return result;
}

static void fn_destroy_views(VkDevice device, VkImageView *views, uint32_t count) {
  if (views == NULL) {
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (views[i] != VK_NULL_HANDLE) {
      vkDestroyImageView(device, views[i], NULL);
      views[i] = VK_NULL_HANDLE;
    }
  }
  free(views);
}

static void fn_destroy_semaphores(VkDevice device, VkSemaphore *semaphores, uint32_t count) {
  if (semaphores == NULL) {
    return;
  }

  for (uint32_t i = 0; i < count; ++i) {
    if (semaphores[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, semaphores[i], NULL);
    }
  }
  free(semaphores);
}

/**
 * builds or rebuilds swapchain, on failure swapchain is left untouched and is still valid
 * returns true without doing anything if surface is currently zero-sized
 */
static bool fn_swapchain_build(cREN_Device *device, cREN_Swapchain *sc) {
  VkPhysicalDevice phy_device = device->physical;
  VkDevice logical_device = device->logical;

  // get capabilities
  VkSurfaceCapabilitiesKHR caps;
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phy_device, sc->surface, &caps) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Failed to query surface capabilities\n");
    return false;
  }

  // extent
  VkExtent2D extent = fn_choose_extent(&caps, sc->requested_extent.width, sc->requested_extent.height);
  if (extent.width == 0 || extent.height == 0) {
    return true;
  }

  // format
  VkSurfaceFormatKHR format;
  if (!fn_choose_format(phy_device, sc->surface, &format)) {
    fprintf(stderr, "[cREN] Surface reports no formats\n");
    return false;
  }

  uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount != 0 && image_count > caps.maxImageCount) {
    image_count = caps.maxImageCount;
  }

  // usage
  VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
    fprintf(stderr, "[cREN] Surface does not support color attachment usage\n");
    return false;
  }
  if (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  // composition
  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  if (!(caps.supportedCompositeAlpha & composite)) {
    const VkCompositeAlphaFlagBitsKHR candidates[] = {
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
      if (caps.supportedCompositeAlpha & candidates[i]) {
        composite = candidates[i];
        break;
      }
    }
  }

  // swapchain creation
  VkSwapchainCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = sc->surface,
      .minImageCount = image_count,
      .imageFormat = format.format,
      .imageColorSpace = format.colorSpace,
      .imageExtent = extent,
      .imageArrayLayers = 1,
      .imageUsage = usage,
      .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .preTransform = caps.currentTransform,
      .compositeAlpha = composite,
      .presentMode = fn_choose_present_mode(phy_device, sc->surface, sc->vsync),
      .clipped = VK_TRUE,
      .oldSwapchain = sc->handle,
  };

  VkSwapchainKHR new_handle = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(logical_device, &create_info, NULL, &new_handle) != VK_SUCCESS) {
    return false;
  }

  // images
  uint32_t actual_count = 0;
  vkGetSwapchainImagesKHR(logical_device, new_handle, &actual_count, NULL);
  VkImage *images = malloc(actual_count * sizeof(*images));
  VkImageView *views = calloc(actual_count, sizeof(*views));
  if (images == NULL || views == NULL) {
    free(images);
    free(views);
    vkDestroySwapchainKHR(logical_device, new_handle, NULL);
    return false;
  }
  vkGetSwapchainImagesKHR(logical_device, new_handle, &actual_count, images);

  // semaphores
  VkSemaphore *render_finished = calloc(actual_count, sizeof(*render_finished));
  if (render_finished == NULL) {
    fn_destroy_views(logical_device, views, actual_count);
    free(images);
    vkDestroySwapchainKHR(logical_device, new_handle, NULL);
    return false;
  }

  for (uint32_t i = 0; i < actual_count; ++i) {
    // create image views
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = images[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format.format,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .layerCount = 1,
        },
    };
    if (vkCreateImageView(logical_device, &view_info, NULL, &views[i]) != VK_SUCCESS) {
      fprintf(stderr, "[cREN] Failed to create swapchain image view\n");
      fn_destroy_views(logical_device, views, actual_count);
      free(images);
      vkDestroySwapchainKHR(logical_device, new_handle, NULL);
      return false;
    }
  }
  // per image render finished semaphore
  VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (uint32_t i = 0; i < actual_count; ++i) {
    if (vkCreateSemaphore(logical_device, &sem_info, NULL, &render_finished[i]) != VK_SUCCESS) {
      fn_destroy_semaphores(logical_device, render_finished, actual_count);
      fn_destroy_views(logical_device, views, actual_count);
      free(images);
      vkDestroySwapchainKHR(logical_device, new_handle, NULL);
      return false;
    }
  }

  // retire old swapchain
  VkSwapchainKHR old_handle = sc->handle;
  VkImageView *old_views = sc->views;
  uint32_t old_count = sc->image_count;
  VkImage *old_images = sc->images;
  VkSemaphore *old_render_finished = sc->render_finished;

  if (old_handle != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(logical_device);
    fn_destroy_semaphores(logical_device, old_render_finished, old_count);
    fn_destroy_views(logical_device, old_views, old_count);
    free(old_images);
    vkDestroySwapchainKHR(logical_device, old_handle, NULL);
  }

  sc->handle = new_handle;
  sc->format = format.format;
  sc->color_space = format.colorSpace;
  sc->extent = extent;
  sc->image_count = actual_count;
  sc->images = images;
  sc->views = views;
  sc->needs_resize = false;
  sc->render_finished = render_finished;
  return true;
}

cREN_Swapchain *cREN_swapchain_init(cREN_Context *ctx, cREN_Device *device, cREN_Surface *surface, const cREN_SwapchainDesc *const desc) {
  if (ctx == NULL || device == NULL || surface == NULL || desc == NULL ||
      device->logical == VK_NULL_HANDLE || surface->handle == VK_NULL_HANDLE ||
      desc->frame_count == 0) {
    return NULL;
  }

  VkSurfaceKHR vk_surface = (VkSurfaceKHR)surface->handle;

  VkBool32 present_supported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(device->physical, device->universal_family, vk_surface, &present_supported);
  if (!present_supported) {
    fprintf(stderr, "[cREN] Universal queue family cannot present to this surface\n");
    return NULL;
  }

  cREN_Swapchain *sc = calloc(1, sizeof(*sc));
  if (sc == NULL) {
    return NULL;
  }
  sc->surface = vk_surface;
  sc->requested_extent.width = desc->width;
  sc->requested_extent.height = desc->height;
  sc->vsync = desc->vsync;

  sc->acquires = calloc(desc->frame_count, sizeof(*sc->acquires));
  if (sc->acquires == NULL) {
    free(sc);
    return NULL;
  }
  sc->acquire_count = desc->frame_count;

  VkSemaphoreCreateInfo sem_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (uint32_t i = 0; i < sc->acquire_count; ++i) {
    if (vkCreateSemaphore(device->logical, &sem_info, NULL, &sc->acquires[i]) != VK_SUCCESS) {
      fprintf(stderr, "[cREN] Swapchain creation failed\n");
      fn_destroy_semaphores(device->logical, sc->acquires, sc->acquire_count);
      free(sc);
      return NULL;
    }
  }

  if (!fn_swapchain_build(device, sc) || sc->handle == VK_NULL_HANDLE) {
    fprintf(stderr, "[cREN] Swapchain creation failed\n");
    fn_destroy_semaphores(device->logical, sc->acquires, sc->acquire_count);
    free(sc);
    return NULL;
  }

  fprintf(stderr, "[cREN] Swapchain: %ux%u, %u images\n", sc->extent.width, sc->extent.height, sc->image_count);
  return sc;
}

bool cREN_swapchain_resize(cREN_Context *ctx, cREN_Device *device, cREN_Swapchain *sc, uint32_t width, uint32_t height) {
  if (ctx == NULL || device == NULL || sc == NULL) {
    return false;
  }

  sc->requested_extent.width = width;
  sc->requested_extent.height = height;

  return fn_swapchain_build(device, sc);
}

void cREN_swapchain_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Swapchain *sc) {
  if (ctx == NULL || device == NULL || sc == NULL) {
    return;
  }

  vkDeviceWaitIdle(device->logical);

  fn_destroy_semaphores(device->logical, sc->render_finished, sc->image_count);

  fn_destroy_views(device->logical, sc->views, sc->image_count);

  free(sc->images);

  fn_destroy_semaphores(device->logical, sc->acquires, sc->acquire_count);

  if (sc->handle != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device->logical, sc->handle, NULL);
    sc->handle = VK_NULL_HANDLE;
  }
  free(sc);
}

bool cREN_swapchain_needs_resize(const cREN_Swapchain *sc) {
  return sc != NULL && sc->needs_resize;
}

cREN_Format cREN_swapchain_format(const cREN_Swapchain *sc) {
  return sc != NULL ? fn_format_from_vk(sc->format) : CREN_FORMAT_UNDEFINED;
}
