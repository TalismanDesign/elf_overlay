#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/isa.h"
#include "blob_pack.h"
#include "elf_read.h"
#include "reloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

void elf_overlay_xtensa_init(void);
void elf_overlay_riscv_init(void);

#ifndef EM_RISCV
#define EM_RISCV 243
#endif
#ifndef EM_ARM
#define EM_ARM 40
#endif

#define STUB_SLOTS_SIZE ELF_OVERLAY_STUB_SLOTS_SIZE
#define XTENSA_STUB_STRIDE ELF_OVERLAY_XTENSA_STUB_STRIDE

/* st_info low nibble is the symbol type; STT_FUNC marks a function. Only
 * functions get stubs, so a sized data symbol living in .text is skipped. */
#define STT_FUNC 2
#define ELF_ST_TYPE(info) ((info) & 0xf)

/* Read a 32-bit word from ELF section data at the given virtual address.
 * Returns 0 if the address is not in any loaded section. */
static uint32_t read_word_at(elf_reader_t *reader, uint32_t addr) {
    elf_section_t *sec = elf_section_by_addr(reader, addr);
    if (!sec || !sec->data || addr < sec->addr || addr + 4 > sec->addr + sec->size) {
        return 0;
    }
    size_t off = addr - sec->addr;
    uint32_t val;
    memcpy(&val, sec->data + off, 4);
    return val;
}

