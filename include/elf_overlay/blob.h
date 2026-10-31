#pragma once

#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int elf_overlay_blob_validate(const void *data, size_t size, elf_overlay_header_t *header);

#ifdef __cplusplus
}
#endif
