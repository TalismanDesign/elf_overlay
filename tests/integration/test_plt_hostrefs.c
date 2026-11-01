#include "elf_overlay/overlay_format.h"
#include "elf_read.h"
#include "reloc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) {
        fclose(fp);
        return NULL;
    }
    uint8_t *buf = malloc((size_t)size);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    *out_size = (size_t)size;
    return buf;
}

static size_t collect_expected_slots(const char *fixture, uint32_t *slots, size_t max_slots,
                                     uint32_t *text_addr_out) {
    size_t size = 0;
    uint8_t *data = read_file(fixture, &size);
    assert(data && "fixture ELF must be readable");

    elf_reader_t reader;
    assert(elf_reader_open(&reader, data, size) == 0);
    elf_reader_sections(&reader);
    elf_reader_symbols(&reader);
    elf_reader_relocs(&reader);

    elf_section_t *text = elf_section_by_name(&reader, ".text");
    assert(text && text->size > 0);
    *text_addr_out = text->addr;

    size_t count = 0;
    for (size_t r = 0; r < reader.reloc_count; r++) {
        uint32_t type = reader.relocs[r].r_info & 0xff;
        if (type != R_XTENSA_JMP_SLOT && type != R_XTENSA_GLOB_DAT) continue;
        uint32_t off = reader.relocs[r].r_offset;
        if (off < text->addr || off + 4 > text->addr + text->size) continue;
        assert(count < max_slots);
        slots[count++] = off;
    }

    elf_reader_close(&reader);
    free(data);
    return count;
}

/* The .text slots the emitted .elfo's host_refs patch. */
static size_t collect_hostref_slots(const char *elfo, uint32_t text_addr, uint32_t *slots,
                                    size_t max_slots) {
    size_t size = 0;
    uint8_t *data = read_file(elfo, &size);
    assert(data && "emitted .elfo must be readable");
    assert(size >= sizeof(elf_overlay_header_t));

    elf_overlay_header_t hdr;
    memcpy(&hdr, data, sizeof(hdr));
    assert(memcmp(hdr.magic, ELF_OVERLAY_MAGIC, 4) == 0);

    size_t off = sizeof(elf_overlay_header_t);
    off += 12u * hdr.overlays;
    off += 12u * hdr.stubs;
    off += 8u * hdr.redirects;
    off += 8u * hdr.patches;
    off += 8u * hdr.stub_patches;

    size_t count = 0;
    for (uint16_t i = 0; i < hdr.host_refs; i++) {
        elf_overlay_hostref_t hr;
        assert(off + sizeof(hr) <= size);
        memcpy(&hr, data + off, sizeof(hr));
        off += sizeof(hr);

        const char *name = (const char *)(data + hdr.strtab_off + hr.name_off);
        assert(name[0] != '\0' && "host_ref must name a symbol");

        assert(count < max_slots);
        slots[count++] = text_addr + hr.blob_offset;
    }

    free(data);
    return count;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <relink-exe> <emit-exe> <fixture.tool>\n", argv[0]);
        return 2;
    }
    const char *relink = argv[1];
    const char *emit = argv[2];
    const char *fixture = argv[3];

    char relk[512];
    char elfo[512];
    snprintf(relk, sizeof(relk), "%s.relk", fixture);
    snprintf(elfo, sizeof(elfo), "%s.test.elfo", fixture);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"", relink, fixture, relk);
    assert(system(cmd) == 0 && "relink must succeed");
    snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\" \"%s\"", emit, relk, elfo);
    assert(system(cmd) == 0 && "emit must succeed");

    uint32_t text_addr = 0;
    uint32_t expected[64];
    size_t expected_count = collect_expected_slots(fixture, expected, 64, &text_addr);
    assert(expected_count > 0 && "fixture must declare host-symbol GOT slots");

    uint32_t covered[256];
    size_t covered_count = collect_hostref_slots(elfo, text_addr, covered, 256);

    size_t missing = 0;
    for (size_t e = 0; e < expected_count; e++) {
        int found = 0;
        for (size_t c = 0; c < covered_count; c++) {
            if (covered[c] == expected[e]) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "JMP_SLOT at 0x%x has no host_ref\n", expected[e]);
            missing++;
        }
    }

    remove(relk);
    remove(elfo);

    assert(missing == 0 && "every .text JMP_SLOT/GLOB_DAT must have a host_ref");
    printf("plt_hostrefs test passed (%zu GOT slots, all covered)\n", expected_count);
    return 0;
}
