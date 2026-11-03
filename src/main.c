#include "cREN_allocator.h"
#include "cREN_buffer.h"
#include "cREN_cmd.h"
#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_frames.h"
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
// TODO: Look into images and see if views can be independent from images like a image can have multiple views

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
  uint64_t vertices;
} PushConstants;

Vertex vertices[3] = {
    {{0.0f, -0.5f, 0.5f}, {1, 0, 0}},
    {{0.5f, 0.5f, 0.5f}, {0, 1, 0}},
    {{-0.5f, 0.5f, 0.5f}, {0, 0, 1}},
};
uint32_t indices[3] = {0, 1, 2};

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

  // shader
  cREN_Shader *shader = cREN_shader_load(ctx, device, CREN_SHADER_DIR "/triangle.spv");

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

  PushConstants push = {.vertices = cREN_buffer_address(vertex_buffer)};

  // pipeline
  cREN_Format color_format = cREN_swapchain_format(swapchain);
  cREN_PipelineDesc pipeline_desc = {
      .shader = shader,
      .vertex_entry = "vsMain",
      .frag_entry = "fsMain",
      .color_formats = &color_format,
      .color_format_count = 1,
      .depth_format = CREN_FORMAT_D32_FLOAT,
      .depth_test = true,
      .depth_write = true,
      .push_constant_size = sizeof(PushConstants),
  };
  cREN_Pipeline *pipeline = cREN_pipeline_init(ctx, device, &pipeline_desc);

  // sky
  cREN_ImageDesc sky_desc = {
      .width = 256,
      .height = 256,
      .format = CREN_FORMAT_RGBA8_UNORM,
      .usage = CREN_IMAGE_SAMPLED | CREN_IMAGE_TRANSFER_DST,
      .type = CREN_IMAGE_TYPE_CUBE,
      .mip_levels = UINT32_MAX,
  };
  cREN_Image *sky = cREN_image_create(ctx, device, allocator, &sky_desc);

  cREN_ImageViewDesc face_desc = {.type = CREN_VIEW_2D, .base_mip = 0, .mip_count = 1, .base_layer = 2, .layer_count = 1};
  cREN_ImageView *face = cREN_image_view_create(ctx, device, sky, &face_desc);

  cREN_SamplerDesc sampler_desc = {
      .min_filter = CREN_FILTER_LINEAR,
      .mag_filter = CREN_FILTER_LINEAR,
      .mip_filter = CREN_FILTER_LINEAR,
      .address_u = CREN_ADDRESS_REPEAT,
      .address_v = CREN_ADDRESS_REPEAT,
      .address_w = CREN_ADDRESS_REPEAT,
      .max_anisotropy = 16.0f,
  };
  cREN_Sampler *sampler = cREN_sampler_create(ctx, device, &sampler_desc);

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
            depth_desc.width = sw;
            depth_desc.height = sh;
            depth = cREN_image_create(ctx, device, allocator, &depth_desc);
          }
        }
      }
    }

    if (cREN_frame_begin(device, frames)) {
      cREN_frame_acquire(device, frames, swapchain);

      cREN_CmdBuffer *cmd = cREN_cmd_begin(frames);
      if (cmd != NULL) {
        // can be moved outside of loop since this is just plain data
        cREN_ColorAttachment color = {
            .swapchain = swapchain,
            .load = CREN_LOAD_CLEAR,
            .clear = {0.1f, 0.2f, 0.4f, 1.0f},
        };
        cREN_DepthAttachment depth_att = {
            .image = depth,
            .load = CREN_LOAD_CLEAR,
            .clear_depth = 1.0f,
        };
        cREN_RenderingDesc pass = {.colors = &color, .color_count = 1, .depth = &depth_att};

        if (cREN_cmd_begin_rendering(cmd, frames, &pass)) {
          cREN_cmd_bind_pipeline(cmd, pipeline);
          cREN_cmd_set_viewport_scissor(cmd, swapchain);
          cREN_cmd_push_constants(cmd, pipeline, &push, sizeof(push));
          cREN_cmd_bind_index_buffer(cmd, index_buffer, 0);
          cREN_cmd_draw_indexed(cmd, 3, 1, 0, 0, 0);
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
  cREN_image_view_destroy(ctx, device, face);
  cREN_image_destroy(ctx, device, allocator, sky);
  cREN_sampler_destroy(ctx, device, sampler);
  cREN_image_destroy(ctx, device, allocator, depth);
  cREN_buffer_destroy(ctx, allocator, index_buffer);
  cREN_buffer_destroy(ctx, allocator, vertex_buffer);
  cREN_pipeline_deinit(ctx, device, pipeline);
  cREN_swapchain_deinit(ctx, device, swapchain);
  cREN_surface_deinit(ctx, surface);
  cREN_allocator_deinit(allocator);
  cREN_shader_deinit(ctx, device, shader);
  cREN_device_deinit(device);
  cREN_instance_deinit(ctx);
  cREN_context_deinit(ctx);

  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}