#include "elf_overlay/isa.h"
#include <string.h>

/* RISC-V AUIPC opcode (U-type): bits [6:0] */
#define RISCV_OPCODE_AUIPC 0x17

/* AUIPC: rd = (pc & ~0xFFF) + (imm << 12). imm is 20 bits signed. */
#define RISCV_AUIPC_IMM_SHIFT 12
#define RISCV_AUIPC_IMM_MASK  0xFFFFF
#define RISCV_AUIPC_RANGE     (1 << 31) /* ±2MB */

static size_t riscv_insn_len(uint8_t first) {
    (void)first;
    return 4; /* all RISC-V base instructions are 4 bytes */
}

static int riscv_find_patch_sites(const uint8_t *code, size_t code_len,
                                   uint32_t pc_base,
                                   elf_overlay_patch_site_t *sites,
                                   size_t *site_count) {
    if (!code || !sites || !site_count) return -1;

    size_t count = 0;
    size_t max_sites = *site_count;
    size_t offset = 0;

    while (offset + 4 <= code_len && count < max_sites) {
        uint32_t insn = code[offset] | (code[offset+1] << 8) |
                        (code[offset+2] << 16) | (code[offset+3] << 24);
        uint32_t opcode = insn & 0x7F;

        if (opcode == RISCV_OPCODE_AUIPC) {
            uint8_t rd = (insn >> 7) & 0x1F;
            uint32_t imm = (insn >> 12) & RISCV_AUIPC_IMM_MASK;
            /* sign-extend 20-bit imm */
            if (imm & (1 << 19)) {
                imm |= ~RISCV_AUIPC_IMM_MASK;
            }
            uint32_t pc_aligned = (pc_base + offset) & ~0xFFF;
            uint32_t literal = pc_aligned + (imm << RISCV_AUIPC_IMM_SHIFT);

            sites[count].insn_offset = offset;
            sites[count].reg = rd;
            sites[count].imm = literal;
            count++;
        }
        offset += 4;
    }

    *site_count = count;
    return 0;
}

static int riscv_encode_patch(uint8_t *insn, size_t insn_len,
                               uint32_t pc, uint32_t target_addr,
                               const elf_overlay_patch_site_t *site) {
    if (!insn || insn_len < 4 || !site) return -1;

    uint32_t pc_aligned = pc & ~0xFFF;
    int32_t delta = (int32_t)target_addr - (int32_t)pc_aligned;
    if (delta % (1 << RISCV_AUIPC_IMM_SHIFT) != 0) return -1;
    int32_t imm = delta >> RISCV_AUIPC_IMM_SHIFT;
    if (imm < -0x80000 || imm > 0x7FFFF) return -1;

    uint32_t insn_bits = ((uint32_t)(imm & RISCV_AUIPC_IMM_MASK) << 12) |
                         ((uint32_t)site->reg << 7) |
                         RISCV_OPCODE_AUIPC;

    insn[0] = insn_bits & 0xFF;
    insn[1] = (insn_bits >> 8) & 0xFF;
    insn[2] = (insn_bits >> 16) & 0xFF;
    insn[3] = (insn_bits >> 24) & 0xFF;

    return 0;
}

static bool riscv_is_patch_valid(int32_t delta) {
    if (delta % (1 << RISCV_AUIPC_IMM_SHIFT) != 0) return false;
    int32_t imm = delta >> RISCV_AUIPC_IMM_SHIFT;
    return (imm >= -0x80000 && imm <= 0x7FFFF);
}

static size_t riscv_get_insn_len(void) {
    return 4;
}

static size_t riscv_get_stub_stride(void) {
    return 32;
}

static size_t riscv_get_stub_alignment(void) {
    return 4;
}

static const elf_overlay_isa_ops_t riscv_ops = {
    .name = "riscv",
    .id = ELF_OVERLAY_ISA_RISCV,
    .find_patch_sites = riscv_find_patch_sites,
    .encode_patch = riscv_encode_patch,
    .is_patch_valid = riscv_is_patch_valid,
    .generate_stub = NULL,
    .get_insn_len = riscv_get_insn_len,
    .get_stub_stride = riscv_get_stub_stride,
    .get_stub_alignment = riscv_get_stub_alignment
};

void elf_overlay_riscv_init(void) {
    elf_overlay_isa_register(&riscv_ops);
}
