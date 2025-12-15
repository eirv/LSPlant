#pragma once

#include "xdl.h"

#ifdef __cplusplus
extern "C" {
#endif

void *xdl_dsym_prefix(void *handle, const char *symbol, size_t *symbol_size);

#ifdef __cplusplus
}
#endif
