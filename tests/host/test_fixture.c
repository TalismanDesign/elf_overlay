#include "elf_overlay/overlay_loader.h"
#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/isa.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void elf_overlay_xtensa_init(void);

static uint32_t word_at(const void *base, uint32_t off) {
    uint32_t v;
    memcpy(&v, (const uint8_t *)base + off, sizeof(v));
    return v;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <elfo-blob>\n", argv[0]);
        return 2;
    }

    elf_overlay_xtensa_init();

    elf_overlay_loader_t *loader = NULL;
    int rc = elf_overlay_load(&loader, argv[1]);
    assert(rc == ELF_OVERLAY_OK);
    assert(loader != NULL);

    void *stub_block = elf_overlay_get_stub_block(loader);
    assert(stub_block != NULL);

    /* Enter/leave slots wired */
    assert(word_at(stub_block, 0) == (uint32_t)(uintptr_t)&elf_overlay_enter);
    assert(word_at(stub_block, 4) == (uint32_t)(uintptr_t)&elf_overlay_leave);

    /* Read stub canonical address from blob */
    uint32_t stub_canonical = 0;
    {
        elf_overlay_header_t hdr;
        FILE *fp = fopen(argv[1], "rb");
        assert(fp);
        fread(&hdr, 1, sizeof(hdr), fp);
        fclose(fp);
        assert(hdr.stubs > 0);
        size_t off = sizeof(hdr) + (size_t)hdr.overlays * 12;
        elf_overlay_stub_t st;
        FILE *fp2 = fopen(argv[1], "rb");
        assert(fp2);
        fseek(fp2, (long)off, SEEK_SET);
        fread(&st, 1, sizeof(st), fp2);
        fclose(fp2);
        stub_canonical = st.canonical;
    }

    /* Section map resolution */
    elf_overlay_section_map_t map = {0};
    map.entries[0].v_addr = stub_canonical;
    map.entries[0].addr = 0x40000000;
    map.entries[0].size = 0x1000;
    rc = elf_overlay_set_section_map(loader, &map);
    assert(rc == ELF_OVERLAY_OK);

    uint32_t resident = 0;
    rc = elf_overlay_resolve(loader, stub_canonical, &resident);
    assert(rc == ELF_OVERLAY_OK);

    /* Stub lookup */
    uint32_t stub_off = 0;
    rc = elf_overlay_find_stub(loader, stub_canonical, &stub_off);
    assert(rc == ELF_OVERLAY_OK);

    /* First enter: pages in overlay, refcount = 1 */
    elf_overlay_set_active(loader);
    void *body = elf_overlay_enter(0x00010000);
    assert(body != NULL);
    assert(((uintptr_t)body & 3) == 0);

    /* Re-enter same overlay: refcount = 2 */
    void *body2 = elf_overlay_enter(0x00010000);
    assert(body2 == body);

    /* Leave once: refcount = 1 */
    elf_overlay_leave(0x00010000);

    /* Leave again: refcount = 0, frame becomes evictable */
    elf_overlay_leave(0x00010000);

    /* Enter again after leave: pages in fresh (refcount = 1) */
    void *body3 = elf_overlay_enter(0x00010000);
    assert(body3 != NULL);

    elf_overlay_leave(0x00010000);

    elf_overlay_unload(loader);
    elf_overlay_set_active(NULL);

    printf("fixture load test passed\n");
    return 0;
}
