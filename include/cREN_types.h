#pragma once

#include <stdbool.h>

#define CREN_MAX_MIP_LEVELS UINT32_MAX
#define CREN_BINDLESS_INVALID UINT32_MAX

/* function pointer type for surface creation */
typedef bool (*cREN_SurfaceCreateFn)(void *renderer_instance,
                                     void *native_window,
                                     void *user_data,
                                     void **native_surface);

/* function pointer type for surface destruction */
typedef void (*cREN_SurfaceDestroyFn)(void *renderer_instance,
                                      void *native_surface,
                                      void *user_data);

typedef enum cREN_FrameStatus {
  CREN_FRAME_OK = 0,
  CREN_FRAME_NEEDS_RESIZE,
  CREN_FRAME_ERROR,
} cREN_FrameStatus;

typedef enum cREN_BufferUsage {
  CREN_BUFFER_VERTEX = 1 << 0,
  CREN_BUFFER_INDEX = 1 << 1,
  CREN_BUFFER_STORAGE = 1 << 2,
  CREN_BUFFER_UNIFORM = 1 << 3,
  CREN_BUFFER_INDIRECT = 1 << 4,
  CREN_BUFFER_TRANSFER_SRC = 1 << 5,
  CREN_BUFFER_TRANSFER_DST = 1 << 6,
} cREN_BufferUsage;

typedef enum cREN_MemoryType {
  CREN_MEMORY_GPU_ONLY = 0, // device-local, filled through a staging copy
  CREN_MEMORY_UPLOAD,       // host-visible, persistently mapped, CPU writes sequentially
  CREN_MEMORY_READBACK,     // host-visible, persistently mapped, CPU reads
} cREN_MemoryType;

typedef enum cREN_Format {
  CREN_FORMAT_UNDEFINED = 0,
  // color
  CREN_FORMAT_BGRA8_UNORM,
  CREN_FORMAT_BGRA8_SRGB,
  CREN_FORMAT_RGBA8_UNORM,
  CREN_FORMAT_RGBA8_SRGB,
  CREN_FORMAT_RGBA16_FLOAT,
  CREN_FORMAT_R11G11B10_FLOAT,
  // depth
  CREN_FORMAT_D32_FLOAT,
  CREN_FORMAT_D32_FLOAT_S8_UINT,
  CREN_FORMAT_D24_UNORM_S8_UINT,
} cREN_Format;

typedef enum cREN_ImageType {
  CREN_IMAGE_TYPE_2D = 0,
  CREN_IMAGE_TYPE_3D,
  CREN_IMAGE_TYPE_CUBE,
} cREN_ImageType;

typedef enum cREN_ImageViewType {
  CREN_VIEW_2D = 0,
  CREN_VIEW_2D_ARRAY,
  CREN_VIEW_CUBE,
  CREN_VIEW_CUBE_ARRAY,
  CREN_VIEW_3D,
} cREN_ImageViewType;

typedef enum cREN_ImageUsage {
  CREN_IMAGE_COLOR_ATTACHMENT = 1 << 0,
  CREN_IMAGE_DEPTH_ATTACHMENT = 1 << 1,
  CREN_IMAGE_SAMPLED = 1 << 2,
  CREN_IMAGE_STORAGE = 1 << 3,
  CREN_IMAGE_TRANSFER_SRC = 1 << 4,
  CREN_IMAGE_TRANSFER_DST = 1 << 5,
} cREN_ImageUsage;

typedef enum cREN_ImageState {
  CREN_IMAGE_STATE_UNDEFINED = 0,
  CREN_IMAGE_STATE_COLOR_ATTACHMENT,
  CREN_IMAGE_STATE_DEPTH_ATTACHMENT,
  CREN_IMAGE_STATE_SHADER_READ,
  CREN_IMAGE_STATE_TRANSFER_SRC,
  CREN_IMAGE_STATE_TRANSFER_DST,
} cREN_ImageState;

typedef enum cREN_LoadOp {
  CREN_LOAD_CLEAR = 0,
  CREN_LOAD_LOAD,
  CREN_LOAD_DONT_CARE,
} cREN_LoadOp;

typedef enum cREN_Filter {
  CREN_FILTER_NEAREST = 0,
  CREN_FILTER_LINEAR
} cREN_Filter;

typedef enum cREN_AddressMode {
  CREN_ADDRESS_REPEAT = 0,
  CREN_ADDRESS_MIRRORED_REPEAT,
  CREN_ADDRESS_CLAMP_TO_EDGE,
  CREN_ADDRESS_CLAMP_TO_BORDER,
} cREN_AddressMode;

typedef enum cREN_CompareOp { // anything but NONE makes it a shadow (comparison) sampler
  CREN_COMPARE_NONE = 0,
  CREN_COMPARE_LESS,
  CREN_COMPARE_LESS_EQUAL,
  CREN_COMPARE_GREATER,
  CREN_COMPARE_GREATER_EQUAL,
} cREN_CompareOp;

typedef enum cREN_BorderColor {
  CREN_BORDER_TRANSPARENT_BLACK = 0,
  CREN_BORDER_OPAQUE_BLACK,
  CREN_BORDER_OPAQUE_WHITE,
} cREN_BorderColor;

typedef enum cREN_BindlessTableType {
  CREN_TABLE_2D = 0, // these five match cREN_ImageViewType's order exactly
  CREN_TABLE_2D_ARRAY,
  CREN_TABLE_CUBE,
  CREN_TABLE_CUBE_ARRAY,
  CREN_TABLE_3D,
  CREN_TABLE_SAMPLER,
  CREN_TABLE_COUNT, // one binding per table
} cREN_BindlessTableType;
