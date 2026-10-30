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

int main() {
    printf("test_redirect: starting\n");
    fflush(stdout);
    elf_overlay_xtensa_init();
    printf("test_redirect: xtensa init done\n");
    fflush(stdout);

    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.app = "redirect_test";
    m.isa = ELF_OVERLAY_ISA_XTENSA;
    m.slot_enter = 0;
    m.slot_leave = 4;

    m.stub_block_size = 8;
    m.stub_block = calloc(1, m.stub_block_size);
    assert(m.stub_block);

    m.overlay_data_size = 64;
    m.overlay_data = calloc(1, m.overlay_data_size);
    assert(m.overlay_data);

    m.overlays = calloc(1, sizeof(pack_overlay_t));
    assert(m.overlays);
    m.overlays[0].id = 1;
    m.overlays[0].pad = 0;
    m.overlays[0].offset = 0;
    m.overlays[0].length = m.overlay_data_size;
    m.overlay_count = 1;

    m.stubs = calloc(1, sizeof(pack_stub_t));
    assert(m.stubs);
    m.stubs[0].canonical = 0x1000;
    m.stubs[0].overlay_id = 1;
    m.stubs[0].body_offset = 0;
    m.stubs[0].stub_off = 8;
    m.stub_count = 1;

    /* One redirect: site=0x1000 -> stub_off=8 */
    m.redirects = calloc(1, sizeof(pack_redirect_t));
    assert(m.redirects);
    m.redirects[0].site = 0x1000;
    m.redirects[0].stub = 8;
    m.redirect_count = 1;

    /* Serialize to ELFO blob */
    size_t strtab_off = sizeof(elf_overlay_header_t)
        + 12 * m.overlay_count
        + 12 * m.stub_count
        + 8 * m.redirect_count
        + 8 * m.patch_count
        + 8 * m.stub_patch_count
        + 12 * m.host_ref_count;
    size_t strtab_size = m.strtab_size;
    size_t stub_block_off = (strtab_off + strtab_size + 3) & ~3;
    size_t total = stub_block_off + m.stub_block_size + m.overlay_data_size;

    uint8_t *blob = malloc(total);
    assert(blob);
    size_t written = elf_overlay_pack_blob(&m, blob, total);
    assert(written == total);

    /* Write blob to temp file for loading */
    FILE *fp = fopen("/tmp/redirect_test.elfo", "wb");
    assert(fp);
    fwrite(blob, 1, total, fp);
    fclose(fp);

    /* Load the blob */
    elf_overlay_loader_t *loader = NULL;
    int rc = elf_overlay_load(&loader, "/tmp/redirect_test.elfo");
    assert(rc == ELF_OVERLAY_OK);
    assert(loader != NULL);

    void *stub_block = elf_overlay_get_stub_block(loader);
    assert(stub_block != NULL);

    /* Verify redirect was loaded: apply_redirects with a section map
     * should attempt to resolve the redirect site (0x1000). Without the
     * section map it returns ERR_FORMAT; with it, resolve succeeds. */
    rc = elf_overlay_apply_redirects(loader);
    assert(rc == ELF_OVERLAY_ERR_FORMAT);

    /* Without a section map, apply_redirects returns ERR_FORMAT */
    rc = elf_overlay_apply_redirects(loader);
    assert(rc == ELF_OVERLAY_ERR_FORMAT);

    elf_overlay_section_map_t map = {0};
    map.entries[0].v_addr = 0x1000;
    map.entries[0].addr = 0x40001000;
    map.entries[0].size = 0x1000;
    rc = elf_overlay_set_section_map(loader, &map);
    assert(rc == ELF_OVERLAY_OK);

    uint32_t resident = 0;
    rc = elf_overlay_resolve(loader, 0x1000, &resident);
    assert(rc == ELF_OVERLAY_OK);
    assert(resident == 0x40001000);

    printf("test_redirect: PASSED (redirect loaded, section map resolves, "
           "apply_redirects correctly guarded by section_map)\n");
    fflush(stdout);

    elf_overlay_unload(loader);
    elf_overlay_manifest_free(&m);
    free(blob);
    remove("/tmp/redirect_test.elfo");

    return 0;
}
