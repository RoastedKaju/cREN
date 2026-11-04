#include "cREN_cmd.h"
#include "cREN_internal.h"

#include <stdlib.h>

static void fn_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t mip_levels, uint32_t array_layers, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
  VkImageMemoryBarrier2 barrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = src_stage,
      .srcAccessMask = src_access,
      .dstStageMask = dst_stage,
      .dstAccessMask = dst_access,
      .oldLayout = old_layout,
      .newLayout = new_layout,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = image,
      .subresourceRange = {.aspectMask = aspect, .levelCount = mip_levels, .layerCount = array_layers},
  };
  VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cmd, &dependency);
}

static void fn_state_info(cREN_ImageState state, VkImageLayout *layout, VkPipelineStageFlags2 *stage, VkAccessFlags2 *access) {
  switch (state) {
  case CREN_IMAGE_STATE_COLOR_ATTACHMENT:
    *layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    *stage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    *access = VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case CREN_IMAGE_STATE_DEPTH_ATTACHMENT:
    *layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    *stage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    *access = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case CREN_IMAGE_STATE_SHADER_READ:
    *layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    *stage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    *access = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    break;
  case CREN_IMAGE_STATE_TRANSFER_SRC:
    *layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    *stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    *access = VK_ACCESS_2_TRANSFER_READ_BIT;
    break;
  case CREN_IMAGE_STATE_TRANSFER_DST:
    *layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    *stage = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT;
    *access = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    break;
  case CREN_IMAGE_STATE_UNDEFINED:
  default:
    *layout = VK_IMAGE_LAYOUT_UNDEFINED;
    *stage = VK_PIPELINE_STAGE_2_NONE;
    *access = 0;
    break;
  }
}

static void fn_transition(VkCommandBuffer cmd, cREN_Image *image, cREN_ImageState new_state, bool discard) {
  VkImageLayout old_layout, new_layout;
  VkPipelineStageFlags2 src_stage, dst_stage;
  VkAccessFlags2 src_access, dst_access;
  fn_state_info(image->state, &old_layout, &src_stage, &src_access);
  fn_state_info(new_state, &new_layout, &dst_stage, &dst_access);
  if (discard) {
    old_layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }
  fn_image_barrier(cmd, image->handle, image->aspect, image->mip_levels, image->array_layers, old_layout, new_layout, src_stage, src_access, dst_stage, dst_access);
  image->state = new_state;
}

static VkAttachmentLoadOp fn_load_op(cREN_LoadOp op) {
  switch (op) {
  case CREN_LOAD_LOAD:
    return VK_ATTACHMENT_LOAD_OP_LOAD;
  case CREN_LOAD_DONT_CARE:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  case CREN_LOAD_CLEAR:
  default:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  }
}

static bool fn_resolve_color(cREN_Frames *frames, const cREN_ColorAttachment *a, VkImage *image, VkImageView *view, VkExtent2D *extent) {
  if ((a->swapchain != NULL) == (a->image != NULL)) {
    return false; // exactly one must be set
  }
  if (a->swapchain != NULL) {
    uint32_t index = 0;
    if (!cREN_frame_image_index(frames, a->swapchain, &index)) {
      return false;
    }
    *image = a->swapchain->images[index];
    *view = a->swapchain->views[index];
    *extent = a->swapchain->extent;
  } else {
    *image = a->image->handle;
    *view = a->image->view;
    *extent = a->image->extent;
  }
  return true;
}

cREN_CmdBuffer *cREN_cmd_begin(cREN_Frames *frames) {
  cREN_CmdBuffer *cmd = cREN_frame_command_buffer(frames);
  if (cmd == NULL) {
    return NULL;
  }

  VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
  };
  if (vkBeginCommandBuffer(cmd->handle, &begin_info) != VK_SUCCESS) {
    return NULL;
  }

  cmd->recorded = false;
  return cmd;
}

bool cREN_cmd_end(cREN_CmdBuffer *cmd) {
  if (cmd == NULL || vkEndCommandBuffer(cmd->handle) != VK_SUCCESS) {
    return false;
  }

  cmd->recorded = true;
  return true;
}

