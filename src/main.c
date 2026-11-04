#include "cREN_allocator.h"
#include "cREN_buffer.h"
#include "cREN_cmd.h"
#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_frames.h"
#include "cREN_graph.h"
#include "cREN_image.h"
#include "cREN_instance.h"
#include "cREN_pipeline.h"
#include "cREN_sampler.h"
#include "cREN_shader.h"
#include "cREN_surface.h"
#include "cREN_swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>

// TODO: Pass the descriptors using const * as well as it will be good later on when desiging ABI for C#
// TODO: see if you can remove the push constant size from pipeline as we are now using the CREN_PUSH_CONSTANT_SIZE macro

static bool create_vulkan_surface(void *instance, void *window, void *data, void **surface) {
  (void)data;

  SDL_Vulkan_CreateSurface((SDL_Window *)window, (VkInstance)instance, NULL, (VkSurfaceKHR *)surface);
  printf("Creating SDL Vulkan surface.\n");

  return true;
}

static void destroy_vulkan_surface(void *instance, void *surface, void *data) {
  (void)data;

  SDL_Vulkan_DestroySurface((VkInstance)instance, (VkSurfaceKHR)surface, NULL);

  printf("Destroying SDL Vulkan surface.\n");
}

typedef struct Vertex {
  float position[3];
  float color[3];
} Vertex;

typedef struct PushConstants {
  uint64_t vertices; // BDA, read by the scene shader
  uint32_t texture;  // bindless image slot, read by the post shader
  uint32_t sampler;  // bindless sampler slot
} PushConstants;     // 16 bytes

Vertex vertices[3] = {
    {{0.0f, -0.5f, 0.5f}, {1, 0, 0}},
    {{0.5f, 0.5f, 0.5f}, {0, 1, 0}},
    {{-0.5f, 0.5f, 0.5f}, {0, 0, 1}},
};
uint32_t indices[3] = {0, 1, 2};

// HDR helper
typedef struct HDRTarget {
  cREN_Image *image;
  cREN_ImageView *view;
  uint32_t slot;
} HDRTarget;

static bool HDR_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Bindless *bindless, uint32_t w, uint32_t h, HDRTarget *out) {
  cREN_ImageDesc desc = {
      .width = w,
      .height = h,
      .format = CREN_FORMAT_RGBA16_FLOAT,
      .usage = CREN_IMAGE_COLOR_ATTACHMENT | CREN_IMAGE_SAMPLED,
  };
  out->image = cREN_image_create(ctx, device, allocator, &desc);
  if (out->image == NULL) {
    return false;
  }

  cREN_ImageViewDesc view_desc = {.type = CREN_VIEW_2D};
  out->view = cREN_image_view_create(ctx, device, out->image, &view_desc);
  out->slot = out->view != NULL ? cREN_bindless_add_image(bindless, out->view) : CREN_BINDLESS_INVALID;
  if (out->slot == CREN_BINDLESS_INVALID) {
    cREN_image_view_destroy(ctx, device, out->view); // NULL-safe
    cREN_image_destroy(ctx, device, allocator, out->image);
    *out = (HDRTarget){0};
    return false;
  }
  return true;
}

static void HDR_destroy(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Bindless *bindless, HDRTarget *hdr) {
  if (hdr->image == NULL) {
    return;
  }
  cREN_bindless_free_image(bindless, CREN_VIEW_2D, hdr->slot);
  cREN_image_view_destroy(ctx, device, hdr->view);
  cREN_image_destroy(ctx, device, allocator, hdr->image);
  *hdr = (HDRTarget){0};
}

static void pass_no_op(cREN_CmdBuffer *cmd, cREN_Graph *graph, void *user) {
  (void)cmd;
  (void)graph;
  (void)user;
}

