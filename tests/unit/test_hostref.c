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

#define RESOLVED_ADDR 0x40123456u

static uintptr_t resolver_ok(const char *name) {
    assert(name && strcmp(name, "host_symbol") == 0);
    return RESOLVED_ADDR;
}

static uintptr_t resolver_unresolved(const char *name) {
    (void)name;
    return 0; /* symbol not found */
}

/* Build a one-overlay blob whose body is a single 4-byte literal slot that a
 * single host_ref at blob offset 0 names "host_symbol". Writes it to path. */
static void build_hostref_blob(const char *path) {
    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.app = "hostref_test";
    m.isa = ELF_OVERLAY_ISA_XTENSA;
    m.slot_enter = 0;
    m.slot_leave = 4;

    m.stub_block_size = 8;
    m.stub_block = calloc(1, m.stub_block_size);
    assert(m.stub_block);

    m.overlay_data_size = 4;
    m.overlay_data = calloc(1, m.overlay_data_size);
    assert(m.overlay_data);

    m.overlays = calloc(1, sizeof(pack_overlay_t));
    assert(m.overlays);
    m.overlays[0].id = 1;
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

    static const char name[] = "host_symbol";
    m.strtab_size = 1 + sizeof(name); /* leading NUL byte + name */
    m.strtab = calloc(1, m.strtab_size);
    assert(m.strtab);
    memcpy(m.strtab + 1, name, sizeof(name));

    m.host_refs = calloc(1, sizeof(pack_hostref_t));
    assert(m.host_refs);
    m.host_refs[0].overlay_id = 1;
    m.host_refs[0].blob_offset = 0;
    m.host_refs[0].insn_offset = 0;
    m.host_refs[0].name_off = 1; /* name starts after the leading NUL */
    m.host_ref_count = 1;

    size_t strtab_off = sizeof(elf_overlay_header_t)
        + 12 * m.overlay_count
        + 12 * m.stub_count
        + 8 * m.redirect_count
        + 8 * m.patch_count
        + 8 * m.stub_patch_count
        + 12 * m.host_ref_count;
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

static uint32_t body_word(const void *body) {
    uint32_t v;
    memcpy(&v, body, sizeof(v));
    return v;
}

int main(void) {
    elf_overlay_xtensa_init();

    const char *path = "/tmp/hostref_test.elfo";
    build_hostref_blob(path);

    /* resolver returns a valid address -> enter succeeds and the
     * host_ref literal is patched to that address. */
    {
        elf_overlay_loader_t *loader = NULL;
        int rc = elf_overlay_load(&loader, path);
        assert(rc == ELF_OVERLAY_OK);
        rc = elf_overlay_set_symbol_resolver(loader, resolver_ok);
        assert(rc == ELF_OVERLAY_OK);

        elf_overlay_set_active(loader);
        void *body = elf_overlay_enter(0x00010000);
        assert(body != NULL);
        assert(body_word(body) == RESOLVED_ADDR);
        elf_overlay_leave(0x00010000);
        elf_overlay_set_active(NULL);
        elf_overlay_unload(loader);
    }

    /* resolver returns 0 (symbol not found) -> enter must abort the
     * page-in and return NULL rather than plant a null target. */
    {
        elf_overlay_loader_t *loader = NULL;
        int rc = elf_overlay_load(&loader, path);
        assert(rc == ELF_OVERLAY_OK);
        rc = elf_overlay_set_symbol_resolver(loader, resolver_unresolved);
        assert(rc == ELF_OVERLAY_OK);

        elf_overlay_set_active(loader);
        void *body = elf_overlay_enter(0x00010000);
        assert(body == NULL);
        elf_overlay_set_active(NULL);
        elf_overlay_unload(loader);
    }

    /* no resolver installed -> same abort, a host_ref cannot resolve
     * without one. */
    {
        elf_overlay_loader_t *loader = NULL;
        int rc = elf_overlay_load(&loader, path);
        assert(rc == ELF_OVERLAY_OK);

        elf_overlay_set_active(loader);
        void *body = elf_overlay_enter(0x00010000);
        assert(body == NULL);
        elf_overlay_set_active(NULL);
        elf_overlay_unload(loader);
    }

    remove(path);
    printf("hostref test passed\n");
    return 0;
}
