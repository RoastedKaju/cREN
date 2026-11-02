#pragma once

#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_swapchain.h"
#include "cREN_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct cREN_Frames cREN_Frames;
typedef struct cREN_CmdBuffer cREN_CmdBuffer;

typedef struct cREN_FramesDesc {
  uint32_t frames_in_flight; // 0 = 2
} cREN_FramesDesc;

cREN_Frames *cREN_frames_init(cREN_Context *ctx, cREN_Device *device, const cREN_FramesDesc *desc);

void cREN_frames_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Frames *frames);

uint32_t cREN_frames_count(const cREN_Frames *frames);

/* waits until GPU is done with current frame slot, then resets it */
bool cREN_frame_begin(cREN_Device *device, cREN_Frames *frames);

cREN_FrameStatus cREN_frame_acquire(cREN_Device *device, cREN_Frames *frames, cREN_Swapchain *sc);

bool cREN_frame_end(cREN_Device *device, cREN_Frames *frames);

/* the current slot's command buffer, valid between begin and end frame. */
cREN_CmdBuffer *cREN_frame_command_buffer(cREN_Frames *frames);

bool cREN_frame_image_index(const cREN_Frames *frames, const cREN_Swapchain *sc, uint32_t *out_index);
