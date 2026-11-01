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