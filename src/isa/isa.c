#include "elf_overlay/isa.h"
#include <stddef.h>

#define MAX_ISA_OPS 8

static const elf_overlay_isa_ops_t *g_isa_ops[MAX_ISA_OPS];
static int g_isa_count = 0;

int elf_overlay_isa_register(const elf_overlay_isa_ops_t *ops) {
    if (!ops || g_isa_count >= MAX_ISA_OPS) return -1;
    g_isa_ops[g_isa_count++] = ops;
    return 0;
}

const elf_overlay_isa_ops_t *elf_overlay_isa_get(uint16_t isa_id) {
    for (int i = 0; i < g_isa_count; i++) {
        if (g_isa_ops[i]->id == isa_id) {
            return g_isa_ops[i];
        }
    }
    return NULL;
}

const elf_overlay_isa_ops_t *elf_overlay_isa_get_default(void) {
    return elf_overlay_isa_get(ELF_OVERLAY_ISA_XTENSA);
}
