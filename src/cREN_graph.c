#include "cREN_graph.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum GraphResourceKind {
  RES_TRANSIENT,
  RES_SWAPCHAIN
} GraphResourceKind;

typedef enum GraphUseKind {
  USE_COLOR_WRITE,
  USE_DEPTH_WRITE,
  USE_SAMPLED_READ
} GraphUseKind;

typedef struct GraphResource {
  GraphResourceKind kind;
  char *name;
  cREN_GraphImageDesc desc;  // transient only; desc.name is cleared, `name` owns the string
  cREN_Format format;        // transient: desc.format, swapchain: queried at import
  cREN_Swapchain *swapchain; // swapchain only
  uint32_t usage;            // cREN_ImageUsage flags, ORed together from the declared uses
  cREN_Image *image;         // filled in by compile (next step)
  cREN_ImageView *view;
  uint32_t bindless_slot;
} GraphResource;

typedef struct GraphUse {
  cREN_GraphImage image;
  GraphUseKind kind;
  cREN_LoadOp load;
  cREN_ClearColor clear;
  float clear_depth;
} GraphUse;

typedef struct GraphPass {
  char *name;
  cREN_PassFn fn;
  void *user;
  GraphUse *uses;
  uint32_t use_count, use_cap;
} GraphPass;

struct cREN_Graph {
  cREN_Context *ctx;
  cREN_Device *device;
  cREN_Allocator *allocator;
  cREN_Bindless *bindless;

  GraphResource *resources;
  uint32_t resource_count, resource_cap;
  GraphPass *passes;
  uint32_t pass_count, pass_cap;
};

static void *fn_reserve(void *array, uint32_t *cap, uint32_t needed, size_t elem_size) {
  if (needed <= *cap) {
    return array;
  }

  uint32_t new_cap = *cap != 0 ? *cap : 8;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  void *grown = realloc(array, (size_t)new_cap * elem_size);
  if (grown != NULL) {
    *cap = new_cap;
  }
  return grown;
}

static char *fn_copy_string(const char *s) {
  const char *src = s != NULL ? s : "unnamed";
  const size_t n = strlen(src) + 1;
  char *copy = malloc(n);
  if (copy != NULL) {
    memcpy(copy, src, n);
  }
  return copy;
}

static bool fn_is_depth_format(cREN_Format f) {
  return f == CREN_FORMAT_D32_FLOAT || f == CREN_FORMAT_D32_FLOAT_S8_UINT || f == CREN_FORMAT_D24_UNORM_S8_UINT;
}

static GraphResource *fn_resource(const cREN_Graph *g, cREN_GraphImage h) {
  return (h.id != 0 && h.id <= g->resource_count) ? &g->resources[h.id - 1] : NULL;
}

static GraphPass *fn_pass(const cREN_Graph *g, cREN_GraphPass h) {
  return (h.id != 0 && h.id <= g->pass_count) ? &g->passes[h.id - 1] : NULL;
}

static cREN_GraphImage fn_add_resource(cREN_Graph *g, GraphResource res) {
  GraphResource *grown = fn_reserve(g->resources, &g->resource_cap, g->resource_count + 1, sizeof(*grown));
  if (grown == NULL) {
    free(res.name);
    return (cREN_GraphImage){0};
  }
  g->resources = grown;
  g->resources[g->resource_count++] = res;
  return (cREN_GraphImage){g->resource_count};
}

