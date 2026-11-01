#include "reloc.h"
#include <stdlib.h>
#include <string.h>

/* Xtensa relocation type numbers, per binutils include/elf/xtensa.h. The
 * dynamic types (RTLD/GLOB_DAT/JMP_SLOT/RELATIVE) come from reloc.h so there is
 * a single source of truth; only the extra static-type names are defined here.
 * CALL16 is an assembler operand syntax, not an ELF reloc type, so it is not
 * listed. */
#define R_XTENSA_NONE 0
#define R_XTENSA_32 1
#define R_XTENSA_PLT 6
#define R_XTENSA_OP0 8
#define R_XTENSA_OP1 9
#define R_XTENSA_OP2 10
#define R_XTENSA_ASM_EXPAND 11
#define R_XTENSA_ASM_SIMPLIFY 12
#define R_XTENSA_32_PCREL 14
#define R_XTENSA_SLOT0_OP 20

#define ELF32_R_SYM(info) ((info) >> 8)
#define ELF32_R_TYPE(info) ((info) & 0xff)

static const char *reloc_type_name(uint32_t type) {
    switch (type) {
        case R_XTENSA_NONE:
            return "R_XTENSA_NONE";
        case R_XTENSA_32:
            return "R_XTENSA_32";
        case R_XTENSA_RTLD:
            return "R_XTENSA_RTLD";
        case R_XTENSA_GLOB_DAT:
            return "R_XTENSA_GLOB_DAT";
        case R_XTENSA_JMP_SLOT:
            return "R_XTENSA_JMP_SLOT";
        case R_XTENSA_RELATIVE:
            return "R_XTENSA_RELATIVE";
        case R_XTENSA_PLT:
            return "R_XTENSA_PLT";
        case R_XTENSA_ASM_EXPAND:
            return "R_XTENSA_ASM_EXPAND";
        case R_XTENSA_SLOT0_OP:
            return "R_XTENSA_SLOT0_OP";
        default:
            return "R_XTENSA_UNKNOWN";
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
