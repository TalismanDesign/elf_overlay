#include "elf_overlay/overlay_loader.h"
#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/isa.h"
#include "blob_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void elf_overlay_xtensa_init(void);

/* Build a one-overlay blob whose body is a single 4-byte patch literal that
 * holds a bare constant (0xDEADBEEF) with no corresponding reloc. A patch
 * entry points at it, but no section map entry covers 0xDEADBEEF, so it must
 * be left untouched and page-in must still succeed. */
static void build_patch_constant_blob(const char *path) {
    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.app = "patch_constant_test";
    m.isa = ELF_OVERLAY_ISA_XTENSA;
    m.slot_enter = 0;
    m.slot_leave = 4;

    m.stub_block_size = 8;
    m.stub_block = calloc(1, m.stub_block_size);
    assert(m.stub_block);

    m.overlay_data_size = 4;
    m.overlay_data = calloc(1, m.overlay_data_size);
    assert(m.overlay_data);
    /* Seed a bare constant that no section map entry will cover. */
    uint32_t constant = 0xDEADBEEF;
    memcpy(m.overlay_data, &constant, sizeof(constant));

    m.overlays = calloc(1, sizeof(pack_overlay_t));
    assert(m.overlays);
    m.overlays[0].id = 1;
    m.overlays[0].offset = 0;
    m.overlays[0].length = m.overlay_data_size;
    m.overlay_count = 1;

    /* One patch entry at blob offset 0, no instruction re-encode. */
    m.patches = calloc(1, sizeof(pack_patch_t));
    assert(m.patches);
    m.patches[0].overlay_id = 1;
    m.patches[0].blob_offset = 0;
    m.patches[0].insn_offset = ELF_OVERLAY_INSN_OFFSET_NONE;
    m.patches[0].pad = 0;
    m.patch_count = 1;

    size_t strtab_off = sizeof(elf_overlay_header_t) + 12 * m.overlay_count +
                        12 * m.stub_count + 8 * m.redirect_count +
                        8 * m.patch_count + 8 * m.stub_patch_count +
                        12 * m.host_ref_count;
    size_t stub_block_off = (strtab_off + m.strtab_size + 3) & ~(size_t)3;
    size_t total = stub_block_off + m.stub_block_size + m.overlay_data_size;

    uint8_t *blob = malloc(total);
    assert(blob);
    size_t written = elf_overlay_pack_blob(&m, blob, total);
    assert(written == total);

    FILE *fp = fopen(path, "wb");
    assert(fp);
    fwrite(blob, 1, total, fp);
    fclose(fp);

    free(blob);
    elf_overlay_manifest_free(&m);
}

int main(void) {
    elf_overlay_xtensa_init();

    const char *path = "/tmp/patch_constant_test.elfo";
    build_patch_constant_blob(path);

    elf_overlay_loader_t *loader = NULL;
    int rc = elf_overlay_load(&loader, path);
    assert(rc == ELF_OVERLAY_OK);
    assert(loader != NULL);

    /* Section map covers nothing the constant lives at, so the literal is
     * unresolvable but page-in must still succeed (constant left untouched). */
    elf_overlay_section_map_t map = {0};
    map.entries[0].v_addr = 0x1000;
    map.entries[0].addr = 0x40001000;
    map.entries[0].size = 0x1000;
    rc = elf_overlay_set_section_map(loader, &map);
    assert(rc == ELF_OVERLAY_OK);

    elf_overlay_set_active(loader);
    void *body = elf_overlay_enter(0x00010000);
    assert(body != NULL);

    /* The constant 0xDEADBEEF must be left as-is, not replaced with garbage
     * or cause the page-in to fail. */
    uint32_t v;
    memcpy(&v, body, sizeof(v));
    assert(v == 0xDEADBEEF);

    elf_overlay_leave(0x00010000);
    elf_overlay_set_active(NULL);
    elf_overlay_unload(loader);

    remove(path);
    printf("patch constant test passed\n");
    return 0;
}
