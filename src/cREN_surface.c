#include "cREN_surface.h"
#include "cREN_internal.h"

#include <stdlib.h>

cREN_Surface *cREN_surface_init(cREN_Context *ctx, cREN_SurfaceDesc desc) {
  if (ctx == NULL || desc.native_window == NULL || ctx->instance == VK_NULL_HANDLE) {
    return NULL;
  }

  if (desc.create_fn == NULL || desc.destroy_fn == NULL) {
    return NULL;
  }

  cREN_Surface *surface = malloc(sizeof(cREN_Surface));
  if (surface == NULL) {
    return NULL;
  }

  surface->handle = NULL;
  surface->user_data = desc.user_data;
  surface->create_fn = desc.create_fn;
  surface->destroy_fn = desc.destroy_fn;

  if (!surface->create_fn((void *)ctx->instance, desc.native_window, desc.user_data, &surface->handle)) {
    free(surface);
    return NULL;
  }

  return surface;
}

void cREN_surface_deinit(cREN_Context *ctx, cREN_Surface *surface) {
  if (ctx == NULL || surface == NULL) {
    return;
  }

  if (surface->handle != NULL && surface->destroy_fn != NULL) {
    surface->destroy_fn((void *)ctx->instance, surface->handle, surface->user_data);
  }

  free(surface);
}
