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

/* Collect every .text relocation slot (RELATIVE + JMP_SLOT/GLOB_DAT) and
 * split them by type. Returns total count; fills relative_slots and
 * hostref_slots (each caller-provided, max 256). */
static size_t collect_text_relocs(const char *fixture, uint32_t *text_addr_out,
                                  uint32_t *relative_slots, size_t max_relative,
                                  uint32_t *hostref_slots, size_t max_hostref) {
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

    size_t relative_count = 0;
    size_t hostref_count = 0;
    size_t total = 0;
    for (size_t r = 0; r < reader.reloc_count; r++) {
        uint32_t type = reader.relocs[r].r_info & 0xff;
        uint32_t off = reader.relocs[r].r_offset;
        if (off < text->addr || off + 4 > text->addr + text->size) continue;
        total++;
        if (type == R_XTENSA_RELATIVE) {
            assert(relative_count < max_relative);
            relative_slots[relative_count++] = off;
        } else if (type == R_XTENSA_JMP_SLOT || type == R_XTENSA_GLOB_DAT) {
            assert(hostref_count < max_hostref);
            hostref_slots[hostref_count++] = off;
        }
    }

    elf_reader_close(&reader);
    free(data);
    return total;
}

/* Parse patch entries from an emitted .elfo; return blob offsets (relative to
 * .text start). */
static size_t collect_patch_slots(const char *elfo, uint32_t text_addr,
                                  uint32_t *slots, size_t max_slots) {
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

    size_t count = 0;
    for (size_t i = 0; i < hdr.patches; i++) {
        elf_overlay_patch_t p;
        assert(off + sizeof(p) <= size);
        memcpy(&p, data + off, sizeof(p));
        off += sizeof(p);

        assert(count < max_slots);
        slots[count++] = text_addr + p.blob_offset;
    }
    for (size_t i = 0; i < hdr.stub_patches; i++) {
        elf_overlay_patch_t p;
        assert(off + sizeof(p) <= size);
        memcpy(&p, data + off, sizeof(p));
        off += sizeof(p);

        assert(count < max_slots);
        slots[count++] = text_addr + p.blob_offset;
    }

    free(data);
    return count;
}

/* Parse host_ref entries from an emitted .elfo; return canonical addresses. */
static size_t collect_hostref_slots(const char *elfo, uint32_t text_addr,
                                    uint32_t *slots, size_t max_slots) {
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
    uint32_t expected_rel[256];
    uint32_t expected_hr[256];
    size_t total_relocs = collect_text_relocs(fixture, &text_addr,
                                              expected_rel, 256,
                                              expected_hr, 256);
    assert(total_relocs > 0 && "fixture must declare .text relocations");

    /* Patch entries must cover every .text R_XTENSA_RELATIVE slot. */
    uint32_t got_patch[256];
    size_t got_patch_count = collect_patch_slots(elfo, text_addr, got_patch, 256);

    /* Re-collect with clean separation: relative count and hostref count. */
    uint32_t rel_only[256];
    uint32_t hr_only[256];
    size_t rel_count = 0;
    size_t hr_count = 0;
    {
        size_t size = 0;
        uint8_t *data = read_file(fixture, &size);
        assert(data);
        elf_reader_t reader;
        assert(elf_reader_open(&reader, data, size) == 0);
        elf_reader_sections(&reader);
        elf_reader_relocs(&reader);
        elf_section_t *text = elf_section_by_name(&reader, ".text");
        for (size_t r = 0; r < reader.reloc_count; r++) {
            uint32_t type = reader.relocs[r].r_info & 0xff;
            uint32_t off = reader.relocs[r].r_offset;
            if (off < text->addr || off + 4 > text->addr + text->size) continue;
            if (type == R_XTENSA_RELATIVE) {
                assert(rel_count < 256);
                rel_only[rel_count++] = off;
            } else if (type == R_XTENSA_JMP_SLOT || type == R_XTENSA_GLOB_DAT) {
                assert(hr_count < 256);
                hr_only[hr_count++] = off;
            }
        }
        elf_reader_close(&reader);
        free(data);
    }

    /* Every RELATIVE slot must have a patch entry. */
    size_t missing_rel2 = 0;
    for (size_t e = 0; e < rel_count; e++) {
        int found = 0;
        for (size_t c = 0; c < got_patch_count; c++) {
            if (got_patch[c] == rel_only[e]) { found = 1; break; }
        }
        if (!found) {
            fprintf(stderr, "RELATIVE at 0x%x has no patch entry\n", rel_only[e]);
            missing_rel2++;
        }
    }

    /* Every JMP_SLOT/GLOB_DAT slot must have a host_ref. */
    uint32_t got_hr[256];
    size_t got_hr_count = collect_hostref_slots(elfo, text_addr, got_hr, 256);
    size_t missing_hr = 0;
    for (size_t e = 0; e < hr_count; e++) {
        int found = 0;
        for (size_t c = 0; c < got_hr_count; c++) {
            if (got_hr[c] == hr_only[e]) { found = 1; break; }
        }
        if (!found) {
            fprintf(stderr, "JMP_SLOT/GLOB_DAT at 0x%x has no host_ref\n", hr_only[e]);
            missing_hr++;
        }
    }

    /* Losslessness: no false positives. patch + host_ref entries must equal
     * the handled .text relocations (RELATIVE + JMP_SLOT/GLOB_DAT) exactly.
     * Other types (e.g. RTLD at the .text head for PLT stubs) are not patch
     * or host_ref entries and are excluded from this count. */
    size_t handled = rel_count + hr_count;
    size_t produced = got_patch_count + got_hr_count;
    int count_ok = (produced == handled);

    remove(relk);
    remove(elfo);

    assert(missing_rel2 == 0 && "every .text R_XTENSA_RELATIVE must have a patch entry");
    assert(missing_hr == 0 && "every .text JMP_SLOT/GLOB_DAT must have a host_ref");
    assert(count_ok &&
           "patch + host_ref entries must equal .text relocation count (no "
           "false positives, no misses)");
    printf("plt_hostrefs test passed (%zu relocs: %zu RELATIVE patched, %zu host refs, "
           "produced %zu entries, no false positives)\n",
           total_relocs, rel_count, hr_count, produced);
    return 0;
}
