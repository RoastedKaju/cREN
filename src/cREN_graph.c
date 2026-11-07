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
  uint32_t width;
  uint32_t height;
  bool written;
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
  uint32_t use_count;
  uint32_t use_cap;
  uint32_t color_first;
  uint32_t color_count;
  uint32_t read_first;
  uint32_t read_count;
  cREN_DepthAttachment depth;
  bool has_depth;
  uint32_t render_w;
  uint32_t render_h;
} GraphPass;

struct cREN_Graph {
  cREN_Context *ctx;
  cREN_Device *device;
  cREN_Allocator *allocator;
  cREN_Bindless *bindless;

  GraphResource *resources;
  uint32_t resource_count, resource_cap;
  GraphPass *passes;
  uint32_t pass_count;
  uint32_t pass_cap;

  cREN_ColorAttachment *plan_colors;
  uint32_t plan_color_count;
  uint32_t plan_color_cap;
  cREN_Image **plan_reads;
  uint32_t plan_read_count;
  uint32_t plan_read_cap;
  bool compiled;
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

static void fn_unrealize(cREN_Graph *g, GraphResource *r) {
  if (r->bindless_slot != CREN_BINDLESS_INVALID && g->bindless != NULL) {
    cREN_bindless_free_image(g->bindless, CREN_VIEW_2D, r->bindless_slot);
  }
  cREN_image_view_destroy(g->ctx, g->device, r->view);           // NULL-safe
  cREN_image_destroy(g->ctx, g->device, g->allocator, r->image); // NULL-safe
  r->view = NULL;
  r->image = NULL;
  r->bindless_slot = CREN_BINDLESS_INVALID;
  r->width = r->height = 0;
}

static void fn_resolve_size(const cREN_Graph *g, const GraphResource *r, uint32_t *w, uint32_t *h) {
  if (r->desc.size_mode == CREN_GRAPH_SIZE_ABSOLUTE) {
    *w = r->desc.width;
    *h = r->desc.height;
    return;
  }
  const GraphResource *ref = fn_resource(g, r->desc.relative_to); // validated at creation
  uint32_t sw = 0, sh = 0;
  cREN_swapchain_extent(ref->swapchain, &sw, &sh);
  const float scale = r->desc.scale > 0.0f ? r->desc.scale : 1.0f;
  *w = (uint32_t)((float)sw * scale + 0.5f);
  *h = (uint32_t)((float)sh * scale + 0.5f);
  if (*w == 0) *w = 1;
  if (*h == 0) *h = 1;
}

static bool fn_realize(cREN_Graph *g, GraphResource *r, uint32_t w, uint32_t h) {
  cREN_ImageDesc desc = {
      .width = w,
      .height = h,
      .format = r->format,
      .usage = r->usage, // inferred from the declared uses
  };

  r->image = cREN_image_create(g->ctx, g->device, g->allocator, &desc);
  if (r->image == NULL) {
    return false;
  }
  r->width = w;
  r->height = h;

  if (r->usage & CREN_IMAGE_SAMPLED) {
    cREN_ImageViewDesc view_desc = {.type = CREN_VIEW_2D};
    r->view = cREN_image_view_create(g->ctx, g->device, r->image, &view_desc);
    r->bindless_slot = r->view != NULL ? cREN_bindless_add_image(g->bindless, r->view) : CREN_BINDLESS_INVALID;
    if (r->bindless_slot == CREN_BINDLESS_INVALID) {
      fn_unrealize(g, r);
      return false;
    }
  }
  return true;
}

static bool fn_read_later(const cREN_Graph *g, uint32_t pass_index, cREN_GraphImage image) {
  for (uint32_t p = pass_index + 1; p < g->pass_count; ++p) {
    for (uint32_t u = 0; u < g->passes[p].use_count; ++u) {
      const GraphUse *use = &g->passes[p].uses[u];
      if (use->image.id == image.id && (use->kind == USE_SAMPLED_READ || use->load == CREN_LOAD_LOAD)) {
        return true;
      }
    }
  }
  return false;
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
    fn_unrealize(g, &g->resources[i]);
    free(g->resources[i].name);
  }
  for (uint32_t i = 0; i < g->pass_count; ++i) {
    free(g->passes[i].name);
    free(g->passes[i].uses);
  }
  free(g->resources);
  free(g->passes);
  free(g->plan_colors);
  free(g->plan_reads);
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

bool cREN_graph_needs_compile(const cREN_Graph *g) {
  if (g == NULL) {
    return false;
  }
  if (!g->compiled) {
    return true;
  }

  for (uint32_t i = 0; i < g->resource_count; ++i) {
    const GraphResource *r = &g->resources[i];
    if (r->kind != RES_TRANSIENT || r->image == NULL) {
      continue;
    }
    uint32_t w, h;
    fn_resolve_size(g, r, &w, &h);
    if (w != r->width || h != r->height) {
      return true;
    }
  }
  return false;
}

bool cREN_graph_compile(cREN_Graph *g) {
  if (g == NULL) {
    return false;
  }
  g->compiled = false;

  // validate: nothing may be sampled before something writes it
  for (uint32_t i = 0; i < g->resource_count; ++i) {
    g->resources[i].written = false;
  }

  for (uint32_t p = 0; p < g->pass_count; ++p) {
    const GraphPass *pass = &g->passes[p];
    for (uint32_t u = 0; u < pass->use_count; ++u) {
      const GraphUse *use = &pass->uses[u];
      const GraphResource *r = &g->resources[use->image.id - 1];
      if (use->kind != USE_SAMPLED_READ) {
        continue;
      }
      if (g->bindless == NULL) {
        fprintf(stderr, "[cREN] Graph: pass '%s' samples '%s' but the graph has no bindless table\n", pass->name, r->name);
        return false;
      }
      if (!r->written) {
        fprintf(stderr, "[cREN] Graph: pass '%s' reads '%s' before any pass writes it\n", pass->name, r->name);
        return false;
      }
    }
    // writes count only after the whole pass, so a pass can't satisfy its own read
    for (uint32_t u = 0; u < pass->use_count; ++u) {
      if (pass->uses[u].kind != USE_SAMPLED_READ) {
        g->resources[pass->uses[u].image.id - 1].written = true;
      }
    }
  }

  // realize transients, recreating only the ones whose size changed
  bool idle = false;
  for (uint32_t i = 0; i < g->resource_count; ++i) {
    GraphResource *r = &g->resources[i];
    if (r->kind != RES_TRANSIENT) {
      continue;
    }
    if (r->usage == 0) {
      fprintf(stderr, "[cREN] Graph: image '%s' is never used, not creating it\n", r->name);
      continue;
    }
    uint32_t w, h;
    fn_resolve_size(g, r, &w, &h);
    if (r->image != NULL && r->width == w && r->height == h) {
      continue;
    }
    if (r->image != NULL) {
      if (!idle) {
        vkDeviceWaitIdle(g->device->logical);
        idle = true;
      }
      fn_unrealize(g, r);
    }
    if (!fn_realize(g, r, w, h)) {
      fprintf(stderr, "[cREN] Graph: failed to create image '%s' (%ux%u)\n", r->name, w, h);
      return false;
    }
  }

  // flat attachment and read lists, one slice per pass
  g->plan_color_count = 0;
  g->plan_read_count = 0;
  for (uint32_t p = 0; p < g->pass_count; ++p) {
    GraphPass *pass = &g->passes[p];
    pass->color_first = g->plan_color_count;
    pass->color_count = 0;
    pass->read_first = g->plan_read_count;
    pass->read_count = 0;
    pass->has_depth = false;
    pass->render_w = pass->render_h = 0;
    uint32_t depth_w = 0, depth_h = 0;

    for (uint32_t u = 0; u < pass->use_count; ++u) {
      const GraphUse *use = &pass->uses[u];
      GraphResource *r = &g->resources[use->image.id - 1];

      switch (use->kind) {
      case USE_COLOR_WRITE: {
        cREN_ColorAttachment *grown = fn_reserve(g->plan_colors, &g->plan_color_cap, g->plan_color_count + 1, sizeof(*grown));
        if (grown == NULL) {
          return false;
        }
        g->plan_colors = grown;

        cREN_ColorAttachment a = {.load = use->load, .clear = use->clear};
        uint32_t w, h;
        if (r->kind == RES_SWAPCHAIN) {
          a.swapchain = r->swapchain;
          cREN_swapchain_extent(r->swapchain, &w, &h);
        } else {
          a.image = r->image;
          w = r->width;
          h = r->height;
        }
        g->plan_colors[g->plan_color_count++] = a;
        pass->color_count++;
        if (pass->render_w == 0) {
          pass->render_w = w;
          pass->render_h = h;
        }
        break;
      }
      case USE_DEPTH_WRITE:
        pass->has_depth = true;
        pass->depth = (cREN_DepthAttachment){
            .image = r->image,
            .load = use->load,
            .clear_depth = use->clear_depth,
            // if nothing ever samples it, the GPU doesn't need to keep the result
            .discard_result = !fn_read_later(g, p, use->image),
        };
        depth_w = r->width;
        depth_h = r->height;
        break;
      case USE_SAMPLED_READ: {
        cREN_Image **grown = fn_reserve(g->plan_reads, &g->plan_read_cap, g->plan_read_count + 1, sizeof(*grown));
        if (grown == NULL) {
          return false;
        }
        g->plan_reads = grown;
        g->plan_reads[g->plan_read_count++] = r->image;
        pass->read_count++;
        break;
      }
      }
    }
    if (pass->render_w == 0) { // depth-only pass
      pass->render_w = depth_w;
      pass->render_h = depth_h;
    }
  }

  g->compiled = true;
  return true;
}

bool cREN_graph_execute(cREN_Graph *g, cREN_CmdBuffer *cmd, cREN_Frames *frames) {
  if (g == NULL || cmd == NULL || frames == NULL || !g->compiled) {
    return false;
  }

  for (uint32_t p = 0; p < g->pass_count; ++p) {
    const GraphPass *pass = &g->passes[p];

    // sampled inputs go to SHADER_READ before the pass that uses them
    for (uint32_t i = 0; i < pass->read_count; ++i) {
      cREN_cmd_transition_image(cmd, g->plan_reads[pass->read_first + i], CREN_IMAGE_STATE_SHADER_READ);
    }

    cREN_RenderingDesc rendering = {
        .colors = pass->color_count > 0 ? &g->plan_colors[pass->color_first] : NULL,
        .color_count = pass->color_count,
        .depth = pass->has_depth ? &pass->depth : NULL,
    };
    if (!cREN_cmd_begin_rendering(cmd, frames, &rendering)) {
      continue; // a swapchain this pass writes was not acquired this frame
    }
    cREN_cmd_set_viewport_scissor_size(cmd, pass->render_w, pass->render_h);
    pass->fn(cmd, g, pass->user);
    cREN_cmd_end_rendering(cmd);
  }

  // every swapchain that was written goes to present layout, once, after the last pass
  for (uint32_t i = 0; i < g->resource_count; ++i) {
    const GraphResource *r = &g->resources[i];
    if (r->kind == RES_SWAPCHAIN && r->written) {
      cREN_cmd_prepare_present(cmd, frames, r->swapchain); // false (and harmless) if not acquired
    }
  }
  return true;
}

cREN_Image *cREN_graph_get_image(const cREN_Graph *g, cREN_GraphImage image) {
  const GraphResource *r = g != NULL ? fn_resource(g, image) : NULL;
  return r != NULL ? r->image : NULL;
}

uint32_t cREN_graph_get_bindless(const cREN_Graph *g, cREN_GraphImage image) {
  const GraphResource *r = g != NULL ? fn_resource(g, image) : NULL;
  return r != NULL ? r->bindless_slot : CREN_BINDLESS_INVALID;
}
