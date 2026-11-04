#pragma once

#include "cREN_allocator.h"
#include "cREN_bindless.h"
#include "cREN_cmd.h"
#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_image.h"
#include "cREN_swapchain.h"
#include "cREN_types.h"

typedef struct cREN_Graph cREN_Graph;

typedef struct cREN_GraphImage {
  uint32_t id;
} cREN_GraphImage;

typedef struct cREN_GraphPass {
  uint32_t id;
} cREN_GraphPass;

typedef void (*cREN_PassFn)(cREN_CmdBuffer *cmd, cREN_Graph *graph, void *user);

typedef struct cREN_GraphImageDesc {
  const char *name;
  cREN_Format format;
  cREN_GraphSizeMode size_mode;
  uint32_t width; // ABSOLUTE only
  uint32_t height;
  cREN_GraphImage relative_to; // RELATIVE only: must be an imported swapchain
  float scale;                 // RELATIVE only, 0 = 1.0
} cREN_GraphImageDesc;

cREN_Graph *cREN_graph_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Bindless *bindless);

/* GPU must be idle*/
void cREN_graph_destroy(cREN_Graph *graph);

/* resources */
cREN_GraphImage cREN_graph_create_image(cREN_Graph *graph, const cREN_GraphImageDesc *desc);

cREN_GraphImage cREN_graph_import_swapchain(cREN_Graph *graph, cREN_Swapchain *swapchain);

/* passes */
cREN_GraphPass cREN_graph_pass_add(cREN_Graph *graph, const char *name, cREN_PassFn record, void *user);

bool cREN_graph_pass_write_color(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image, cREN_LoadOp load, cREN_ClearColor clear);

bool cREN_graph_pass_write_depth(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image, cREN_LoadOp load, float clear_depth);

bool cREN_graph_pass_read_sampled(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image);

void cREN_graph_dump(const cREN_Graph *graph);