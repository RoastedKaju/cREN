#include "cREN_bindless.h"
#include "cREN_internal.h"

#include <stdio.h>
#include <stdlib.h>

static uint32_t fn_pop(cREN_BindlessTable *t) {
  return t->free_count != 0 ? t->free_stack[--t->free_count] : CREN_BINDLESS_INVALID;
}

static void fn_push(cREN_BindlessTable *t, uint32_t index) {
  if (index < t->capacity && t->free_count < t->capacity) {
    t->free_stack[t->free_count++] = index;
  }
}

static void fn_release(cREN_Bindless *b) {
  for (uint32_t i = 0; i < CREN_TABLE_COUNT; ++i) {
    free(b->tables[i].free_stack);
  }
  if (b->pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(b->logical, b->pool, NULL); // frees the set too
  }
  if (b->layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(b->logical, b->layout, NULL);
  }
  free(b);
}

cREN_Bindless *cREN_bindless_init(cREN_Context *ctx, cREN_Device *device, const cREN_BindlessDesc *desc) {
  if (ctx == NULL || device == NULL || desc == NULL) {
    return NULL;
  }

  const uint32_t max_images = desc->max_images != 0 ? desc->max_images : 4096;
  const uint32_t max_samplers = desc->max_samplers != 0 ? desc->max_samplers : 256;
  const uint32_t total_images = max_images * 5;

  // update-after-bind limits are separate from the normal ones
  VkPhysicalDeviceVulkan12Properties p12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
  VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &p12};
  vkGetPhysicalDeviceProperties2(device->physical, &p2);
  if (total_images > p12.maxPerStageDescriptorUpdateAfterBindSampledImages ||
      total_images > p12.maxDescriptorSetUpdateAfterBindSampledImages ||
      max_samplers > p12.maxPerStageDescriptorUpdateAfterBindSamplers ||
      max_samplers > p12.maxDescriptorSetUpdateAfterBindSamplers) {
    fprintf(stderr, "[cREN] Bindless sizes exceed device limits (images %u, samplers %u)\n", total_images, max_samplers);
    return NULL;
  }

  cREN_Bindless *b = calloc(1, sizeof(*b));
  if (b == NULL) {
    return NULL;
  }
  b->logical = device->logical;

  // slot tables: allocated once here, so add/free never allocate
  for (uint32_t t = 0; t < CREN_TABLE_COUNT; ++t) {
    cREN_BindlessTable *table = &b->tables[t];
    table->capacity = t == CREN_TABLE_SAMPLER ? max_samplers : max_images;
    table->free_stack = malloc(table->capacity * sizeof(*table->free_stack));
    if (table->free_stack == NULL) {
      fn_release(b);
      return NULL;
    }
    for (uint32_t i = 0; i < table->capacity; ++i) {
      table->free_stack[i] = table->capacity - 1 - i;
    }
    table->free_count = table->capacity;
  }

  // layout: one binding per table, all partially bound and updatable while in use
  VkDescriptorSetLayoutBinding bindings[CREN_TABLE_COUNT];
  VkDescriptorBindingFlags flags[CREN_TABLE_COUNT];
  for (uint32_t t = 0; t < CREN_TABLE_COUNT; ++t) {
    bindings[t] = (VkDescriptorSetLayoutBinding){
        .binding = t,
        .descriptorType = t == CREN_TABLE_SAMPLER ? VK_DESCRIPTOR_TYPE_SAMPLER : VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        .descriptorCount = b->tables[t].capacity,
        .stageFlags = VK_SHADER_STAGE_ALL,
    };
    flags[t] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
               VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
               VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
  }
  VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = CREN_TABLE_COUNT,
      .pBindingFlags = flags,
  };
  VkDescriptorSetLayoutCreateInfo layout_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &flags_info,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = CREN_TABLE_COUNT,
      .pBindings = bindings,
  };
  if (vkCreateDescriptorSetLayout(device->logical, &layout_info, NULL, &b->layout) != VK_SUCCESS) {
    fn_release(b);
    return NULL;
  }

  // pool: exactly one set
  VkDescriptorPoolSize sizes[2] = {
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, total_images},
      {VK_DESCRIPTOR_TYPE_SAMPLER, max_samplers},
  };
  VkDescriptorPoolCreateInfo pool_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = 2,
      .pPoolSizes = sizes,
  };
  if (vkCreateDescriptorPool(device->logical, &pool_info, NULL, &b->pool) != VK_SUCCESS) {
    fn_release(b);
    return NULL;
  }

  VkDescriptorSetAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = b->pool,
      .descriptorSetCount = 1,
      .pSetLayouts = &b->layout,
  };
  if (vkAllocateDescriptorSets(device->logical, &alloc_info, &b->set) != VK_SUCCESS) {
    fn_release(b);
    return NULL;
  }
  return b;
}

void cREN_bindless_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Bindless *bindless) {
  if (ctx == NULL || device == NULL || bindless == NULL) {
    return;
  }
  fn_release(bindless);
}

uint32_t cREN_bindless_add_image(cREN_Bindless *b, const cREN_ImageView *view) {
  if (b == NULL || view == NULL || (uint32_t)view->kind >= CREN_TABLE_SAMPLER) {
    return CREN_BINDLESS_INVALID;
  }
  if (!(view->image->usage & CREN_IMAGE_SAMPLED)) {
    fprintf(stderr, "[cREN] Bindless image lacks CREN_IMAGE_SAMPLED\n");
    return CREN_BINDLESS_INVALID;
  }

  const uint32_t table = (uint32_t)view->kind;
  const uint32_t index = fn_pop(&b->tables[table]);
  if (index == CREN_BINDLESS_INVALID) {
    fprintf(stderr, "[cREN] Bindless table %u is full\n", table);
    return CREN_BINDLESS_INVALID;
  }

  VkDescriptorImageInfo info = {
      .imageView = view->handle,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
  };
  VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = b->set,
      .dstBinding = table,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = &info,
  };
  vkUpdateDescriptorSets(b->logical, 1, &write, 0, NULL);
  return index;
}

uint32_t cREN_bindless_add_sampler(cREN_Bindless *b, const cREN_Sampler *sampler) {
  if (b == NULL || sampler == NULL) {
    return CREN_BINDLESS_INVALID;
  }
  const uint32_t index = fn_pop(&b->tables[CREN_TABLE_SAMPLER]);
  if (index == CREN_BINDLESS_INVALID) {
    fprintf(stderr, "[cREN] Bindless sampler table is full\n");
    return CREN_BINDLESS_INVALID;
  }

  VkDescriptorImageInfo info = {.sampler = sampler->handle};
  VkWriteDescriptorSet write = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = b->set,
      .dstBinding = CREN_TABLE_SAMPLER,
      .dstArrayElement = index,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = &info,
  };
  vkUpdateDescriptorSets(b->logical, 1, &write, 0, NULL);
  return index;
}

void cREN_bindless_free_image(cREN_Bindless *b, cREN_ImageViewType type, uint32_t index) {
  if (b != NULL && (uint32_t)type < CREN_TABLE_SAMPLER) {
    fn_push(&b->tables[(uint32_t)type], index);
  }
}

void cREN_bindless_free_sampler(cREN_Bindless *b, uint32_t index) {
  if (b != NULL) {
    fn_push(&b->tables[CREN_TABLE_SAMPLER], index);
  }
}