#include "cREN_frames.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static void fn_destroy_slots(VkDevice device, cREN_FrameSlot *slots, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    if (slots[i].pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, slots[i].pool, NULL); // also frees its command buffer
    }
  }
}

cREN_Frames *cREN_frames_init(cREN_Context *ctx, cREN_Device *device, const cREN_FramesDesc *desc) {
  if (ctx == NULL || device == NULL || desc == NULL) {
    return NULL;
  }

  cREN_Frames *frames = calloc(1, sizeof(*frames));
  if (frames == NULL) {
    return NULL;
  }

  frames->count = desc->frames_in_flight != 0 ? desc->frames_in_flight : 2;
  frames->slots = calloc(frames->count, sizeof(*frames->slots));
  if (frames->slots == NULL) {
    free(frames);
    return NULL;
  }

  for (uint32_t i = 0; i < frames->count; ++i) {
    cREN_FrameSlot *slot = &frames->slots[i];

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = device->universal_family,
    };
    if (vkCreateCommandPool(device->logical, &pool_info, NULL, &slot->pool) != VK_SUCCESS) {
      goto fail;
    }

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = slot->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device->logical, &alloc_info, &slot->cmd.handle) != VK_SUCCESS) {
      goto fail;
    }
  }

  return frames;

fail:
  fprintf(stderr, "[cREN] Failed to create frame resources\n");
  fn_destroy_slots(device->logical, frames->slots, frames->count);
  free(frames->slots);
  free(frames);
  return NULL;
}

void cREN_frames_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Frames *frames) {
  if (ctx == NULL || device == NULL || frames == NULL) {
    return;
  }

  vkDeviceWaitIdle(device->logical);
  fn_destroy_slots(device->logical, frames->slots, frames->count);
  free(frames->slots);
  free(frames->acquired);
  free(frames->waits);
  free(frames->signals);
  free(frames->present_waits);
  free(frames->chains);
  free(frames->indices);
  free(frames->results);
  free(frames);
}

uint32_t cREN_frames_count(const cREN_Frames *frames) {
  return frames != NULL ? frames->count : 0;
}

bool cREN_frame_begin(cREN_Device *device, cREN_Frames *frames) {
  if (device == NULL || frames == NULL) {
    return false;
  }

  cREN_FrameSlot *slot = &frames->slots[frames->current];

  VkSemaphoreWaitInfo wait_info = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
      .semaphoreCount = 1,
      .pSemaphores = &device->timeline,
      .pValues = &slot->submit_value,
  };
  if (vkWaitSemaphores(device->logical, &wait_info, UINT64_MAX) != VK_SUCCESS) {
    return false;
  }

  // GPU is finished with this slot, so it is safe to reuse
  vkResetCommandPool(device->logical, slot->pool, 0);
  frames->acquired_count = 0;

  slot->cmd.recorded = false;

  return true;
}

cREN_FrameStatus cREN_frame_acquire(cREN_Device *device, cREN_Frames *frames, cREN_Swapchain *sc) {
  if (device == NULL || frames == NULL || sc == NULL) {
    return CREN_FRAME_ERROR;
  }
  if (sc->acquire_count != frames->count) {
    fprintf(stderr, "[cREN] Swapchain frame count does not match the frames ring size\n");
    return CREN_FRAME_ERROR;
  }

#define CREN_GROW(field, n)                                          \
  do {                                                               \
    void *p_ = realloc(frames->field, (n) * sizeof(*frames->field)); \
    if (p_ == NULL) {                                                \
      return CREN_FRAME_ERROR;                                       \
    }                                                                \
    frames->field = p_;                                              \
  } while (0)

  if (frames->acquired_count == frames->acquired_cap) {
    uint32_t cap = frames->acquired_cap != 0 ? frames->acquired_cap * 2 : 2;
    CREN_GROW(acquired, cap);
    CREN_GROW(waits, cap);
    CREN_GROW(signals, cap + 1); // one render-finished per window + the timeline
    CREN_GROW(present_waits, cap);
    CREN_GROW(chains, cap);
    CREN_GROW(indices, cap);
    CREN_GROW(results, cap);
    frames->acquired_cap = cap;
  }
#undef CREN_GROW

  uint32_t index = 0;
  VkResult res = vkAcquireNextImageKHR(device->logical, sc->handle, UINT64_MAX, sc->acquires[frames->current], VK_NULL_HANDLE, &index);
  if (res == VK_ERROR_OUT_OF_DATE_KHR) {
    sc->needs_resize = true;
    return CREN_FRAME_NEEDS_RESIZE;
  }
  if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
    return CREN_FRAME_ERROR;
  }
  if (res == VK_SUBOPTIMAL_KHR) {
    sc->needs_resize = true; // still usable this frame
  }

  frames->acquired[frames->acquired_count++] = (cREN_Acquired){sc, index};
  return CREN_FRAME_OK;
}

