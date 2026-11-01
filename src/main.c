#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_instance.h"
#include "cREN_surface.h"
#include "cREN_swapchain.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stdio.h>

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

  // surface
  cREN_SurfaceDesc surface_desc = {
      .native_window = window,
      .user_data = NULL,
      .create_fn = create_vulkan_surface,
      .destroy_fn = destroy_vulkan_surface,
  };
  cREN_Surface *surface = cREN_surface_init(ctx, surface_desc);

  // device
  cREN_Device *device = cREN_device_init(ctx);

  // swapchain
  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(window, &width, &height);
  cREN_SwapchainDesc swapchain_desc = {
      .width = (uint32_t)width,
      .height = (uint32_t)height,
      .vsync = true,
  };
  cREN_Swapchain *swapchain = cREN_swapchain_init(ctx, device, surface, &swapchain_desc);

  bool running = true;
  bool resized = false;

  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        resized = true;
      }
    }

    if (resized) {
      SDL_GetWindowSizeInPixels(window, &width, &height);
      if (width > 0 && height > 0) {
        cREN_swapchain_resize(ctx, device, swapchain, (uint32_t)width, (uint32_t)height);
      }
      resized = false;
    }

    // if window is minimized
    if (width == 0 || height == 0) {
      SDL_Delay(10);
      continue;
    }
  }

  // clean-up
  cREN_swapchain_deinit(ctx, device, swapchain);
  cREN_device_deinit(device);
  cREN_surface_deinit(ctx, surface);
  cREN_instance_deinit(ctx);
  cREN_context_deinit(ctx);

  return 0;
}