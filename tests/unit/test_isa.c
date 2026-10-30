#include "elf_overlay/isa.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void elf_overlay_xtensa_init(void);

int main() {
    elf_overlay_xtensa_init();
    const elf_overlay_isa_ops_t *ops = elf_overlay_isa_get(ELF_OVERLAY_ISA_XTENSA);
    assert(ops != NULL);
    assert(strcmp(ops->name, "xtensa") == 0);
    printf("ISA test passed\n");
    return 0;
}
