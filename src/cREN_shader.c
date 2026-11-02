#include "cREN_shader.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t *fn_read_spirv(const char *path, size_t *out_size) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "[cREN] Failed to open shader: %s\n", path);
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  if (size <= 0 || size % 4 != 0) { // SPIR-V is a stream of 32-bit words
    fprintf(stderr, "[cREN] Invalid SPIR-V size: %s\n", path);
    fclose(file);
    return NULL;
  }

  uint32_t *code = malloc((size_t)size); // malloc alignment satisfies uint32_t
  if (code == NULL || fread(code, 1, (size_t)size, file) != (size_t)size) {
    free(code);
    fclose(file);
    return NULL;
  }

  fclose(file);
  *out_size = (size_t)size;
  return code;
}

cREN_Shader *cREN_shader_load(cREN_Context *ctx, cREN_Device *device, const char *path) {
  if (ctx == NULL || device == NULL || path == NULL || device->logical == VK_NULL_HANDLE) {
    return NULL;
  }

  size_t size = 0;
  uint32_t *code = fn_read_spirv(path, &size);
  if (code == NULL) {
    return NULL;
  }

  cREN_Shader *shader = calloc(1, sizeof(*shader));
  if (shader == NULL) {
    free(code);
    return NULL;
  }

  VkShaderModuleCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = size,
      .pCode = code,
  };
  VkResult result = vkCreateShaderModule(device->logical, &info, NULL, &shader->module);
  free(code); // the driver copies it, so the buffer isn't needed after this call

  if (result != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Shader module creation failed: %s\n", path);
    free(shader);
    return NULL;
  }

  fprintf(stderr, "[cREN] Shader loaded: %s\n", path);
  return shader;
}

void cREN_shader_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Shader *shader) {
  if (ctx == NULL || device == NULL || shader == NULL) {
    return;
  }
  if (shader->module != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device->logical, shader->module, NULL);
  }
  free(shader);
}