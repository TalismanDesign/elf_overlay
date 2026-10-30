#include "blob_pack.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/isa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

int main() {
    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.app = "testapp";
    m.isa = ELF_OVERLAY_ISA_XTENSA;
    m.slot_enter = 0;
    m.slot_leave = 4;

    /* stub block: 8 bytes */
    m.stub_block_size = 8;
    m.stub_block = malloc(m.stub_block_size);
    memset(m.stub_block, 0xAA, m.stub_block_size);

    /* overlay data: 64 bytes */
    m.overlay_data_size = 64;
    m.overlay_data = malloc(m.overlay_data_size);
    memset(m.overlay_data, 0xBB, m.overlay_data_size);

    /* 1 overlay */
    m.overlays = calloc(1, sizeof(pack_overlay_t));
    m.overlays[0].id = 1;
    m.overlays[0].offset = 0;
    m.overlays[0].length = 64;
    m.overlay_count = 1;

    /* 1 stub */
    m.stubs = calloc(1, sizeof(pack_stub_t));
    m.stubs[0].canonical = 0x1000;
    m.stubs[0].overlay_id = 1;
    m.stubs[0].body_offset = 0;
    m.stubs[0].stub_off = 8;
    m.stub_count = 1;

    /* 1 redirect */
    m.redirects = calloc(1, sizeof(pack_redirect_t));
    m.redirects[0].site = 0x2000;
    m.redirects[0].stub = 8;
    m.redirect_count = 1;

    /* 1 patch */
    m.patches = calloc(1, sizeof(pack_patch_t));
    m.patches[0].overlay_id = 1;
    m.patches[0].blob_offset = 0x10;
    m.patches[0].insn_offset = 0xC;
    m.patches[0].pad = 0;
    m.patch_count = 1;

    /* 1 host ref with strtab */
    m.host_refs = calloc(1, sizeof(pack_hostref_t));
    m.host_refs[0].overlay_id = 1;
    m.host_refs[0].blob_offset = 0x14;
    m.host_refs[0].insn_offset = 0x10;
    m.host_refs[0].pad = 0;
    m.host_refs[0].name_off = 0;
    m.host_ref_count = 1;
    m.strtab = strdup("my_symbol");
    m.strtab_size = strlen("my_symbol") + 1;

    /* Serialize */
    uint8_t *ser = NULL;
    size_t ser_len = 0;
    assert(elf_overlay_manifest_serialize(&m, &ser, &ser_len) == 0);
    assert(ser_len > 0);
    assert(memcmp(ser, RELK_MAGIC, 4) == 0);

    /* Deserialize */
    pack_manifest_t m2;
    memset(&m2, 0, sizeof(m2));
    assert(elf_overlay_manifest_deserialize(&m2, ser, ser_len) == 0);
    assert(m2.isa == m.isa);
    assert(m2.slot_enter == m.slot_enter);
    assert(m2.slot_leave == m.slot_leave);
    assert(m2.stub_block_size == m.stub_block_size);
    assert(m2.overlay_data_size == m.overlay_data_size);
    assert(m2.overlay_count == m.overlay_count);
    assert(m2.stub_count == m.stub_count);
    assert(m2.redirect_count == m.redirect_count);
    assert(m2.patch_count == m.patch_count);
    assert(m2.host_ref_count == m.host_ref_count);
    assert(m2.strtab_size == m.strtab_size);
    assert(memcmp(m2.stub_block, m.stub_block, m.stub_block_size) == 0);
    assert(memcmp(m2.overlay_data, m.overlay_data, m.overlay_data_size) == 0);
    assert(m2.overlays[0].id == 1);
    assert(m2.overlays[0].offset == 0);
    assert(m2.overlays[0].length == 64);
    assert(m2.stubs[0].canonical == 0x1000);
    assert(m2.stubs[0].stub_off == 8);
    assert(m2.redirects[0].site == 0x2000);
    assert(m2.redirects[0].stub == 8);
    assert(m2.patches[0].blob_offset == 0x10);
    assert(m2.patches[0].insn_offset == 0xC);
    assert(m2.host_refs[0].name_off == 0);
    assert(strcmp(m2.strtab, "my_symbol") == 0);

    /* Pack to ELFO */
    uint8_t blob[512];
    size_t written = elf_overlay_pack_blob(&m2, blob, sizeof(blob));
    assert(written > 0);
    assert(memcmp(blob, ELF_OVERLAY_MAGIC, 4) == 0);

    elf_overlay_manifest_free(&m2);
    elf_overlay_manifest_free(&m);
    free(ser);

    printf("pack round-trip test passed\n");
    return 0;
}
