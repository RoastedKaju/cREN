#include "cREN_instance.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fn_has_extension(const char *name) {
  uint32_t count = 0;
  if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS) {
    return false;
  }

  VkExtensionProperties *properties = malloc(count * sizeof(VkExtensionProperties));
  if (properties == NULL) {
    return false;
  }

  const VkResult result = vkEnumerateInstanceExtensionProperties(NULL, &count, properties);
  bool found = false;
  for (uint32_t i = 0; i < count && result == VK_SUCCESS; ++i) {
    if (strcmp(name, properties[i].extensionName) == 0) {
      found = true;
      break;
    }
  }

  free(properties);
  return found;
}

static bool fn_has_layer(const char *name) {
  uint32_t count = 0;
  if (vkEnumerateInstanceLayerProperties(&count, NULL) != VK_SUCCESS) {
    return false;
  }

  VkLayerProperties *properties = malloc(count * sizeof(VkLayerProperties));
  if (properties == NULL) {
    return false;
  }

  const VkResult result = vkEnumerateInstanceLayerProperties(&count, properties);
  bool found = false;
  for (uint32_t i = 0; i < count && result == VK_SUCCESS; ++i) {
    if (strcmp(name, properties[i].layerName) == 0) {
      found = true;
      break;
    }
  }

  free(properties);
  return found;
}

static inline VKAPI_ATTR VkBool32 VKAPI_CALL fn_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                               VkDebugUtilsMessageTypeFlagsEXT flags,
                                                               const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                               void *user_data) {
  (void)flags;
  (void)user_data;

  fprintf(stderr, "[cREN] %s\n", data->pMessage);
  return VK_FALSE;
}

static inline VkDebugUtilsMessengerCreateInfoEXT fn_make_messenger_info() {
  VkDebugUtilsMessengerCreateInfoEXT info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = fn_debug_callback,
  };
  return info;
}

bool cREN_instance_init(cREN_Context *ctx, cREN_InstanceDesc desc) {
  if (ctx == NULL) {
    return false;
  }

  /**
   * make a VK API VERSION from the major and minor parameters
   * check if the requested version is lower than supported version
   * assign the correct requested version to context API member
   */
  uint32_t requested = (desc.api_major != 0) ? VK_MAKE_API_VERSION(0, desc.api_major, desc.api_minor, 0) : VK_API_VERSION_1_3;
  ctx->api_ver = requested < ctx->api_ver ? requested : ctx->api_ver;
  fprintf(stderr, "[cREN] Requesting API version: %u.%u\n", VK_API_VERSION_MAJOR(ctx->api_ver), VK_API_VERSION_MINOR(ctx->api_ver));

  VkApplicationInfo application_info = {
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = desc.application_name != NULL ? desc.application_name : "cREN Application",
      .applicationVersion = desc.application_version,
      .pEngineName = desc.engine_name != NULL ? desc.engine_name : "cREN",
      .engineVersion = desc.engine_version,
      .apiVersion = ctx->api_ver,
  };

  /* check requested extensions */
  const uint32_t max_extensions = desc.required_extension_count + 1;
  const char **extensions = malloc(max_extensions * sizeof(*extensions));
  if (extensions == NULL) {
    return false;
  }

  uint32_t extension_count = 0;
  for (uint32_t i = 0; i < desc.required_extension_count; i++) {
    const char *name = desc.required_extensions[i];
    if (!fn_has_extension(name)) {
      fprintf(stderr, "[cREN] Missing instance extension: %s\n", name);
      free(extensions);
      return false;
    }
    fprintf(stderr, "[cREN] Adding extension: %s\n", name);
    extensions[extension_count++] = name;
  }

  /* layers */
  uint32_t layer_count = 0;
  const char *layers[1];

  // check for validation layer and extension support
  const bool validation_available = desc.enable_validation && fn_has_layer("VK_LAYER_KHRONOS_validation") && fn_has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  if (validation_available) {
    extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    fprintf(stderr, "[cREN] Adding extension: %s\n", VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    // add layer
    layers[layer_count++] = "VK_LAYER_KHRONOS_validation";
  }

  VkDebugUtilsMessengerCreateInfoEXT messenger_info = fn_make_messenger_info();

  VkInstanceCreateInfo create_info = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &application_info,
      .pNext = validation_available ? &messenger_info : NULL,
      .enabledLayerCount = layer_count,
      .ppEnabledLayerNames = layer_count ? layers : NULL,
      .enabledExtensionCount = extension_count,
      .ppEnabledExtensionNames = extensions,
  };

  VkResult result = vkCreateInstance(&create_info, NULL, &ctx->instance);
  if (result != VK_SUCCESS) {
    ctx->instance = VK_NULL_HANDLE;
    return false;
  }
  free(extensions);

  volkLoadInstance(ctx->instance);

  if (validation_available) {
    result = vkCreateDebugUtilsMessengerEXT(ctx->instance, &messenger_info, NULL, &ctx->debug_messenger);
    if (result != VK_SUCCESS) {
      ctx->debug_messenger = VK_NULL_HANDLE;
      vkDestroyInstance(ctx->instance, NULL);
      ctx->instance = VK_NULL_HANDLE;
      return false;
    }
  }

  return true;
}

void cREN_instance_deinit(cREN_Context *ctx) {
  if (ctx == NULL) {
    return;
  }

  if (ctx->debug_messenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(ctx->instance, ctx->debug_messenger, NULL);
    ctx->debug_messenger = VK_NULL_HANDLE;
  }

  if (ctx->instance != VK_NULL_HANDLE) {
    vkDestroyInstance(ctx->instance, NULL);
    ctx->instance = VK_NULL_HANDLE;
  }
}
