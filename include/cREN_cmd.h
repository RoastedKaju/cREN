#pragma once

#include "cREN_buffer.h"
#include "cREN_frames.h"
#include "cREN_pipeline.h"

#include <stdbool.h>

typedef struct cREN_ClearColor {
  float r, g, b, a;
} cREN_ClearColor;

cREN_CmdBuffer *cREN_cmd_begin(cREN_Frames *frames);

bool cREN_cmd_end(cREN_CmdBuffer *cmd);

bool cREN_cmd_begin_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc, cREN_ClearColor clear);

bool cREN_cmd_end_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc);

void cREN_cmd_bind_pipeline(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline);

/* sets viewport and scissor to the swapchain's full extent, call inside begin/end rendering. */
void cREN_cmd_set_viewport_scissor(cREN_CmdBuffer *cmd, const cREN_Swapchain *sc);

void cREN_cmd_draw(cREN_CmdBuffer *cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance);

void cREN_cmd_push_constants(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline, const void *data, uint32_t size);

void cREN_cmd_bind_index_buffer(cREN_CmdBuffer *cmd, const cREN_Buffer *buffer, uint64_t offset);

void cREN_cmd_draw_indexed(cREN_CmdBuffer *cmd, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance);