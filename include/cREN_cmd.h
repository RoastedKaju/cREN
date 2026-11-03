#pragma once

#include "cREN_buffer.h"
#include "cREN_frames.h"
#include "cREN_image.h"
#include "cREN_pipeline.h"

#include <stdbool.h>

typedef struct cREN_ClearColor {
  float r, g, b, a;
} cREN_ClearColor;

typedef struct cREN_ColorAttachment {
  cREN_Swapchain *swapchain; // set exactly one of swapchain / image
  cREN_Image *image;
  cREN_LoadOp load;
  cREN_ClearColor clear;
} cREN_ColorAttachment;

typedef struct cREN_DepthAttachment {
  cREN_Image *image;
  cREN_LoadOp load;
  float clear_depth;
  bool discard_result; // false = store, which is needed if the depth is read later
} cREN_DepthAttachment;

typedef struct cREN_RenderingDesc {
  const cREN_ColorAttachment *colors;
  uint32_t color_count;
  const cREN_DepthAttachment *depth; // NULL = none
} cREN_RenderingDesc;

cREN_CmdBuffer *cREN_cmd_begin(cREN_Frames *frames);

bool cREN_cmd_end(cREN_CmdBuffer *cmd);

bool cREN_cmd_begin_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, const cREN_RenderingDesc *desc);

void cREN_cmd_end_rendering(cREN_CmdBuffer *cmd);

void cREN_cmd_bind_pipeline(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline);

bool cREN_cmd_prepare_present(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc);

void cREN_cmd_transition_image(cREN_CmdBuffer *cmd, cREN_Image *image, cREN_ImageState state);

void cREN_cmd_set_viewport_scissor_size(cREN_CmdBuffer *cmd, uint32_t width, uint32_t height);

void cREN_cmd_set_viewport_scissor(cREN_CmdBuffer *cmd, const cREN_Swapchain *sc);

void cREN_cmd_draw(cREN_CmdBuffer *cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

void cREN_cmd_push_constants(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline, const void *data, uint32_t size);

void cREN_cmd_bind_index_buffer(cREN_CmdBuffer *cmd, const cREN_Buffer *buffer, uint64_t offset);

void cREN_cmd_draw_indexed(cREN_CmdBuffer *cmd, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);

void cREN_cmd_bind_bindless(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline, const cREN_Bindless *bindless);