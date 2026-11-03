#include "cREN_sampler.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static VkFilter fn_filter(cREN_Filter f) { return f == CREN_FILTER_LINEAR ? VK_FILTER_LINEAR : VK_FILTER_NEAREST; }

static VkSamplerAddressMode fn_address(cREN_AddressMode m) {
  switch (m) {
  case CREN_ADDRESS_MIRRORED_REPEAT:
    return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
  case CREN_ADDRESS_CLAMP_TO_EDGE:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  case CREN_ADDRESS_CLAMP_TO_BORDER:
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
  case CREN_ADDRESS_REPEAT:
  default:
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
  }
}

static VkCompareOp fn_compare(cREN_CompareOp op) {
  switch (op) {
  case CREN_COMPARE_LESS:
    return VK_COMPARE_OP_LESS;
  case CREN_COMPARE_LESS_EQUAL:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case CREN_COMPARE_GREATER:
    return VK_COMPARE_OP_GREATER;
  case CREN_COMPARE_GREATER_EQUAL:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  default:
    return VK_COMPARE_OP_NEVER;
  }
}

static VkBorderColor fn_border(cREN_BorderColor c) {
  switch (c) {
  case CREN_BORDER_OPAQUE_BLACK:
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
  case CREN_BORDER_OPAQUE_WHITE:
    return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  default:
    return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
  }
}

cREN_Sampler *cREN_sampler_create(cREN_Context *ctx, cREN_Device *device, const cREN_SamplerDesc *desc) {
  if (ctx == NULL || device == NULL || desc == NULL) {
    return NULL;
  }

  cREN_Sampler *sampler = calloc(1, sizeof(*sampler));
  if (sampler == NULL) {
    return NULL;
  }

  float anisotropy = desc->max_anisotropy;
  if (anisotropy > device->props.limits.maxSamplerAnisotropy) {
    anisotropy = device->props.limits.maxSamplerAnisotropy;
  }

  VkSamplerCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
      .magFilter = fn_filter(desc->mag_filter),
      .minFilter = fn_filter(desc->min_filter),
      .mipmapMode = desc->mip_filter == CREN_FILTER_LINEAR ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST,
      .addressModeU = fn_address(desc->address_u),
      .addressModeV = fn_address(desc->address_v),
      .addressModeW = fn_address(desc->address_w),
      .anisotropyEnable = anisotropy > 1.0f ? VK_TRUE : VK_FALSE, // the feature is already enabled on the device
      .maxAnisotropy = anisotropy > 1.0f ? anisotropy : 1.0f,
      .compareEnable = desc->compare != CREN_COMPARE_NONE ? VK_TRUE : VK_FALSE,
      .compareOp = fn_compare(desc->compare),
      .minLod = desc->min_lod,
      .maxLod = desc->max_lod > 0.0f ? desc->max_lod : VK_LOD_CLAMP_NONE,
      .borderColor = fn_border(desc->border),
  };
  if (vkCreateSampler(device->logical, &info, NULL, &sampler->handle) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Sampler creation failed\n");
    free(sampler);
    return NULL;
  }
  return sampler;
}

void cREN_sampler_destroy(cREN_Context *ctx, cREN_Device *device, cREN_Sampler *sampler) {
  if (ctx == NULL || device == NULL || sampler == NULL) {
    return;
  }
  vkDestroySampler(device->logical, sampler->handle, NULL);
  free(sampler);
}