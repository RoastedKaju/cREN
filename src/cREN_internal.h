#pragma once

#include "cREN_types.h"

#include <vk_mem_alloc.h>
#include <volk.h>

static inline VkFormat fn_format_to_vk(cREN_Format format) {
  switch (format) {
  case CREN_FORMAT_BGRA8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case CREN_FORMAT_BGRA8_SRGB:
    return VK_FORMAT_B8G8R8A8_SRGB;
  case CREN_FORMAT_RGBA8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case CREN_FORMAT_RGBA8_SRGB:
    return VK_FORMAT_R8G8B8A8_SRGB;
  case CREN_FORMAT_RGBA16_FLOAT:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case CREN_FORMAT_R11G11B10_FLOAT:
    return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
  case CREN_FORMAT_D32_FLOAT:
    return VK_FORMAT_D32_SFLOAT;
  case CREN_FORMAT_D32_FLOAT_S8_UINT:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  case CREN_FORMAT_D24_UNORM_S8_UINT:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case CREN_FORMAT_UNDEFINED:
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

static inline cREN_Format fn_format_from_vk(VkFormat format) {
  switch (format) {
  case VK_FORMAT_B8G8R8A8_UNORM:
    return CREN_FORMAT_BGRA8_UNORM;
  case VK_FORMAT_B8G8R8A8_SRGB:
    return CREN_FORMAT_BGRA8_SRGB;
  case VK_FORMAT_R8G8B8A8_UNORM:
    return CREN_FORMAT_RGBA8_UNORM;
  case VK_FORMAT_R8G8B8A8_SRGB:
    return CREN_FORMAT_RGBA8_SRGB;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return CREN_FORMAT_RGBA16_FLOAT;
  case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
    return CREN_FORMAT_R11G11B10_FLOAT;
  case VK_FORMAT_D32_SFLOAT:
    return CREN_FORMAT_D32_FLOAT;
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return CREN_FORMAT_D32_FLOAT_S8_UINT;
  case VK_FORMAT_D24_UNORM_S8_UINT:
    return CREN_FORMAT_D24_UNORM_S8_UINT;
  default:
    return CREN_FORMAT_UNDEFINED;
  }
}

typedef struct cREN_Context {
  uint32_t api_ver;
  VkInstance instance;
  VkDebugUtilsMessengerEXT debug_messenger;
} cREN_Context;

typedef struct cREN_Device {
  VkPhysicalDevice physical;
  VkPhysicalDeviceProperties props;
  VkDevice logical;
  uint32_t universal_family;
  VkQueue universal_queue;
  VkSemaphore timeline;
  uint64_t timeline_value;
} cREN_Device;

typedef struct cREN_Allocator {
  VmaAllocator handle;
} cREN_Allocator;

typedef struct cREN_Buffer {
  VkBuffer handle;
  VmaAllocation allocation;
  VkDeviceSize size;
  cREN_BufferUsage usage;
  void *mapped;
  VkDeviceAddress address;
} cREN_Buffer;

typedef struct cREN_Surface {
  void *handle;
  cREN_SurfaceCreateFn create_fn;
  cREN_SurfaceDestroyFn destroy_fn;
  void *user_data;
} cREN_Surface;

typedef struct cREN_Swapchain {
  VkSwapchainKHR handle;
  VkSurfaceKHR surface;
  VkFormat format;
  VkColorSpaceKHR color_space;
  VkExtent2D requested_extent; // native window size
  VkExtent2D extent;           // actual swapchain size
  uint32_t image_count;
  VkImage *images;
  VkImageView *views;
  bool vsync;
  uint32_t acquire_count;
  VkSemaphore *acquires;        // one binary semaphore per frame slot
  VkSemaphore *render_finished; // one binary semaphore per swapchain image
  bool needs_resize;            // set by acquire or present, cleared by successful rebuild
} cREN_Swapchain;

typedef struct cREN_CmdBuffer {
  VkCommandBuffer handle;
  bool recorded;
  VkRenderingAttachmentInfo *color_infos;
  uint32_t color_cap;
} cREN_CmdBuffer;

typedef struct cREN_FrameSlot {
  VkCommandPool pool;
  cREN_CmdBuffer cmd;
  uint64_t submit_value; // timeline value that signals when work is done
} cREN_FrameSlot;

typedef struct cREN_Acquired {
  cREN_Swapchain *swapchain;
  uint32_t image_index;
} cREN_Acquired;

typedef struct cREN_Frames {
  cREN_FrameSlot *slots;
  uint32_t count;
  uint32_t current;
  cREN_Acquired *acquired;
  uint32_t acquired_count;
  uint32_t acquired_cap;
  VkSemaphoreSubmitInfo *waits;
  VkSemaphoreSubmitInfo *signals; // cap + 1 entries (one render-finished per window + the timeline)
  VkSemaphore *present_waits;
  VkSwapchainKHR *chains;
  uint32_t *indices;
  VkResult *results;
} cREN_Frames;

typedef struct cREN_Shader {
  VkShaderModule module;
} cREN_Shader;

typedef struct cREN_Pipeline {
  VkPipeline handle;
  VkPipelineLayout layout;
  VkShaderStageFlags push_stages;
} cREN_Pipeline;

typedef struct cREN_Image {
  VkImage handle;
  VkImageView view; // default view, used when the image is a render attachment
  VmaAllocation allocation;
  VkFormat format;
  VkExtent2D extent;
  uint32_t depth; // 1 unless 3D
  uint32_t mip_levels;
  uint32_t array_layers; // faces included for cubes
  cREN_ImageType type;
  VkImageAspectFlags aspect; // for barriers, depth plus stencil for combined formats
  cREN_ImageState state;     // whole-image state, tracked at record time
  cREN_ImageUsage usage;
} cREN_Image;

typedef struct cREN_ImageView {
  VkImageView handle;
  VkImageViewType type;
  const cREN_Image *image; // descriptors will need the image's usage and format
  cREN_ImageViewType kind; // our own type, which doubles as the bindless table index
} cREN_ImageView;

typedef struct cREN_Sampler {
  VkSampler handle;
} cREN_Sampler;

typedef struct cREN_BindlessTable {
  uint32_t capacity;
  uint32_t *free_stack;
  uint32_t free_count;
} cREN_BindlessTable;

typedef struct cREN_Bindless {
  VkDevice logical;
  VkDescriptorSetLayout layout;
  VkDescriptorPool pool;
  VkDescriptorSet set;
  cREN_BindlessTable tables[CREN_TABLE_COUNT];
} cREN_Bindless;
