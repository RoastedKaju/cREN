#include "cREN_image.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t fn_mip_count(uint32_t w, uint32_t h, uint32_t d) {
  uint32_t m = w > h ? w : h;
  if (d > m) m = d;
  uint32_t n = 1;
  while (m > 1) {
    m >>= 1;
    ++n;
  }
  return n;
}

static bool fn_has_depth(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

static bool fn_has_stencil(VkFormat format) {
  return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

static VkImageUsageFlags fn_usage(uint32_t usage) {
  VkImageUsageFlags flags = 0;
  if (usage & CREN_IMAGE_COLOR_ATTACHMENT) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (usage & CREN_IMAGE_DEPTH_ATTACHMENT) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  if (usage & CREN_IMAGE_SAMPLED) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
  if (usage & CREN_IMAGE_STORAGE) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
  if (usage & CREN_IMAGE_TRANSFER_SRC) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (usage & CREN_IMAGE_TRANSFER_DST) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  return flags;
}

cREN_Image *cREN_image_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_ImageDesc *desc) {
  if (ctx == NULL || device == NULL || allocator == NULL || desc == NULL ||
      desc->width == 0 || desc->height == 0 || desc->usage == 0) {
    return NULL;
  }
  const VkFormat format = fn_format_to_vk(desc->format);
  if (format == VK_FORMAT_UNDEFINED) {
    fprintf(stderr, "[cREN] Image format is undefined or unsupported\n");
    return NULL;
  }
  const bool depth_format = fn_has_depth(format);
  const bool attachment = (desc->usage & (CREN_IMAGE_COLOR_ATTACHMENT | CREN_IMAGE_DEPTH_ATTACHMENT)) != 0;
  if (((desc->usage & CREN_IMAGE_DEPTH_ATTACHMENT) && !depth_format) || ((desc->usage & CREN_IMAGE_COLOR_ATTACHMENT) && depth_format)) {
    fprintf(stderr, "[cREN] Image usage does not match its format\n");
    return NULL;
  }

  const cREN_ImageType type = desc->type;
  const uint32_t depth_px = (type == CREN_IMAGE_TYPE_3D && desc->depth != 0) ? desc->depth : 1;
  uint32_t layers = desc->array_layers != 0 ? desc->array_layers : 1;
  if (type == CREN_IMAGE_TYPE_3D && layers != 1) {
    fprintf(stderr, "[cREN] 3D images cannot be arrays\n");
    return NULL;
  }
  if (type == CREN_IMAGE_TYPE_CUBE) {
    if (desc->width != desc->height) {
      fprintf(stderr, "[cREN] Cube images must be square\n");
      return NULL;
    }
    layers *= 6;
  }
  if (attachment && type != CREN_IMAGE_TYPE_2D) {
    // attachments to cube faces / 3D slices need per-layer views, which come with the render graph step
    fprintf(stderr, "[cREN] Only 2D images can be render attachments for now\n");
    return NULL;
  }

  const uint32_t max_mips = fn_mip_count(desc->width, desc->height, depth_px);
  uint32_t mips = 1;
  if (desc->mip_levels == UINT32_MAX) {
    mips = max_mips;
  } else if (desc->mip_levels != 0) {
    if (desc->mip_levels > max_mips) {
      fprintf(stderr, "[cREN] Too many mip levels (%u, max %u)\n", desc->mip_levels, max_mips);
      return NULL;
    }
    mips = desc->mip_levels;
  }

  cREN_Image *image = calloc(1, sizeof(*image)); // state starts as UNDEFINED
  if (image == NULL) {
    return NULL;
  }
  image->format = format;
  image->extent = (VkExtent2D){desc->width, desc->height};
  image->depth = depth_px;
  image->mip_levels = mips;
  image->array_layers = layers;
  image->type = type;
  image->aspect = depth_format ? (VK_IMAGE_ASPECT_DEPTH_BIT | (fn_has_stencil(format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0))
                               : VK_IMAGE_ASPECT_COLOR_BIT;

  VkImageCreateInfo image_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .flags = type == CREN_IMAGE_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
      .imageType = type == CREN_IMAGE_TYPE_3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
      .format = format,
      .extent = {desc->width, desc->height, depth_px},
      .mipLevels = mips,
      .arrayLayers = layers,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = fn_usage(desc->usage),
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };

  VmaAllocationCreateInfo alloc_info = {.usage = VMA_MEMORY_USAGE_AUTO};
  if (attachment) {
    alloc_info.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
  }

  if (vmaCreateImage(allocator->handle, &image_info, &alloc_info, &image->handle, &image->allocation, NULL) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] vmaCreateImage failed (%ux%u)\n", desc->width, desc->height);
    free(image);
    return NULL;
  }

  // default view: the whole image, except attachments get mip 0 / layer 0 as a plain 2D view
  VkImageViewType view_type = VK_IMAGE_VIEW_TYPE_2D;
  uint32_t view_mips = mips, view_layers = layers;
  if (attachment) {
    view_mips = 1;
    view_layers = 1;
  } else if (type == CREN_IMAGE_TYPE_3D) {
    view_type = VK_IMAGE_VIEW_TYPE_3D;
  } else if (type == CREN_IMAGE_TYPE_CUBE) {
    view_type = layers > 6 ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_CUBE;
  } else if (layers > 1) {
    view_type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
  }

  VkImageViewCreateInfo view_info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image->handle,
      .viewType = view_type,
      .format = format,
      .subresourceRange = {
          .aspectMask = depth_format ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = view_mips,
          .layerCount = view_layers,
      },
  };
  if (vkCreateImageView(device->logical, &view_info, NULL, &image->view) != VK_SUCCESS) {
    vmaDestroyImage(allocator->handle, image->handle, image->allocation);
    free(image);
    return NULL;
  }
  return image;
}