int main() {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_Window *window = SDL_CreateWindow("Application", 800, 600, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  Uint32 ext_count = 0;
  const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&ext_count);

  // context
  cREN_Context *ctx = cREN_context_init();

  // instance
  cREN_InstanceDesc instance_desc = {
      .application_name = "cREN Application",
      .engine_name = "No Engine",
      .application_version = CREN_MAKE_VERSION(1, 0, 0),
      .engine_version = CREN_MAKE_VERSION(1, 0, 0),
      .api_major = 1,
      .api_minor = 3,
      .enable_validation = true,
      .required_extensions = exts,
      .required_extension_count = ext_count,
  };
  cREN_instance_init(ctx, instance_desc);

  // device
  cREN_Device *device = cREN_device_init(ctx);

  // bindless table
  cREN_BindlessDesc bl_desc = {0}; // defaults
  cREN_Bindless *bindless = cREN_bindless_init(ctx, device, &bl_desc);

  // shader
  cREN_Shader *default_shader = cREN_shader_load(ctx, device, CREN_SHADER_DIR "/triangle.spv");
  cREN_Shader *post_shader = cREN_shader_load(ctx, device, CREN_SHADER_DIR "/post.spv");

  // frames
  cREN_FramesDesc frames_desc = {
      .frames_in_flight = 2,
  };
  cREN_Frames *frames = cREN_frames_init(ctx, device, &frames_desc);

  // allocator
  cREN_Allocator *allocator = cREN_allocator_init(ctx, device);

  // surface
  cREN_SurfaceDesc surface_desc = {
      .native_window = window,
      .user_data = NULL,
      .create_fn = create_vulkan_surface,
      .destroy_fn = destroy_vulkan_surface,
  };
  cREN_Surface *surface = cREN_surface_init(ctx, surface_desc);

  // swapchain
  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  cREN_SwapchainDesc swapchain_desc = {
      .width = (uint32_t)width,
      .height = (uint32_t)height,
      .vsync = true,
      .frame_count = cREN_frames_count(frames),
  };
  cREN_Swapchain *swapchain = cREN_swapchain_init(ctx, device, surface, &swapchain_desc);

  // depth image
  uint32_t sw = 0, sh = 0;
  cREN_swapchain_extent(swapchain, &sw, &sh);
  cREN_ImageDesc depth_desc = {sw, sh, CREN_FORMAT_D32_FLOAT, CREN_IMAGE_DEPTH_ATTACHMENT};
  cREN_Image *depth = cREN_image_create(ctx, device, allocator, &depth_desc);

  // upload data in buffers
  cREN_BufferDesc vb_desc = {sizeof(vertices), CREN_BUFFER_STORAGE | CREN_BUFFER_TRANSFER_DST, CREN_MEMORY_GPU_ONLY};
  cREN_BufferDesc ib_desc = {sizeof(indices), CREN_BUFFER_INDEX | CREN_BUFFER_TRANSFER_DST, CREN_MEMORY_GPU_ONLY};
  cREN_Buffer *vertex_buffer = cREN_buffer_create(ctx, device, allocator, &vb_desc);
  cREN_Buffer *index_buffer = cREN_buffer_create(ctx, device, allocator, &ib_desc);
  cREN_buffer_upload(ctx, device, allocator, vertex_buffer, 0, vertices, sizeof(vertices));
  cREN_buffer_upload(ctx, device, allocator, index_buffer, 0, indices, sizeof(indices));

  // create HDR target
  HDRTarget hdr = {0};
  HDR_create(ctx, device, allocator, bindless, sw, sh, &hdr);
  cREN_SamplerDesc linear_desc = {
      .min_filter = CREN_FILTER_LINEAR,
      .mag_filter = CREN_FILTER_LINEAR,
      .mip_filter = CREN_FILTER_NEAREST,
      .address_u = CREN_ADDRESS_CLAMP_TO_EDGE,
      .address_v = CREN_ADDRESS_CLAMP_TO_EDGE,
      .address_w = CREN_ADDRESS_CLAMP_TO_EDGE,
  };
  cREN_Sampler *linear_clamp = cREN_sampler_create(ctx, device, &linear_desc);

  // push constants
  PushConstants push = {
      .vertices = cREN_buffer_address(vertex_buffer),
      .texture = hdr.slot, // updated again after every resize
      .sampler = cREN_bindless_add_sampler(bindless, linear_clamp),
  };

  // pipelines
  cREN_Format hdr_format = CREN_FORMAT_RGBA16_FLOAT;
  cREN_PipelineDesc scene_desc = {
      .shader = default_shader,
      .vertex_entry = "vsMain",
      .frag_entry = "fsMain",
      .color_formats = &hdr_format,
      .color_format_count = 1,
      .depth_format = CREN_FORMAT_D32_FLOAT,
      .depth_test = true,
      .depth_write = true,
      .bindless = bindless,
  };
  cREN_Pipeline *scene_pipeline = cREN_pipeline_init(ctx, device, &scene_desc);

  cREN_Format swap_format = cREN_swapchain_format(swapchain);
  cREN_PipelineDesc post_desc = {
      .shader = post_shader,
      .vertex_entry = "vsMain",
      .frag_entry = "fsMain",
      .color_formats = &swap_format,
      .color_format_count = 1,
      .depth_format = CREN_FORMAT_UNDEFINED,
      .bindless = bindless,
  };
  cREN_Pipeline *post_pipeline = cREN_pipeline_init(ctx, device, &post_desc);

  // render graph
  cREN_Graph *graph = cREN_graph_create(ctx, device, allocator, bindless);
  cREN_GraphImage backbuffer = cREN_graph_import_swapchain(graph, swapchain);
  cREN_GraphImageDesc hdr_desc = {
      .name = "hdr",
      .format = CREN_FORMAT_RGBA16_FLOAT,
      .size_mode = CREN_GRAPH_SIZE_RELATIVE,
      .relative_to = backbuffer,
  };
  cREN_GraphImage hdr_img = cREN_graph_create_image(graph, &hdr_desc);

  cREN_GraphImageDesc depth_desc2 = {
      .name = "depth",
      .format = CREN_FORMAT_D32_FLOAT,
      .size_mode = CREN_GRAPH_SIZE_RELATIVE,
      .relative_to = backbuffer,
  };
  cREN_GraphImage depth_img = cREN_graph_create_image(graph, &depth_desc2);

  cREN_GraphPass scene = cREN_graph_pass_add(graph, "scene", pass_no_op, NULL);
  cREN_graph_pass_write_color(graph, scene, hdr_img, CREN_LOAD_CLEAR, (cREN_ClearColor){0.1f, 0.2f, 0.4f, 1.0f});
  cREN_graph_pass_write_depth(graph, scene, depth_img, CREN_LOAD_CLEAR, 1.0f);

  cREN_GraphPass post = cREN_graph_pass_add(graph, "post", pass_no_op, NULL);
  cREN_graph_pass_read_sampled(graph, post, hdr_img);
  cREN_graph_pass_write_color(graph, post, backbuffer, CREN_LOAD_DONT_CARE, (cREN_ClearColor){0});

  cREN_graph_dump(graph);

  bool running = true;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }

    // if window is minimized
    if (width == 0 || height == 0) {
      SDL_Delay(10);
      continue;
    }

    if (cREN_swapchain_needs_resize(swapchain)) {
      SDL_GetWindowSizeInPixels(window, &width, &height);
      if (width > 0 && height > 0) {
        if (cREN_swapchain_resize(ctx, device, swapchain, (uint32_t)width, (uint32_t)height)) {
          // on successful resize, recreate depth image
          cREN_swapchain_extent(swapchain, &sw, &sh);
          if (sw != cREN_image_width(depth) || sh != cREN_image_height(depth)) {
            cREN_image_destroy(ctx, device, allocator, depth);
            HDR_destroy(ctx, device, allocator, bindless, &hdr);

            depth_desc.width = sw;
            depth_desc.height = sh;
            depth = cREN_image_create(ctx, device, allocator, &depth_desc);
            if (depth == NULL || !HDR_create(ctx, device, allocator, bindless, sw, sh, &hdr)) {
              running = false;
              continue;
            }
            push.texture = hdr.slot; // the slot may change, so never cache it elsewhere
          }
        }
      }
    }

    if (cREN_frame_begin(device, frames)) {
      const bool have_image = cREN_frame_acquire(device, frames, swapchain) == CREN_FRAME_OK;

      cREN_CmdBuffer *cmd = have_image ? cREN_cmd_begin(frames) : NULL;
      if (cmd != NULL) {
        cREN_ColorAttachment hdr_color = {.image = hdr.image, .load = CREN_LOAD_CLEAR, .clear = {0.1f, 0.2f, 0.4f, 1.0f}};
        cREN_DepthAttachment depth_att = {.image = depth, .load = CREN_LOAD_CLEAR, .clear_depth = 1.0f};
        cREN_RenderingDesc scene_pass = {.colors = &hdr_color, .color_count = 1, .depth = &depth_att};

        cREN_ColorAttachment post_color = {.swapchain = swapchain, .load = CREN_LOAD_DONT_CARE};
        cREN_RenderingDesc post_pass = {.colors = &post_color, .color_count = 1};

        // scene -> HDR
        if (cREN_cmd_begin_rendering(cmd, frames, &scene_pass)) {
          cREN_cmd_bind_pipeline(cmd, scene_pipeline);
          cREN_cmd_bind_bindless(cmd, scene_pipeline, bindless); // once per frame, the post layout is identical
          cREN_cmd_set_viewport_scissor_size(cmd, cREN_image_width(hdr.image), cREN_image_height(hdr.image));
          cREN_cmd_push_constants(cmd, scene_pipeline, &push, sizeof(push));
          cREN_cmd_bind_index_buffer(cmd, index_buffer, 0);
          cREN_cmd_draw_indexed(cmd, 3, 1, 0, 0, 0);
          cREN_cmd_end_rendering(cmd);
          cREN_cmd_transition_image(cmd, hdr.image, CREN_IMAGE_STATE_SHADER_READ);
        }

        // HDR -> tonemap -> swapchain
        if (cREN_cmd_begin_rendering(cmd, frames, &post_pass)) {
          cREN_cmd_bind_pipeline(cmd, post_pipeline);
          cREN_cmd_set_viewport_scissor(cmd, swapchain);
          cREN_cmd_push_constants(cmd, post_pipeline, &push, sizeof(push));
          cREN_cmd_draw(cmd, 3, 1, 0, 0);
          cREN_cmd_end_rendering(cmd);
          cREN_cmd_prepare_present(cmd, frames, swapchain);
        }
        cREN_cmd_end(cmd);
      }

      cREN_frame_end(device, frames);
    }
  }

  // clean-up
  cREN_frames_deinit(ctx, device, frames);
  cREN_graph_destroy(graph);
  HDR_destroy(ctx, device, allocator, bindless, &hdr);
  cREN_image_destroy(ctx, device, allocator, depth);
  cREN_buffer_destroy(ctx, allocator, index_buffer);
  cREN_buffer_destroy(ctx, allocator, vertex_buffer);
  cREN_pipeline_deinit(ctx, device, scene_pipeline);
  cREN_pipeline_deinit(ctx, device, post_pipeline);
  cREN_sampler_destroy(ctx, device, linear_clamp);
  cREN_swapchain_deinit(ctx, device, swapchain);
  cREN_surface_deinit(ctx, surface);
  cREN_allocator_deinit(allocator);
  cREN_shader_deinit(ctx, device, default_shader);
  cREN_shader_deinit(ctx, device, post_shader);
  cREN_bindless_deinit(ctx, device, bindless);
  cREN_device_deinit(device);
  cREN_instance_deinit(ctx);
  cREN_context_deinit(ctx);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}