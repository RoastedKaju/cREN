#pragma once

#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_surface.h"

#include <stdbool.h>
#include <stdint.h>

// TODO: Eventually remove context parameter from resize and deinit and store the context in swapchain itself
// TODO: Pass the descriptors using const * as well as it will be good later on when desiging ABI for C#
// TODO: Find out if possible to decouple window extent from swapchain extent

typedef struct cREN_Swapchain cREN_Swapchain;

typedef struct cREN_SwapchainDesc {
  uint32_t width;
  uint32_t height;
  bool vsync;
} cREN_SwapchainDesc;

cREN_Swapchain *cREN_swapchain_init(cREN_Context *ctx, cREN_Device *device, cREN_Surface *surface, const cREN_SwapchainDesc *const desc);

bool cREN_swapchain_resize(cREN_Context *ctx, cREN_Device *device, cREN_Swapchain *sc, uint32_t width, uint32_t height);

void cREN_swapchain_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Swapchain *sc);