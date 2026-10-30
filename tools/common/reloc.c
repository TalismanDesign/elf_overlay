#include "reloc.h"
#include <stdlib.h>
#include <string.h>

/* Xtensa relocation type numbers */
#define R_XTENSA_NONE 0
#define R_XTENSA_32 1
#define R_XTENSA_ASM_EXPAND 2
#define R_XTENSA_SLOT0_OP 3
#define R_XTENSA_SLOT1_OP 4
#define R_XTENSA_SLOT2_OP 5
#define R_XTENSA_SLOT3_OP 6
#define R_XTENSA_SLOT4_OP 7
#define R_XTENSA_SLOT5_OP 8
#define R_XTENSA_SLOT6_OP 9
#define R_XTENSA_SLOT7_OP 10
#define R_XTENSA_SLOT8_OP 11
#define R_XTENSA_SLOT9_OP 12
#define R_XTENSA_SLOT10_OP 13
#define R_XTENSA_SLOT11_OP 14
#define R_XTENSA_SLOT_OP 15
#define R_XTENSA_32_PCREL 16
#define R_XTENSA_GLOB_DAT 17
#define R_XTENSA_JMP_SLOT 18
#define R_XTENSA_RELATIVE 19
#define R_XTENSA_TLS_DTPMOD 20
#define R_XTENSA_TLS_DTPOFF 21
#define R_XTENSA_TLS_TPOFF 22
#define R_XTENSA_TLS_GD 23
#define R_XTENSA_TLS_LDM 24
#define R_XTENSA_TLS_DTPMOD64 25
#define R_XTENSA_TLS_DTPOFF64 26
#define R_XTENSA_TLS_TPOFF64 27
#define R_XTENSA_TLS_GD64 28
#define R_XTENSA_TLS_LDM64 29
#define R_XTENSA_CALL16 30
#define R_XTENSA_COPY 31

#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((info) & 0xff)

static const char *reloc_type_name(uint32_t type) {
    switch (type) {
        case R_XTENSA_NONE: return "R_XTENSA_NONE";
        case R_XTENSA_32: return "R_XTENSA_32";
        case R_XTENSA_RELATIVE: return "R_XTENSA_RELATIVE";
        case R_XTENSA_JMP_SLOT: return "R_XTENSA_JMP_SLOT";
        case R_XTENSA_CALL16: return "R_XTENSA_CALL16";
        default: return "R_XTENSA_UNKNOWN";
    }
}

int parse_relocations(elf_reader_t *reader, relocation_t **out, size_t *count) {
    if (!reader || !out || !count) return -1;
    
    size_t total = reader->reloc_count;
    if (total == 0) {
        *out = NULL;
        *count = 0;
        return 0;
    }
    
    relocation_t *rels = calloc(total, sizeof(relocation_t));
    if (!rels) return -1;
    
    for (size_t i = 0; i < total; i++) {
        uint32_t info = reader->relocs[i].r_info;
        rels[i].offset = reader->relocs[i].r_offset;
        rels[i].type = reloc_type_name(ELF32_R_TYPE(info));
        
        /* Look up symbol name from symtab using symbol index in r_info */
        uint32_t sym_idx = ELF32_R_SYM(info);
        if (reader->symbol_count > 0 && sym_idx < reader->symbol_count &&
            reader->symbols[sym_idx].name) {
            rels[i].symbol = reader->symbols[sym_idx].name;
        } else {
            rels[i].symbol = NULL;
        }
    }
    
    *out = rels;
    *count = total;
    return 0;
}
