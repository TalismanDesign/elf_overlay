#include "elf_overlay/blob.h"
#include "elf_overlay/errors.h"
#include <string.h>
#include <stdlib.h>

int elf_overlay_blob_validate(const void *data, size_t size, elf_overlay_header_t *header) {
    if (!data || size < sizeof(elf_overlay_header_t)) {
        return ELF_OVERLAY_ERR_FORMAT;
    }

    const elf_overlay_header_t *hdr = (const elf_overlay_header_t *)data;

    if (memcmp(hdr->magic, ELF_OVERLAY_MAGIC, 4) != 0) {
        return ELF_OVERLAY_ERR_FORMAT;
    }

    if (hdr->version != ELF_OVERLAY_VERSION) {
        return ELF_OVERLAY_ERR_FORMAT;
    }

    if (header) {
        memcpy(header, hdr, sizeof(elf_overlay_header_t));
    }

    return ELF_OVERLAY_OK;
}
