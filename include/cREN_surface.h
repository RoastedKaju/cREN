#pragma once

#include "cREN_context.h"
#include "cREN_types.h"

#include <stdbool.h>

/* forward declared surface struct */
typedef struct cREN_Surface cREN_Surface;

/* surface descriptor */
typedef struct cREN_SurfaceDesc {
  void *native_window;
  void *user_data;
  cREN_SurfaceCreateFn create_fn;
  cREN_SurfaceDestroyFn destroy_fn;
} cREN_SurfaceDesc;

cREN_Surface *cREN_surface_init(cREN_Context *ctx, cREN_SurfaceDesc desc);

void cREN_surface_deinit(cREN_Context *ctx, cREN_Surface *surface);