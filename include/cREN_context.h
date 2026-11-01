#pragma once

/* make version public define */
#define CREN_MAKE_VERSION(major, minor, patch) (((uint32_t)(major) << 22) | ((uint32_t)(minor) << 12) | (uint32_t)(patch))

/* forward declaration for internal context type */
typedef struct cREN_Context cREN_Context;

cREN_Context *cREN_context_init(void);

void cREN_context_deinit(cREN_Context *ctx);