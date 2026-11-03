#pragma once

#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_image.h"
#include "cREN_sampler.h"

typedef struct cREN_Bindless cREN_Bindless;

typedef struct cREN_BindlessDesc {
  uint32_t max_images;   // per view type (2D, 2D array, cube, cube array, 3D). 0 = 4096
  uint32_t max_samplers; // 0 = 256
} cREN_BindlessDesc;

cREN_Bindless *cREN_bindless_init(cREN_Context *ctx, cREN_Device *device, const cREN_BindlessDesc *desc);

void cREN_bindless_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Bindless *bindless);

/* returns a slot index in the table that matches the view's type or CREN_BINDLESS_INVALID */
uint32_t cREN_bindless_add_image(cREN_Bindless *bindless, const cREN_ImageView *view);

uint32_t cREN_bindless_add_sampler(cREN_Bindless *bindless, const cREN_Sampler *sampler);

/* slots are per table, so freeing needs the view type it was added with. */
void cREN_bindless_free_image(cREN_Bindless *bindless, cREN_ImageViewType type, uint32_t index);

void cREN_bindless_free_sampler(cREN_Bindless *bindless, uint32_t index);