void cREN_image_destroy(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Image *image) {
  if (ctx == NULL || device == NULL || allocator == NULL || image == NULL) {
    return;
  }
  vkDestroyImageView(device->logical, image->view, NULL);
  vmaDestroyImage(allocator->handle, image->handle, image->allocation);
  free(image);
}

uint32_t cREN_image_width(const cREN_Image *image) {
  return image != NULL ? image->extent.width : 0;
}

uint32_t cREN_image_height(const cREN_Image *image) {
  return image != NULL ? image->extent.height : 0;
}

cREN_Format cREN_image_format(const cREN_Image *image) {
  return image != NULL ? fn_format_from_vk(image->format) : CREN_FORMAT_UNDEFINED;
}

cREN_ImageView *cREN_image_view_create(cREN_Context *ctx, cREN_Device *device, const cREN_Image *image, const cREN_ImageViewDesc *desc) {
  if (ctx == NULL || device == NULL || image == NULL || desc == NULL) {
    return NULL;
  }

  const uint32_t mips = desc->mip_count != 0 ? desc->mip_count : image->mip_levels - desc->base_mip;
  const uint32_t layers = desc->layer_count != 0 ? desc->layer_count : image->array_layers - desc->base_layer;
  if (desc->base_mip >= image->mip_levels || desc->base_mip + mips > image->mip_levels || desc->base_layer >= image->array_layers || desc->base_layer + layers > image->array_layers) {
    fprintf(stderr, "[cREN] Image view range is out of bounds\n");
    return NULL;
  }

  VkImageViewType type;
  switch (desc->type) {
  case CREN_VIEW_2D:
    type = VK_IMAGE_VIEW_TYPE_2D;
    break;
  case CREN_VIEW_2D_ARRAY:
    type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    break;
  case CREN_VIEW_CUBE:
    type = VK_IMAGE_VIEW_TYPE_CUBE;
    break;
  case CREN_VIEW_CUBE_ARRAY:
    type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    break;
  case CREN_VIEW_3D:
    type = VK_IMAGE_VIEW_TYPE_3D;
    break;
  default:
    return NULL;
  }
  const bool cube = type == VK_IMAGE_VIEW_TYPE_CUBE || type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
  if (cube && (image->type != CREN_IMAGE_TYPE_CUBE || layers % 6 != 0 || (type == VK_IMAGE_VIEW_TYPE_CUBE && layers != 6))) {
    fprintf(stderr, "[cREN] Invalid cube view\n");
    return NULL;
  }
  if ((type == VK_IMAGE_VIEW_TYPE_3D) != (image->type == CREN_IMAGE_TYPE_3D)) {
    fprintf(stderr, "[cREN] View type does not match image type\n");
    return NULL;
  }

  cREN_ImageView *view = calloc(1, sizeof(*view));
  if (view == NULL) {
    return NULL;
  }
  view->type = type;
  view->image = image;

  VkImageViewCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image->handle,
      .viewType = type,
      .format = image->format,
      .subresourceRange = {
          // depth/stencil images are viewed depth-only so they can be sampled
          .aspectMask = (image->aspect & VK_IMAGE_ASPECT_COLOR_BIT) ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT,
          .baseMipLevel = desc->base_mip,
          .levelCount = mips,
          .baseArrayLayer = desc->base_layer,
          .layerCount = layers,
      },
  };
  if (vkCreateImageView(device->logical, &info, NULL, &view->handle) != VK_SUCCESS) {
    free(view);
    return NULL;
  }
  return view;
}

void cREN_image_view_destroy(cREN_Context *ctx, cREN_Device *device, cREN_ImageView *view) {
  if (ctx == NULL || device == NULL || view == NULL) {
    return;
  }
  vkDestroyImageView(device->logical, view->handle, NULL);
  free(view);
}

uint32_t cREN_image_mip_levels(const cREN_Image *image) {
  return image != NULL ? image->mip_levels : 0;
}

uint32_t cREN_image_array_layers(const cREN_Image *image) {
  return image != NULL ? image->array_layers : 0;
}
