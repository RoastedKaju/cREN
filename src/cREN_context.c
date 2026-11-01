#include "cREN_context.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

cREN_Context *cREN_context_init(void) {
  // loader
  const VkResult res = volkInitialize();
  if (res != VK_SUCCESS) {
    return NULL;
  }

  uint32_t loader_ver = VK_API_VERSION_1_0;
  if (vkEnumerateInstanceVersion) {
    vkEnumerateInstanceVersion(&loader_ver);
  }

  if (loader_ver < VK_API_VERSION_1_3) {
    fprintf(stderr, "[cREN] Vulkan 1.3 or newer is required, loader reports: %d\n", loader_ver);
    return NULL;
  }
  fprintf(stderr, "[cREN] Vulkan loader version: %u.%u\n", VK_API_VERSION_MAJOR(loader_ver), VK_API_VERSION_MINOR(loader_ver));

  // context
  cREN_Context *ctx = (cREN_Context *)malloc(sizeof(cREN_Context));
  if (ctx == NULL) {
    return NULL;
  }

  ctx->api_ver = loader_ver;

  // defaults
  ctx->instance = VK_NULL_HANDLE;
  ctx->debug_messenger = VK_NULL_HANDLE;

  return ctx;
}

void cREN_context_deinit(cREN_Context *ctx) {
  if (ctx != NULL) {
    free(ctx);
  }
}
