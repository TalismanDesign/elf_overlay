#include "elf_overlay/isa.h"
#include <stddef.h>

#define MAX_ISA_OPS 8

static const elf_overlay_isa_ops_t *g_isa_ops[MAX_ISA_OPS];
static int g_isa_count = 0;
static int g_builtin_isa_init = 0;

void elf_overlay_xtensa_init(void);
void elf_overlay_riscv_init(void);

static void elf_overlay_isa_init_builtins(void) {
    if (g_builtin_isa_init) return;
    g_builtin_isa_init = 1;
    elf_overlay_xtensa_init();
    elf_overlay_riscv_init();
}

int elf_overlay_isa_register(const elf_overlay_isa_ops_t *ops) {
    if (!ops || g_isa_count >= MAX_ISA_OPS) return -1;
    for (int i = 0; i < g_isa_count; i++) {
        if (g_isa_ops[i]->id == ops->id) return 0;
    }
    g_isa_ops[g_isa_count++] = ops;
    return 0;
}

const elf_overlay_isa_ops_t *elf_overlay_isa_get(uint16_t isa_id) {
    elf_overlay_isa_init_builtins();
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