bool cREN_frame_end(cREN_Device *device, cREN_Frames *frames) {
  if (device == NULL || frames == NULL) {
    return false;
  }

  cREN_FrameSlot *slot = &frames->slots[frames->current];
  const uint32_t n = frames->acquired_count;
  const uint64_t signal_value = device->timeline_value + 1;

  for (uint32_t i = 0; i < n; ++i) {
    cREN_Swapchain *sc = frames->acquired[i].swapchain;
    const uint32_t image = frames->acquired[i].image_index;

    frames->waits[i] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sc->acquires[frames->current],
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    frames->signals[i] = (VkSemaphoreSubmitInfo){
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = sc->render_finished[image],
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
    };
    frames->present_waits[i] = sc->render_finished[image];
    frames->chains[i] = sc->handle;
    frames->indices[i] = image;
  }

  frames->signals[n] = (VkSemaphoreSubmitInfo){
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = device->timeline,
      .value = signal_value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
  };

  VkCommandBufferSubmitInfo cb = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = slot->cmd.handle,
  };

  VkSubmitInfo2 submit = {
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
      .waitSemaphoreInfoCount = n,
      .pWaitSemaphoreInfos = frames->waits,
      .signalSemaphoreInfoCount = n + 1,
      .pSignalSemaphoreInfos = frames->signals,
      .commandBufferInfoCount = slot->cmd.recorded ? 1 : 0,
      .pCommandBufferInfos = &cb,
  };

  bool ok = vkQueueSubmit2(device->universal_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
  if (ok) {
    device->timeline_value = signal_value;
    slot->submit_value = signal_value;

    if (n > 0) {
      VkPresentInfoKHR present = {
          .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
          .waitSemaphoreCount = n,
          .pWaitSemaphores = frames->present_waits,
          .swapchainCount = n,
          .pSwapchains = frames->chains,
          .pImageIndices = frames->indices,
          .pResults = frames->results,
      };
      vkQueuePresentKHR(device->universal_queue, &present);

      for (uint32_t i = 0; i < n; ++i) {
        if (frames->results[i] == VK_ERROR_OUT_OF_DATE_KHR || frames->results[i] == VK_SUBOPTIMAL_KHR) {
          frames->acquired[i].swapchain->needs_resize = true;
        }
      }
    }
  }

  frames->acquired_count = 0;
  frames->current = (frames->current + 1) % frames->count;
  return ok;
}

cREN_CmdBuffer *cREN_frame_command_buffer(cREN_Frames *frames) {
  return frames != NULL ? &frames->slots[frames->current].cmd : NULL;
}

bool cREN_frame_image_index(const cREN_Frames *frames, const cREN_Swapchain *sc, uint32_t *out_index) {
  if (frames == NULL || sc == NULL || out_index == NULL) {
    return false;
  }
  for (uint32_t i = 0; i < frames->acquired_count; ++i) {
    if (frames->acquired[i].swapchain == sc) {
      *out_index = frames->acquired[i].image_index;
      return true;
    }
  }
  return false;
}
