#pragma once

#include "cREN_context.h"

typedef struct cREN_Device cREN_Device;

cREN_Device *cREN_device_init(cREN_Context *ctx);

void cREN_device_deinit(cREN_Device *device);