static bool fn_add_use(cREN_Graph *g, cREN_GraphPass pass_h, GraphUse use) {
  GraphPass *pass = g != NULL ? fn_pass(g, pass_h) : NULL;
  GraphResource *res = g != NULL ? fn_resource(g, use.image) : NULL;
  if (pass == NULL || res == NULL) {
    fprintf(stderr, "[cREN] Graph: invalid pass or image handle\n");
    return false;
  }

  // a pass may touch an image only once: reading and writing the same image is a feedback loop
  for (uint32_t i = 0; i < pass->use_count; ++i) {
    if (pass->uses[i].image.id == use.image.id) {
      fprintf(stderr, "[cREN] Graph pass '%s' uses image '%s' twice\n", pass->name, res->name);
      return false;
    }
  }

  // early validation, so mistakes surface at the declaration and not at compile
  const bool depth_format = fn_is_depth_format(res->format);
  const char *problem = NULL;
  switch (use.kind) {
  case USE_COLOR_WRITE:
    if (depth_format) problem = "is a depth format and cannot be a color attachment";
    break;
  case USE_DEPTH_WRITE:
    if (res->kind != RES_TRANSIENT)
      problem = "is a swapchain and cannot be a depth attachment";
    else if (!depth_format)
      problem = "is not a depth format";
    break;
  case USE_SAMPLED_READ:
    if (res->kind != RES_TRANSIENT) problem = "is a swapchain and cannot be sampled";
    break;
  }
  if (problem != NULL) {
    fprintf(stderr, "[cREN] Graph pass '%s': image '%s' %s\n", pass->name, res->name, problem);
    return false;
  }

  GraphUse *grown = fn_reserve(pass->uses, &pass->use_cap, pass->use_count + 1, sizeof(*grown));
  if (grown == NULL) {
    return false;
  }
  pass->uses = grown;
  pass->uses[pass->use_count++] = use;

  // the graph, not the caller, decides the image's usage flags
  if (res->kind == RES_TRANSIENT) {
    switch (use.kind) {
    case USE_COLOR_WRITE:
      res->usage |= CREN_IMAGE_COLOR_ATTACHMENT;
      break;
    case USE_DEPTH_WRITE:
      res->usage |= CREN_IMAGE_DEPTH_ATTACHMENT;
      break;
    case USE_SAMPLED_READ:
      res->usage |= CREN_IMAGE_SAMPLED;
      break;
    }
  }
  return true;
}

cREN_Graph *cREN_graph_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Bindless *bindless) {
  if (ctx == NULL || device == NULL || allocator == NULL) {
    return NULL;
  }

  cREN_Graph *g = calloc(1, sizeof(*g));
  if (g == NULL) {
    return NULL;
  }

  g->ctx = ctx;
  g->device = device;
  g->allocator = allocator;
  g->bindless = bindless; // may be NULL if no pass samples anything
  return g;
}

void cREN_graph_destroy(cREN_Graph *g) {
  if (g == NULL) {
    return;
  }

  for (uint32_t i = 0; i < g->resource_count; ++i) {
    GraphResource *r = &g->resources[i];
    if (r->bindless_slot != CREN_BINDLESS_INVALID && g->bindless != NULL) {
      cREN_bindless_free_image(g->bindless, CREN_VIEW_2D, r->bindless_slot);
    }
    cREN_image_view_destroy(g->ctx, g->device, r->view);           // NULL-safe
    cREN_image_destroy(g->ctx, g->device, g->allocator, r->image); // NULL-safe
    free(r->name);
  }
  for (uint32_t i = 0; i < g->pass_count; ++i) {
    free(g->passes[i].name);
    free(g->passes[i].uses);
  }
  free(g->resources);
  free(g->passes);
  free(g);
}

cREN_GraphImage cREN_graph_create_image(cREN_Graph *graph, const cREN_GraphImageDesc *desc) {
  const cREN_GraphImage none = {0};
  if (graph == NULL || desc == NULL || desc->format == CREN_FORMAT_UNDEFINED) {
    return none;
  }

  if (desc->size_mode == CREN_GRAPH_SIZE_RELATIVE) {
    const GraphResource *ref = fn_resource(graph, desc->relative_to);
    if (ref == NULL || ref->kind != RES_SWAPCHAIN) {
      fprintf(stderr, "[cREN] Graph image '%s': relative_to must be an imported swapchain\n", desc->name != NULL ? desc->name : "unnamed");
      return none;
    }
  } else if (desc->width == 0 || desc->height == 0) {
    fprintf(stderr, "[cREN] Graph image '%s': absolute size must be non-zero\n", desc->name != NULL ? desc->name : "unnamed");
    return none;
  }

  GraphResource res = {
      .kind = RES_TRANSIENT,
      .name = fn_copy_string(desc->name),
      .desc = *desc,
      .format = desc->format,
      .bindless_slot = CREN_BINDLESS_INVALID,
  };
  res.desc.name = NULL;
  if (res.name == NULL) {
    return none;
  }
  return fn_add_resource(graph, res);
}

