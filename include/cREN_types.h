#pragma once

#include <stdbool.h>

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