bool cREN_cmd_begin_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, const cREN_RenderingDesc *desc) {
  if (cmd == NULL || desc == NULL || (desc->color_count == 0 && desc->depth == NULL) ||
      (desc->color_count > 0 && desc->colors == NULL)) {
    return false;
  }

  // validate and find the render area, before recording anything
  VkExtent2D area = {0, 0};
  for (uint32_t i = 0; i < desc->color_count; ++i) {
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
    if (!fn_resolve_color(frames, &desc->colors[i], &image, &view, &extent)) {
      return false;
    }
    if (i == 0) {
      area = extent;
    }
  }
  if (desc->depth != NULL) {
    if (desc->depth->image == NULL) {
      return false;
    }
    if (desc->color_count == 0) {
      area = desc->depth->image->extent;
    }
  }

  // persistent attachment array: grows once, never shrinks, so no per-frame allocation
  if (desc->color_count > cmd->color_cap) {
    VkRenderingAttachmentInfo *grown = realloc(cmd->color_infos, desc->color_count * sizeof(*grown));
    if (grown == NULL) {
      return false;
    }
    cmd->color_infos = grown;
    cmd->color_cap = desc->color_count;
  }

  // barriers and attachment infos
  for (uint32_t i = 0; i < desc->color_count; ++i) {
    const cREN_ColorAttachment *a = &desc->colors[i];
    VkImage image;
    VkImageView view;
    VkExtent2D extent;
    fn_resolve_color(frames, a, &image, &view, &extent);

    const bool keep = a->load == CREN_LOAD_LOAD;
    if (a->image != NULL) {
      fn_transition(cmd->handle, a->image, CREN_IMAGE_STATE_COLOR_ATTACHMENT, !keep);
    } else if (!keep) {
      // swapchain image: the acquire semaphore is waited on at COLOR_ATTACHMENT_OUTPUT.
      // LOAD means an earlier pass this frame already put it in attachment layout.
      fn_image_barrier(cmd->handle, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }

    cmd->color_infos[i] = (VkRenderingAttachmentInfo){
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = fn_load_op(a->load),
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {a->clear.r, a->clear.g, a->clear.b, a->clear.a}}},
    };
  }

  VkRenderingAttachmentInfo depth_info = {0};
  if (desc->depth != NULL) {
    fn_transition(cmd->handle, desc->depth->image, CREN_IMAGE_STATE_DEPTH_ATTACHMENT, desc->depth->load != CREN_LOAD_LOAD);
    depth_info = (VkRenderingAttachmentInfo){
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = desc->depth->image->view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = fn_load_op(desc->depth->load),
        .storeOp = desc->depth->discard_result ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {desc->depth->clear_depth, 0}},
    };
  }

  VkRenderingInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = area},
      .layerCount = 1,
      .colorAttachmentCount = desc->color_count,
      .pColorAttachments = desc->color_count > 0 ? cmd->color_infos : NULL,
      .pDepthAttachment = desc->depth != NULL ? &depth_info : NULL,
  };
  vkCmdBeginRendering(cmd->handle, &rendering);
  return true;
}

void cREN_cmd_end_rendering(cREN_CmdBuffer *cmd) {
  if (cmd != NULL) {
    vkCmdEndRendering(cmd->handle);
  }
}

void cREN_cmd_bind_pipeline(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline) {
  if (cmd == NULL || pipeline == NULL) {
    return;
  }
  vkCmdBindPipeline(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
}

bool cREN_cmd_prepare_present(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc) {
  uint32_t index = 0;
  if (cmd == NULL || sc == NULL || !cREN_frame_image_index(frames, sc, &index)) {
    return false;
  }
  // the render-finished semaphore signal makes the writes available to the presentation engine
  fn_image_barrier(cmd->handle, sc->images[index], VK_IMAGE_ASPECT_COLOR_BIT, 1, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
  return true;
}

void cREN_cmd_transition_image(cREN_CmdBuffer *cmd, cREN_Image *image, cREN_ImageState state) {
  if (cmd == NULL || image == NULL) {
    return;
  }
  fn_transition(cmd->handle, image, state, false);
}

void cREN_cmd_set_viewport_scissor_size(cREN_CmdBuffer *cmd, uint32_t width, uint32_t height) {
  if (cmd == NULL) {
    return;
  }
  VkViewport viewport = {.width = (float)width, .height = (float)height, .maxDepth = 1.0f};
  VkRect2D scissor = {.extent = {width, height}};
  vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
  vkCmdSetScissor(cmd->handle, 0, 1, &scissor);
}

void cREN_cmd_set_viewport_scissor(cREN_CmdBuffer *cmd, const cREN_Swapchain *sc) {
  if (cmd == NULL || sc == NULL) {
    return;
  }
  VkViewport viewport = {
      .width = (float)sc->extent.width,
      .height = (float)sc->extent.height,
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  VkRect2D scissor = {.extent = sc->extent};
  vkCmdSetViewport(cmd->handle, 0, 1, &viewport);
  vkCmdSetScissor(cmd->handle, 0, 1, &scissor);
}

void cREN_cmd_draw(cREN_CmdBuffer *cmd, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance) {
  if (cmd == NULL) {
    return;
  }
  vkCmdDraw(cmd->handle, vertex_count, instance_count, first_vertex, first_instance);
}

void cREN_cmd_push_constants(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline, const void *data, uint32_t size) {
  if (cmd == NULL || pipeline == NULL || data == NULL || size == 0 || size > CREN_PUSH_CONSTANT_SIZE) {
    return;
  }
  vkCmdPushConstants(cmd->handle, pipeline->layout, pipeline->push_stages, 0, size, data);
}

void cREN_cmd_bind_index_buffer(cREN_CmdBuffer *cmd, const cREN_Buffer *buffer, uint64_t offset) {
  if (cmd == NULL || buffer == NULL) {
    return;
  }
  vkCmdBindIndexBuffer(cmd->handle, buffer->handle, offset, VK_INDEX_TYPE_UINT32);
}

void cREN_cmd_draw_indexed(cREN_CmdBuffer *cmd, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance) {
  if (cmd == NULL) {
    return;
  }
  vkCmdDrawIndexed(cmd->handle, index_count, instance_count, first_index, vertex_offset, first_instance);
}

void cREN_cmd_bind_bindless(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline, const cREN_Bindless *bindless) {
  if (cmd == NULL || pipeline == NULL || bindless == NULL) {
    return;
  }
  vkCmdBindDescriptorSets(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout, 0, 1, &bindless->set, 0, NULL);
}
