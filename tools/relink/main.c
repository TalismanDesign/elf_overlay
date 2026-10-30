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

void elf_overlay_xtensa_init(void);
void elf_overlay_riscv_init(void);

#ifndef EM_RISCV
#define EM_RISCV 243
#endif
#ifndef EM_ARM
#define EM_ARM 40
#endif

#define POOL_ALIGN 4

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

/* Read a 32-bit word from ELF section data at the given virtual address.
 * Returns 0 if the address is not in any loaded section. */
static uint32_t read_word_at(elf_reader_t *reader, uint32_t addr) {
    elf_section_t *sec = elf_section_by_addr(reader, addr);
    if (!sec || !sec->data || addr < sec->addr ||
        addr + 4 > sec->addr + sec->size) {
        return 0;
    }
    size_t off = addr - sec->addr;
    uint32_t val;
    memcpy(&val, sec->data + off, 4);
    return val;
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
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc(fsize);
    if (!data) { fclose(fp); return 2; }
    fread(data, 1, fsize, fp);
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
    if (reader.machine == EM_RISCV) isa = ELF_OVERLAY_ISA_RISCV;
    else if (reader.machine == EM_ARM) isa = ELF_OVERLAY_ISA_ARM;

    const elf_overlay_isa_ops_t *isa_ops = elf_overlay_isa_get(isa);
    if (!isa_ops) {
        fprintf(stderr, "no ISA ops for machine %u\n", reader.machine);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    /* Find .text section */
    elf_section_t *text = elf_section_by_name(&reader, ".text");
    if (!text || !text->data || text->size == 0) {
        fprintf(stderr, "no .text section\n");
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    /* Scan for l32r/auipc sites */
    elf_overlay_patch_site_t sites[256];
    size_t n = 256;
    if (isa_ops->find_patch_sites(text->data, text->size, text->addr, sites, &n) != 0) {
        fprintf(stderr, "failed to scan patch sites\n");
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    printf("machine=%u isa=%u .text=0x%x+%zu patch_sites=%zu\n",
           reader.machine, isa, (unsigned int)text->addr, (size_t)text->size, n);

    /* Build overlay data: code + padding + literal pool */
    size_t code_end = text->size;
    size_t pool_off = align_up(code_end, POOL_ALIGN);
    size_t pool_bytes = n * 4; /* one 4-byte literal per site */
    size_t overlay_data_size = pool_off + pool_bytes;

    uint8_t *overlay_data = calloc(1, overlay_data_size);
    if (!overlay_data) {
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    memcpy(overlay_data, text->data, text->size);

    /* Build patches: one per patch site */
    pack_patch_t *patches = calloc(n ? n : 1, sizeof(pack_patch_t));
    if (!patches) { free(overlay_data); elf_reader_close(&reader); free(data); return 2; }

    /* Build literal pool entries */
    uint32_t *pool = calloc(n ? n : 1, sizeof(uint32_t));
    if (!pool) { free(patches); free(overlay_data); elf_reader_close(&reader); free(data); return 2; }

    size_t patch_count = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t lit_addr = sites[i].imm; /* literal address from ISA decoding */
        uint32_t lit_val = 0;

        /* Read the literal value from the target address */
        elf_section_t *sec = elf_section_by_addr(&reader, lit_addr);
        if (sec && sec->data && lit_addr >= sec->addr &&
            lit_addr + 4 <= sec->addr + sec->size) {
            size_t off_in_sec = lit_addr - sec->addr;
            memcpy(&lit_val, sec->data + off_in_sec, 4);
        } else {
            /* Try relocations: find a relocation at this address */
            for (size_t r = 0; r < reader.reloc_count; r++) {
                if (reader.relocs[r].r_offset == lit_addr) {
                    lit_val = reader.relocs[r].r_addend;
                    break;
                }
            }
        }

        pool[i] = lit_val;

        /* Record patch: overlay_id=1, blob_offset=pool_off + i*4, insn_offset=site */
        patches[patch_count].overlay_id = 1;
        patches[patch_count].blob_offset = (uint16_t)(pool_off + i * 4);
        patches[patch_count].insn_offset = sites[i].insn_offset;
        patches[patch_count].pad = 0;
        patch_count++;
    }

    /* Resolve redirects
     *
     * Paged sections = sections containing overlay functions (.text here).
     * Skip relocations in paged sections (they're handled by patches).
     */
    pack_redirect_t *redirects = NULL;
    size_t redirect_count = 0;
    size_t redirect_cap = reader.reloc_count;
    if (redirect_cap > 0) {
        redirects = calloc(redirect_cap, sizeof(pack_redirect_t));
        if (!redirects) {
            free(pool); free(patches); free(overlay_data);
            elf_reader_close(&reader);
            free(data);
            return 2;
        }
    }

    for (size_t r = 0; r < reader.reloc_count; r++) {
        uint32_t info = reader.relocs[r].r_info;
        uint32_t type = info & 0xff;
        if (type != R_XTENSA_RELATIVE) {
            continue;
        }
        uint32_t r_offset = reader.relocs[r].r_offset;

        /* Skip relocations in paged sections (.text here) */
        elf_section_t *sec = elf_section_by_addr(&reader, r_offset);
        if (sec && sec == text) {
            continue;
        }

        /* Read the word at the relocation offset */
        uint32_t word = read_word_at(&reader, r_offset);
        if (word == 0) continue;

        /* Does this word point to a paged function? */
        /* All functions are in overlay 1, canonical address = function address */
        uint32_t func_addr = text->addr; /* stub canonical = .text addr */
        if (word != func_addr) continue;

        redirects[redirect_count].site = r_offset;
        redirects[redirect_count].stub = 8; /* stub_off = 8 (after enter/leave slots) */
        redirect_count++;
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

    /* 1 overlay */
    m.overlays = calloc(1, sizeof(pack_overlay_t));
    m.overlays[0].id = 1;
    m.overlays[0].pad = 0;
    m.overlays[0].offset = 0;
    m.overlays[0].length = (uint32_t)overlay_data_size;
    m.overlay_count = 1;

    m.overlay_data = overlay_data;
    m.overlay_data_size = overlay_data_size;

    /* 1 stub pointing at overlay 1 */
    m.stubs = calloc(1, sizeof(pack_stub_t));
    m.stubs[0].canonical = text->addr;
    m.stubs[0].overlay_id = 1;
    m.stubs[0].body_offset = 0;
    m.stubs[0].stub_off = 8;
    m.stub_count = 1;

    /* Patches */
    m.patches = patches;
    m.patch_count = patch_count;

    /* Redirects */
    m.redirects = redirects;
    m.redirect_count = redirect_count;

    /* Serialize to .relk */
    uint8_t *ser = NULL;
    size_t ser_len = 0;
    if (elf_overlay_manifest_serialize(&m, &ser, &ser_len) != 0) {
        fprintf(stderr, "serialize failed\n");
        free(pool); free(patches); free(redirects); free(overlay_data);
        free(m.stub_block); free(m.overlays); free(m.stubs);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }

    fp = fopen(out_path, "wb");
    if (!fp) {
        fprintf(stderr, "cannot open %s for write\n", out_path);
        free(ser);
        free(pool); free(patches); free(redirects); free(overlay_data);
        free(m.stub_block); free(m.overlays); free(m.stubs);
        elf_reader_close(&reader);
        free(data);
        return 2;
    }
    fwrite(ser, 1, ser_len, fp);
    fclose(fp);

    printf("wrote %zu bytes to %s\n", ser_len, out_path);
    printf("redirects=%zu\n", redirect_count);

    free(ser);
    free(pool);
    elf_overlay_manifest_free(&m);
    elf_reader_close(&reader);
    free(data);
    return 0;
}
