#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ISA identifiers */
#define ELF_OVERLAY_ISA_UNKNOWN 0
#define ELF_OVERLAY_ISA_XTENSA  1
#define ELF_OVERLAY_ISA_RISCV   2
#define ELF_OVERLAY_ISA_ARM     3
#define ELF_OVERLAY_ISA_AARCH64 4

/* Patch site info for ISA-specific instruction patching */
typedef struct elf_overlay_patch_site {
    uint16_t insn_offset;  /* offset of instruction in code */
    uint8_t reg;           /* destination register */
    int32_t imm;           /* immediate value */
} elf_overlay_patch_site_t;

/* Stub descriptor */
typedef struct elf_overlay_stub_desc {
    uint32_t key;
    uint32_t enter_func;
    uint32_t leave_func;
} elf_overlay_stub_desc_t;

struct elf_overlay_isa_ops;

/* ISA operations vtable */
typedef struct elf_overlay_isa_ops {
    const char *name;
    uint16_t id;
    
    /* Find patch sites (e.g., l32r on Xtensa) in code */
    int (*find_patch_sites)(const uint8_t *code, size_t code_len, 
                            uint32_t pc_base,
                            elf_overlay_patch_site_t *sites, 
                            size_t *site_count);
    
    /* Encode a patch site to load from a specific address */
    int (*encode_patch)(uint8_t *insn, size_t insn_len,
                        uint32_t pc, uint32_t target_addr,
                        const elf_overlay_patch_site_t *site);
    
    /* Validate if immediate/range is valid for this ISA */
    bool (*is_patch_valid)(int32_t delta);
    
    /* Get instruction length in bytes for this ISA */
    size_t (*get_insn_len)(void);
    
    /* Generate stub code */
    int (*generate_stub)(const elf_overlay_stub_desc_t *desc,
                         uint8_t *out, size_t out_size,
                         size_t *out_len);
    
    /* Get stub stride/alignment */
    size_t (*get_stub_stride)(void);
    size_t (*get_stub_alignment)(void);
} elf_overlay_isa_ops_t;

const elf_overlay_isa_ops_t *elf_overlay_isa_get(uint16_t isa_id);
const elf_overlay_isa_ops_t *elf_overlay_isa_get_default(void);
int elf_overlay_isa_register(const elf_overlay_isa_ops_t *ops);

#ifdef __cplusplus
}
#endif