cREN_GraphImage cREN_graph_import_swapchain(cREN_Graph *graph, cREN_Swapchain *swapchain) {
  if (graph == NULL || swapchain == NULL) {
    return (cREN_GraphImage){0};
  }
  GraphResource res = {
      .kind = RES_SWAPCHAIN,
      .name = fn_copy_string("swapchain"),
      .format = cREN_swapchain_format(swapchain),
      .swapchain = swapchain,
      .bindless_slot = CREN_BINDLESS_INVALID,
  };
  if (res.name == NULL) {
    return (cREN_GraphImage){0};
  }
  return fn_add_resource(graph, res);
}

cREN_GraphPass cREN_graph_pass_add(cREN_Graph *graph, const char *name, cREN_PassFn record, void *user) {
  const cREN_GraphPass none = {0};
  if (graph == NULL || record == NULL) {
    return none;
  }
  GraphPass *grown = fn_reserve(graph->passes, &graph->pass_cap, graph->pass_count + 1, sizeof(*grown));
  if (grown == NULL) {
    return none;
  }
  graph->passes = grown;

  GraphPass pass = {.name = fn_copy_string(name), .fn = record, .user = user};
  if (pass.name == NULL) {
    return none;
  }
  graph->passes[graph->pass_count++] = pass;
  return (cREN_GraphPass){graph->pass_count};
}

bool cREN_graph_pass_write_color(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image, cREN_LoadOp load, cREN_ClearColor clear) {
  return fn_add_use(graph, pass, (GraphUse){.image = image, .kind = USE_COLOR_WRITE, .load = load, .clear = clear});
}

bool cREN_graph_pass_write_depth(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image, cREN_LoadOp load, float clear_depth) {
  return fn_add_use(graph, pass, (GraphUse){.image = image, .kind = USE_DEPTH_WRITE, .load = load, .clear_depth = clear_depth});
}

bool cREN_graph_pass_read_sampled(cREN_Graph *graph, cREN_GraphPass pass, cREN_GraphImage image) {
  return fn_add_use(graph, pass, (GraphUse){.image = image, .kind = USE_SAMPLED_READ});
}

void cREN_graph_dump(const cREN_Graph *graph) {
  if (graph == NULL) {
    return;
  }
  fprintf(stderr, "[cREN] Graph: %u images, %u passes\n", graph->resource_count, graph->pass_count);
  for (uint32_t i = 0; i < graph->resource_count; ++i) {
    const GraphResource *r = &graph->resources[i];
    fprintf(stderr, "  image %u '%s' (%s) usage=0x%x\n", i, r->name, r->kind == RES_SWAPCHAIN ? "swapchain" : "transient", r->usage);
  }
  for (uint32_t p = 0; p < graph->pass_count; ++p) {
    const GraphPass *pass = &graph->passes[p];
    fprintf(stderr, "  pass %u '%s'\n", p, pass->name);
    for (uint32_t u = 0; u < pass->use_count; ++u) {
      const GraphUse *use = &pass->uses[u];
      const char *kind = use->kind == USE_COLOR_WRITE   ? "writes color"
                         : use->kind == USE_DEPTH_WRITE ? "writes depth"
                                                        : "reads sampled";
      fprintf(stderr, "    %s '%s'\n", kind, graph->resources[use->image.id - 1].name);
    }
  }
}
