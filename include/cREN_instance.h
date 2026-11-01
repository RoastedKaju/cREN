#pragma once

#include "cREN_context.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct cREN_InstanceDesc {
  // application
  const char *application_name;
  const char *engine_name;
  uint32_t application_version;
  uint32_t engine_version;
  uint32_t api_major;
  uint32_t api_minor;
  bool enable_validation;

  // windowing backend
  const char *const *required_extensions;
  uint32_t required_extension_count;

  
} cREN_InstanceDesc;

bool cREN_instance_init(cREN_Context *ctx, cREN_InstanceDesc desc);

void cREN_instance_deinit(cREN_Context *ctx);