static bool reloc_is_host_symbol(uint32_t type) {
    return type == R_XTENSA_JMP_SLOT || type == R_XTENSA_GLOB_DAT;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: elf_overlay_relink <elf_file> <output.relk>\n");
        return 2;
    }

    const char *elf_path = argv[1];
    const char *out_path = argv[2];

    FILE *fp = fopen(elf_path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", elf_path);
        return 2;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot seek %s\n", elf_path);
        fclose(fp);
        return 2;
    }
    long fsize = ftell(fp);
    if (fsize <= 0) {
        fprintf(stderr, "empty or unseekable %s\n", elf_path);
        fclose(fp);
        return 2;
    }
    rewind(fp);
    uint8_t *data = malloc((size_t)fsize);
    if (!data) {
        fclose(fp);
        return 2;
    }
    if (fread(data, 1, (size_t)fsize, fp) != (size_t)fsize) {
        fprintf(stderr, "short read on %s\n", elf_path);
        free(data);
        fclose(fp);
        return 2;
    }
    fclose(fp);

    elf_reader_t reader;
    if (elf_reader_open(&reader, data, fsize) != 0) {
        fprintf(stderr, "not an ELF32 file\n");
        free(data);
        return 2;
    }

    if (reader.machine == EM_RISCV) {
        elf_overlay_riscv_init();
    } else {
        elf_overlay_xtensa_init();
    }

    elf_reader_sections(&reader);
    elf_reader_symbols(&reader);
    elf_reader_relocs(&reader);

    /* Determine ISA from machine field */
    uint16_t isa = ELF_OVERLAY_ISA_XTENSA;
    if (reader.machine == EM_RISCV)
        isa = ELF_OVERLAY_ISA_RISCV;
    else if (reader.machine == EM_ARM)
        isa = ELF_OVERLAY_ISA_ARM;

    /* Find .text section */
    elf_section_t *text = elf_section_by_name(&reader, ".text");
    if (!text || !text->data || text->size == 0) {
        fprintf(stderr, "no .text section\n");
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    /* The overlay format stores blob_offset/insn_offset as 16-bit; a .text
     * larger than 64 KiB cannot be represented. Reject it up front rather than
     * silently wrapping those fields later. */
    if (text->size > 0xFFFF) {
        fprintf(stderr, "error: .text is %zu bytes; the overlay format caps it at 65535\n",
                (size_t)text->size);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    /* Literal sites come from the ELF's own dynamic relocs, not from decoding
     * .text. The start of .text is a literal pool, so a linear sweep never
     * lines up with real instruction boundaries: it invents sites and misses
     * real ones. Every literal that must change at load time has a reloc (it
     * is exactly the set esp_elf patches in its resident copy), so this list
     * is exact by construction. n is only an upper bound for table sizing. */
    size_t n = reader.reloc_count;

    printf("machine=%u isa=%u .text=0x%x+%zu literal_relocs=%zu\n", reader.machine, isa,
           (unsigned int)text->addr, (size_t)text->size, n);

    size_t overlay_data_size = text->size;

    uint8_t *overlay_data = calloc(1, overlay_data_size);
    if (!overlay_data) {
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    memcpy(overlay_data, text->data, text->size);

    /* Build patches/host refs: one per patch site */
    pack_patch_t *patches = calloc(n ? n : 1, sizeof(pack_patch_t));
    if (!patches) {
        free(overlay_data);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    size_t host_ref_cap = n + reader.reloc_count;
    if (host_ref_cap == 0) host_ref_cap = 1;
    pack_hostref_t *host_refs = calloc(host_ref_cap, sizeof(pack_hostref_t));
    if (!host_refs) {
        free(patches);
        free(overlay_data);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    char *strtab = calloc(1, 1);
    size_t strtab_size = 1;
    if (!strtab) {
        free(host_refs);
        free(patches);
        free(overlay_data);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    size_t patch_count = 0;
    size_t host_ref_count = 0;
    for (size_t r = 0; r < reader.reloc_count; r++) {
        uint32_t type = reader.relocs[r].r_info & 0xff;
        uint32_t lit_addr = reader.relocs[r].r_offset;
        if (lit_addr < text->addr || lit_addr + 4 > text->addr + text->size) continue;
        uint16_t blob_offset = (uint16_t)(lit_addr - text->addr);

        /* Literal-pool patching only: the l32r stays PC-relative to its pool
         * slot, and the whole .text block relocates together, so the original
         * encoding still resolves. No instruction re-encode is requested. */
        uint16_t insn_offset = ELF_OVERLAY_INSN_OFFSET_NONE;

        if (type == R_XTENSA_RELATIVE) {
            /* Word at lit_addr holds an address that needs the load delta applied. */
            bool dup = false;
            for (size_t p = 0; p < patch_count; p++) {
                if (patches[p].blob_offset == blob_offset) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            patches[patch_count].overlay_id = 1;
            patches[patch_count].blob_offset = blob_offset;
            patches[patch_count].insn_offset = insn_offset;
            patches[patch_count].pad = 0;
            patch_count++;
        } else if (reloc_is_host_symbol(type)) {
            const char *sym = reader.relocs[r].symbol;
            if (!sym || !sym[0]) continue;

            size_t name_off = strtab_size;
            size_t name_len = strlen(sym) + 1;
            char *next = realloc(strtab, strtab_size + name_len);
            if (!next) {
                free(strtab);
                free(host_refs);
                free(patches);
                free(overlay_data);
                elf_reader_close(&reader);
                free(data);
                return 2;
            }
            strtab = next;
            memcpy(strtab + name_off, sym, name_len);
            strtab_size += name_len;

            host_refs[host_ref_count].overlay_id = 1;
            host_refs[host_ref_count].blob_offset = blob_offset;
            host_refs[host_ref_count].insn_offset = insn_offset;
            host_refs[host_ref_count].pad = 0;
            host_refs[host_ref_count].name_off = (uint32_t)name_off;
            host_ref_count++;
        }
    }

    pack_redirect_t *redirects = NULL;
    size_t redirect_count = 0;
    size_t redirect_cap = reader.reloc_count;
    if (redirect_cap > 0) {
        redirects = calloc(redirect_cap, sizeof(pack_redirect_t));
        if (!redirects) {
            free(host_refs);
            free(strtab);
            free(patches);
            free(overlay_data);
            elf_reader_close(&reader);
            free(data);
            return 2;
        }
    }

    /* Build pack_manifest_t */
    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    m.app = "relink_app";
    m.isa = isa;
    m.slot_enter = 0;
    m.slot_leave = 4;

    /* Stub block: 8 bytes (enter + leave slots) */
    m.stub_block_size = 8;
    m.stub_block = calloc(1, m.stub_block_size);

    /* 1 overlay covering all .text functions */
    m.overlays = calloc(1, sizeof(pack_overlay_t));
    m.overlays[0].id = 1;
    m.overlays[0].pad = 0;
    m.overlays[0].offset = 0;
    m.overlays[0].length = (uint32_t)overlay_data_size;
    m.overlay_count = 1;

    m.overlay_data = overlay_data;
    m.overlay_data_size = overlay_data_size;

    /* One stub per function in .text. */
    size_t func_count = 0;
    for (size_t i = 0; i < reader.symbol_count; i++) {
        elf_symbol_t *sym = &reader.symbols[i];
        if (!sym->name || sym->size == 0) continue;
        if (ELF_ST_TYPE(sym->info) != STT_FUNC) continue;
        if (sym->shndx >= reader.section_count) continue;
        elf_section_t *sec = &reader.sections[sym->shndx];
        if (sec->addr != text->addr || strcmp(sec->name, text->name) != 0) continue;
        func_count++;
    }
    if (func_count == 0) {
        fprintf(stderr, "no functions in .text\n");
        free(host_refs);
        free(strtab);
        free(patches);
        free(redirects);
        free(overlay_data);
        free(m.stub_block);
        free(m.overlays);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    m.stubs = calloc(func_count, sizeof(pack_stub_t));
    if (!m.stubs) {
        free(host_refs);
        free(strtab);
        free(patches);
        free(redirects);
        free(overlay_data);
        free(m.stub_block);
        free(m.overlays);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    size_t s = 0;
    for (size_t i = 0; i < reader.symbol_count; i++) {
        elf_symbol_t *sym = &reader.symbols[i];
        if (!sym->name || sym->size == 0) continue;
        if (ELF_ST_TYPE(sym->info) != STT_FUNC) continue;
        if (sym->shndx >= reader.section_count) continue;
        elf_section_t *sec = &reader.sections[sym->shndx];
        if (sec->addr != text->addr || strcmp(sec->name, text->name) != 0) continue;
        m.stubs[s].canonical = sym->value;
        m.stubs[s].overlay_id = 1;
        m.stubs[s].body_offset = (uint16_t)(sym->value - text->addr);
        m.stubs[s].stub_off = STUB_SLOTS_SIZE + (uint32_t)(func_count * sizeof(uint32_t)) +
                              (uint32_t)(s * XTENSA_STUB_STRIDE);
        s++;
    }
    m.stub_count = func_count;
    m.stub_block_size =
        STUB_SLOTS_SIZE + (func_count * sizeof(uint32_t)) + (func_count * XTENSA_STUB_STRIDE);
    free(m.stub_block);
    m.stub_block = calloc(1, m.stub_block_size);
    if (!m.stub_block) {
        free(host_refs);
        free(strtab);
        free(patches);
        free(redirects);
        free(overlay_data);
        free(m.overlays);
        free(m.stubs);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    for (size_t r = 0; r < reader.reloc_count; r++) {
        uint32_t info = reader.relocs[r].r_info;
        uint32_t type = info & 0xff;
        if (type != R_XTENSA_RELATIVE) continue;

        uint32_t r_offset = reader.relocs[r].r_offset;
        elf_section_t *sec = elf_section_by_addr(&reader, r_offset);
        if (sec && sec->addr == text->addr && strcmp(sec->name, text->name) == 0) {
            continue;
        }

        uint32_t word = read_word_at(&reader, r_offset);
        if (word == 0) continue;

        for (size_t i = 0; i < m.stub_count; i++) {
            if (word != m.stubs[i].canonical) continue;
            redirects[redirect_count].site = r_offset;
            redirects[redirect_count].stub = m.stubs[i].stub_off;
            redirect_count++;
            break;
        }
    }

    pack_patch_t *regular_patches = calloc(patch_count ? patch_count : 1, sizeof(pack_patch_t));
    pack_patch_t *stub_patches = calloc(patch_count ? patch_count : 1, sizeof(pack_patch_t));
    if (!regular_patches || !stub_patches) {
        free(host_refs);
        free(strtab);
        free(patches);
        free(redirects);
        free(overlay_data);
        free(regular_patches);
        free(stub_patches);
        free(m.stub_block);
        free(m.overlays);
        free(m.stubs);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    size_t regular_patch_count = 0;
    size_t stub_patch_count = 0;
    for (size_t i = 0; i < patch_count; i++) {
        uint32_t pool_word = 0;
        memcpy(&pool_word, overlay_data + patches[i].blob_offset, 4);

        bool is_stub = false;
        for (size_t sidx = 0; sidx < m.stub_count; sidx++) {
            if (pool_word != m.stubs[sidx].canonical) continue;
            uint32_t stub_off = m.stubs[sidx].stub_off;
            memcpy(overlay_data + patches[i].blob_offset, &stub_off, 4);
            stub_patches[stub_patch_count++] = patches[i];
            is_stub = true;
            break;
        }
        if (!is_stub) {
            regular_patches[regular_patch_count++] = patches[i];
        }
    }

    free(patches);
    patches = NULL;

    m.patches = regular_patches;
    m.patch_count = regular_patch_count;
    m.stub_patches = stub_patches;
    m.stub_patch_count = stub_patch_count;
    m.host_refs = host_refs;
    m.host_ref_count = host_ref_count;
    m.strtab = strtab;
    m.strtab_size = strtab_size;

    /* Redirects */
    m.redirects = redirects;
    m.redirect_count = redirect_count;

    /* Serialize to .relk */
    uint8_t *ser = NULL;
    size_t ser_len = 0;
    if (elf_overlay_manifest_serialize(&m, &ser, &ser_len) != 0) {
        fprintf(stderr, "serialize failed\n");
        elf_overlay_manifest_free(&m);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "cannot open %s for write\n", out_path);
        free(ser);
        elf_overlay_manifest_free(&m);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    fwrite(ser, 1, ser_len, fp);
    fclose(fp);

    printf("wrote %zu bytes to %s\n", ser_len, out_path);
    printf("redirects=%zu\n", redirect_count);

    free(ser);
    elf_overlay_manifest_free(&m);
    elf_reader_close(&reader);
    free(data);
    return 0;
}
