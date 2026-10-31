#include "elf_overlay/isa.h"
#include <string.h>

#define XTENSA_L32R_OP0 0x1
#define XTENSA_SHORT_OP0_MIN 0x8
#define XTENSA_SHORT_OP0_MAX 0xD

static size_t xtensa_insn_len(uint8_t first) {
    if ((first & 0xF) >= XTENSA_SHORT_OP0_MIN && (first & 0xF) <= XTENSA_SHORT_OP0_MAX) {
        return 2;
    }
    return 3;
}

static int16_t xtensa_sign16(uint16_t val) {
    if (val & 0x8000) {
        return (int16_t)(val - 0x10000);
    }
    return (int16_t)val;
}

static uint32_t xtensa_l32r_literal(uint32_t pc, uint16_t imm) {
    uint32_t aligned = (pc + 3) & ~3;
    int32_t delta = xtensa_sign16(imm);
    return aligned + (delta << 2);
}

static int xtensa_find_patch_sites(const uint8_t *code, size_t code_len,
                                   uint32_t pc_base,
                                   elf_overlay_patch_site_t *sites,
                                   size_t *site_count) {
    if (!code || !sites || !site_count) return -1;
    
    size_t count = 0;
    size_t max_sites = *site_count;
    size_t offset = 0;
    
    while (offset < code_len && count < max_sites) {
        uint8_t first = code[offset];
        if ((first & 0xF) == XTENSA_L32R_OP0 && offset + 3 <= code_len) {
            uint32_t word = code[offset] | ((uint32_t)code[offset+1] << 8) | ((uint32_t)code[offset+2] << 16);
            uint8_t reg = (word >> 4) & 0xF;
            uint16_t imm = (word >> 8) & 0xFFFF;
            uint32_t literal = xtensa_l32r_literal(pc_base + offset, imm);
            
            sites[count].insn_offset = offset;
            sites[count].reg = reg;
            sites[count].imm = literal;
            count++;
            offset += 3;
        } else {
            offset += xtensa_insn_len(first);
        }
    }
    
    *site_count = count;
    return 0;
}

static int xtensa_encode_patch(uint8_t *insn, size_t insn_len,
                               uint32_t pc, uint32_t target_addr,
                               const elf_overlay_patch_site_t *site) {
    if (!insn || insn_len < 3 || !site) return -1;
    
    uint32_t aligned_pc = (pc + 3) & ~3;
    int32_t delta = (int32_t)target_addr - (int32_t)aligned_pc;
    
    if (delta % 4 != 0) return -1;
    int32_t imm = delta >> 2;
    if (imm < -0x8000 || imm > 0x7FFF) return -1;
    
    uint8_t reg = (insn[0] >> 4) & 0xF;
    (void)site;

    insn[0] = XTENSA_L32R_OP0 | (reg << 4);
    insn[1] = (uint8_t)(imm & 0xFF);
    insn[2] = (uint8_t)((imm >> 8) & 0xFF);
    
    return 0;
}

static bool xtensa_is_patch_valid(int32_t delta) {
    if (delta % 4 != 0) return false;
    int32_t imm = delta >> 2;
    return (imm >= -0x8000 && imm <= 0x7FFF);
}

static size_t xtensa_get_insn_len(void) {
    return 3;
}

static size_t xtensa_get_stub_stride(void) {
    return ELF_OVERLAY_XTENSA_STUB_STRIDE;
}

static size_t xtensa_get_stub_alignment(void) {
    return ELF_OVERLAY_STUB_ALIGNMENT;
}

static const elf_overlay_isa_ops_t xtensa_ops = {
    .name = "xtensa",
    .id = ELF_OVERLAY_ISA_XTENSA,
    .find_patch_sites = xtensa_find_patch_sites,
    .encode_patch = xtensa_encode_patch,
    .is_patch_valid = xtensa_is_patch_valid,
    .get_insn_len = xtensa_get_insn_len,
    .get_stub_stride = xtensa_get_stub_stride,
    .get_stub_alignment = xtensa_get_stub_alignment
};

void elf_overlay_xtensa_init(void) {
    elf_overlay_isa_register(&xtensa_ops);
}
