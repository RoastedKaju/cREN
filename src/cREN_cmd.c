#include "cREN_cmd.h"
#include "cREN_internal.h"

static void fn_image_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
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
      .subresourceRange = {
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .levelCount = 1,
          .layerCount = 1,
      },
  };
  VkDependencyInfo dependency = {
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = 1,
      .pImageMemoryBarriers = &barrier,
  };
  vkCmdPipelineBarrier2(cmd, &dependency);
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

bool cREN_cmd_begin_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc, cREN_ClearColor clear) {
  uint32_t index = 0;
  if (cmd == NULL || sc == NULL || !cREN_frame_image_index(frames, sc, &index)) {
    return false;
  }

  fn_image_barrier(cmd->handle, sc->images[index], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

  VkRenderingAttachmentInfo color = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = sc->views[index],
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      .clearValue = {.color = {.float32 = {clear.r, clear.g, clear.b, clear.a}}},
  };

  VkRenderingInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {.extent = sc->extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color,
  };
  vkCmdBeginRendering(cmd->handle, &rendering);
  return true;
}

bool cREN_cmd_end_rendering(cREN_CmdBuffer *cmd, cREN_Frames *frames, cREN_Swapchain *sc) {
  uint32_t index = 0;
  if (cmd == NULL || sc == NULL || !cREN_frame_image_index(frames, sc, &index)) {
    return false;
  }

  vkCmdEndRendering(cmd->handle);

  fn_image_barrier(cmd->handle, sc->images[index], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);

  return true;
}

void cREN_cmd_bind_pipeline(cREN_CmdBuffer *cmd, const cREN_Pipeline *pipeline) {
  if (cmd == NULL || pipeline == NULL) {
    return;
  }
  vkCmdBindPipeline(cmd->handle, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle);
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
  if (cmd == NULL || pipeline == NULL || data == NULL || size == 0) {
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
