#pragma once

#include "cREN_context.h"
#include "cREN_device.h"

typedef struct cREN_Shader cREN_Shader;

cREN_Shader *cREN_shader_load(cREN_Context *ctx, cREN_Device *device, const char *path);

void cREN_shader_deinit(cREN_Context *ctx, cREN_Device *device, cREN_Shader *shader);