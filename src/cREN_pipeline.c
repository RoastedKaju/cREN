#include "cREN_pipeline.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

cREN_Pipeline *cREN_pipeline_init(cREN_Context *ctx, cREN_Device *device, const cREN_PipelineDesc *desc) {
  if (ctx == NULL || device == NULL || desc == NULL || desc->shader == NULL ||
      desc->vertex_entry == NULL || desc->frag_entry == NULL ||
      (desc->color_format_count > 0 && desc->color_formats == NULL)) {
    return NULL;
  }

  // get color attachments
  VkFormat *color_formats = NULL;
  VkPipelineColorBlendAttachmentState *blend_attachments = NULL;
  if (desc->color_format_count > 0) {
    color_formats = malloc(desc->color_format_count * sizeof(*color_formats));
    blend_attachments = calloc(desc->color_format_count, sizeof(*blend_attachments));
    if (color_formats == NULL || blend_attachments == NULL) {
      free(color_formats);
      free(blend_attachments);
      return NULL;
    }
    for (uint32_t i = 0; i < desc->color_format_count; ++i) {
      color_formats[i] = fn_format_to_vk(desc->color_formats[i]);
      if (color_formats[i] == VK_FORMAT_UNDEFINED) {
        fprintf(stderr, "[cREN] Pipeline color format %u is undefined or unsupported\n", i);
        free(color_formats);
        free(blend_attachments);
        return NULL;
      }
      blend_attachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    }
  }
  const VkFormat depth_format = fn_format_to_vk(desc->depth_format);
  const bool depth_stencil_enabled = depth_format != VK_FORMAT_UNDEFINED;

  cREN_Pipeline *pipeline = calloc(1, sizeof(*pipeline));
  if (pipeline == NULL) {
    return NULL;
  }
  pipeline->push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkPushConstantRange push_range = {
      .stageFlags = pipeline->push_stages,
      .offset = 0,
      .size = CREN_PUSH_CONSTANT_SIZE,
  };

  VkDescriptorSetLayout set_layout = desc->bindless != NULL ? desc->bindless->layout : VK_NULL_HANDLE;
  VkPipelineLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = desc->bindless != NULL ? 1 : 0,
      .pSetLayouts = &set_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_range,
  };

  if (vkCreatePipelineLayout(device->logical, &layout_info, NULL, &pipeline->layout) != VK_SUCCESS) {
    free(pipeline);
    return NULL;
  }

  VkPipelineShaderStageCreateInfo stages[2] = {
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT,
          .module = desc->shader->module,
          .pName = desc->vertex_entry,
      },
      {
          .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
          .module = desc->shader->module,
          .pName = desc->frag_entry,
      },
  };

  VkPipelineVertexInputStateCreateInfo vertex_input = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  VkPipelineInputAssemblyStateCreateInfo input_assembly = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  VkPipelineViewportStateCreateInfo viewport = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1,
  };

  VkPipelineRasterizationStateCreateInfo raster = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_CLOCKWISE,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisample = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  VkPipelineColorBlendStateCreateInfo blend = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = desc->color_format_count,
      .pAttachments = blend_attachments,
  };

  VkPipelineDepthStencilStateCreateInfo depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = desc->depth_test ? VK_TRUE : VK_FALSE,
      .depthWriteEnable = desc->depth_write ? VK_TRUE : VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
  };

  const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2,
      .pDynamicStates = dynamic_states,
  };

  VkPipelineRenderingCreateInfo rendering = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = desc->color_format_count,
      .pColorAttachmentFormats = color_formats,
      .depthAttachmentFormat = depth_format,
  };

  VkGraphicsPipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = 2,
      .pStages = stages,
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = pipeline->layout,
      .pDepthStencilState = depth_stencil_enabled ? &depth_stencil : NULL,
  };
  if (vkCreateGraphicsPipelines(device->logical, VK_NULL_HANDLE, 1, &info, NULL, &pipeline->handle) != VK_SUCCESS) {
    fprintf(stderr, "[cREN] Failed to create graphics pipeline\n");
    vkDestroyPipelineLayout(device->logical, pipeline->layout, NULL);
    free(pipeline);
    free(color_formats);
    free(blend_attachments);
    return NULL;
  }

  free(color_formats);
  free(blend_attachments);

  return pipeline;
}

void cREN_pipeline_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Pipeline *pipeline) {
  if (ctx == NULL || device == NULL || pipeline == NULL) {
    return;
  }
  if (pipeline->handle != VK_NULL_HANDLE) {
    vkDestroyPipeline(device->logical, pipeline->handle, NULL);
  }
  if (pipeline->layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device->logical, pipeline->layout, NULL);
  }
  free(pipeline);
}
