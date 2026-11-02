#pragma once

#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_shader.h"
#include "cREN_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct cREN_Pipeline cREN_Pipeline;

typedef struct cREN_PipelineDesc {
  const cREN_Shader *shader;
  const char *vertex_entry;
  const char *frag_entry;
  const cREN_Format *color_formats; // one color per attachment
  uint32_t color_format_count;
  cREN_Format depth_format; // UNDEFINED = no depth attachment
  bool depth_test;
  bool depth_write;
  uint32_t push_constant_size; // 128 max
} cREN_PipelineDesc;

cREN_Pipeline *cREN_pipeline_init(cREN_Context *ctx, cREN_Device *device, const cREN_PipelineDesc *desc);

void cREN_pipeline_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Pipeline *pipeline);