#pragma once

#include "cREN_allocator.h"
#include "cREN_context.h"
#include "cREN_device.h"
#include "cREN_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct cREN_Buffer cREN_Buffer;

typedef struct cREN_BufferDesc {
  uint64_t size;
  uint32_t usage; // cREN_BufferUsage flags
  cREN_MemoryType memory;
} cREN_BufferDesc;

cREN_Buffer *cREN_buffer_create(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, const cREN_BufferDesc *desc);

void cREN_buffer_destroy(cREN_Context *ctx, cREN_Allocator *allocator, cREN_Buffer *buffer);

void *cREN_buffer_mapped(const cREN_Buffer *buffer);

uint64_t cREN_buffer_address(const cREN_Buffer *buffer);

uint64_t cREN_buffer_size(const cREN_Buffer *buffer);

bool cREN_buffer_upload(cREN_Context *ctx, cREN_Device *device, cREN_Allocator *allocator, cREN_Buffer *dst, uint64_t offset, const void *data, uint64_t size);