#pragma once

#include "cREN_allocator.h"
#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_types.h"

#include <stdint.h>

typedef struct cREN_Image cREN_Image;

typedef struct cREN_ImageView cREN_ImageView;

typedef struct cREN_ImageDesc {
  uint32_t width;
  uint32_t height;
  cREN_Format format;
  cREN_ImageUsage usage;
  cREN_ImageType type;
  uint32_t depth;        // 3D only, 0 = 1
  uint32_t mip_levels;   // 0 = 1, UINT32_MAX = full chain
  uint32_t array_layers; // 0 = 1, for CUBE this counts as cubes and not faces
} cREN_ImageDesc;

typedef struct cREN_ImageViewDesc {
  cREN_ImageViewType type;
  uint32_t base_mip;
  uint32_t mip_count; // 0 = all remaining
  uint32_t base_layer;
  uint32_t layer_count; // 0 = all remaining. Counts faces for cube views (6 per cube)
} cREN_ImageViewDesc;

cREN_Image *cREN_image_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_ImageDesc *desc);

void cREN_image_destroy(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Image *image);

uint32_t cREN_image_width(const cREN_Image *image);

uint32_t cREN_image_height(const cREN_Image *image);

cREN_Format cREN_image_format(const cREN_Image *image);

cREN_ImageView *cREN_image_view_create(cREN_Context *ctx, cREN_Device *device, const cREN_Image *image, const cREN_ImageViewDesc *desc);

void cREN_image_view_destroy(cREN_Context *ctx, cREN_Device *device, cREN_ImageView *view);

uint32_t cREN_image_mip_levels(const cREN_Image *image);

uint32_t cREN_image_array_layers(const cREN_Image *image);