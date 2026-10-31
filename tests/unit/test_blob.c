#include "elf_overlay/blob.h"
#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>

int main(void) {
    elf_overlay_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));

    /* Valid header */
    memcpy(hdr.magic, ELF_OVERLAY_MAGIC, 4);
    hdr.version = ELF_OVERLAY_VERSION;
    assert(elf_overlay_blob_validate(&hdr, sizeof(hdr), NULL) == ELF_OVERLAY_OK);

    /* Bad magic */
    memcpy(hdr.magic, "XXXX", 4);
    assert(elf_overlay_blob_validate(&hdr, sizeof(hdr), NULL) != ELF_OVERLAY_OK);

    /* Bad version */
    memcpy(hdr.magic, ELF_OVERLAY_MAGIC, 4);
    hdr.version = 99;
    assert(elf_overlay_blob_validate(&hdr, sizeof(hdr), NULL) != ELF_OVERLAY_OK);

    /* Too small */
    memcpy(hdr.magic, ELF_OVERLAY_MAGIC, 4);
    hdr.version = ELF_OVERLAY_VERSION;
    assert(elf_overlay_blob_validate(&hdr, sizeof(hdr) - 1, NULL) != ELF_OVERLAY_OK);

    /* NULL data */
    assert(elf_overlay_blob_validate(NULL, sizeof(hdr), NULL) != ELF_OVERLAY_OK);

    printf("blob test passed\n");
    return 0;
}
