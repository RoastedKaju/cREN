#pragma once

#include "cREN_context.h"
#include "cREN_device.h"

typedef struct cREN_Allocator cREN_Allocator;

cREN_Allocator *cREN_allocator_init(const cREN_Context *const ctx, const cREN_Device *const device);

void cREN_allocator_deinit(cREN_Allocator *allocator);