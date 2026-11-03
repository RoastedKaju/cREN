#pragma once

#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_types.h"

typedef struct cREN_Sampler cREN_Sampler;

typedef struct cREN_SamplerDesc {
  cREN_Filter min_filter;
  cREN_Filter mag_filter;
  cREN_Filter mip_filter;
  cREN_AddressMode address_u;
  cREN_AddressMode address_v;
  cREN_AddressMode address_w;
  float max_anisotropy; // 0 = off, clamped to the device limit
  cREN_CompareOp compare;
  float min_lod;
  float max_lod; // 0 = unclamped (all mips)
  cREN_BorderColor border;
} cREN_SamplerDesc;

cREN_Sampler *cREN_sampler_create(cREN_Context *ctx, cREN_Device *device, const cREN_SamplerDesc *desc);

void cREN_sampler_destroy(cREN_Context *ctx, cREN_Device *device, cREN_Sampler *sampler);