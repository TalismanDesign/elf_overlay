#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISA identifiers */
#define ELF_OVERLAY_ISA_UNKNOWN 0
#define ELF_OVERLAY_ISA_XTENSA 1
#define ELF_OVERLAY_ISA_RISCV 2
#define ELF_OVERLAY_ISA_ARM 3
#define ELF_OVERLAY_ISA_AARCH64 4

/* Per-stub byte stride in the generated stub block. */
#define ELF_OVERLAY_XTENSA_STUB_STRIDE 44
#define ELF_OVERLAY_RISCV_STUB_STRIDE 32
#define ELF_OVERLAY_STUB_ALIGNMENT 4

/* Fixed slot block ahead of the stubs: enter + leave function pointers. */
#define ELF_OVERLAY_STUB_SLOTS_SIZE 8

/* Patch site info for ISA-specific instruction patching */
typedef struct elf_overlay_patch_site {
    uint16_t insn_offset; /* offset of instruction in code */
    uint8_t reg;          /* destination register */
    int32_t imm;          /* immediate value */
} elf_overlay_patch_site_t;

struct elf_overlay_isa_ops;

/* ISA operations vtable */
typedef struct elf_overlay_isa_ops {
    const char *name;
    uint16_t id;

    /* Find patch sites (e.g., l32r on Xtensa) in code */
    int (*find_patch_sites)(const uint8_t *code, size_t code_len, uint32_t pc_base,
                            elf_overlay_patch_site_t *sites, size_t *site_count);

    /* Encode a patch site to load from a specific address */
    int (*encode_patch)(uint8_t *insn, size_t insn_len, uint32_t pc, uint32_t target_addr,
                        const elf_overlay_patch_site_t *site);

    /* Validate if immediate/range is valid for this ISA */
    bool (*is_patch_valid)(int32_t delta);

    /* Get instruction length in bytes for this ISA */
    size_t (*get_insn_len)(void);

    /* Get stub stride/alignment. Stub bodies themselves are assembled by the
     * host emit tool, not generated at runtime, so the runtime vtable only
     * reports their layout. */
    size_t (*get_stub_stride)(void);
    size_t (*get_stub_alignment)(void);
} elf_overlay_isa_ops_t;

const elf_overlay_isa_ops_t *elf_overlay_isa_get(uint16_t isa_id);
const elf_overlay_isa_ops_t *elf_overlay_isa_get_default(void);
int elf_overlay_isa_register(const elf_overlay_isa_ops_t *ops);

#ifdef __cplusplus
}
#endif
