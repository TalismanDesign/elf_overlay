#include "elf_overlay/isa.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void elf_overlay_xtensa_init(void);

int main(void) {
    elf_overlay_xtensa_init();
    const elf_overlay_isa_ops_t *isa = elf_overlay_isa_get(ELF_OVERLAY_ISA_XTENSA);
    assert(isa != NULL);

    /* L32R reads only backward: vaddr = ((pc + 3) & ~3) + ((0xFFFC0000 |
     * (imm16 << 2))). The 16-bit field is interpreted as a fixed negative
     * range, so the reachable byte delta is -0x40000 (imm16 = 0) to -4
     * (imm16 = 0xFFFF). encode_patch stores delta/4 into that field; the model
     * below mirrors xtensa_l32r_literal so every case is verified round-trip. */
#define ALIGNED_PC(pc) (((pc) + 3u) & ~3u)
    /* Decode the literal address the hardware would compute from the three
     * encoded bytes, to confirm encode_patch and the decoder agree. */
#define DECODE_LIT(pc, b1, b2)                                                                      \
    (ALIGNED_PC(pc) + ((0xFFFC0000u | ((uint32_t)(((b2) << 8) | (b1)) << 2))))

    /* Case 1: smallest backward reference, one word (-4 bytes). */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        uint32_t pc = 0x100, tgt = 0xFC;
        int r = isa->encode_patch(insn, 3, pc, tgt, &site);
        assert(r == 0);
        assert(insn[0] == (0x1 | (10 << 4)));
        /* imm16 = (-4)/4 = -1 = 0xFFFF */
        assert(insn[1] == 0xFF);
        assert(insn[2] == 0xFF);
        assert(DECODE_LIT(pc, insn[1], insn[2]) == tgt);
    }

    /* Case 2: matches encode_l32r(0x208, 8, 0x1C8) == 81f0ff */
    {
        uint8_t insn[3] = {0x81, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 8};
        uint32_t pc = 0x208, tgt = 0x1C8;
        int r = isa->encode_patch(insn, 3, pc, tgt, &site);
        assert(r == 0);
        /* delta = 0x1C8 - 0x208 = -0x40, imm = -0x10 = 0xFFF0 */
        assert(insn[0] == 0x81); /* 0x1 | (8<<4) = 0x81 */
        assert(insn[1] == 0xF0); /* imm low */
        assert(insn[2] == 0xFF); /* imm high */
        assert(DECODE_LIT(pc, insn[1], insn[2]) == tgt);
    }

    /* Case 3: matches encode_l32r(0x1EB, 8, 0x1B8) == 81f3ff */
    {
        uint8_t insn[3] = {0x81, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 8};
        uint32_t pc = 0x1EB, tgt = 0x1B8;
        int r = isa->encode_patch(insn, 3, pc, tgt, &site);
        assert(r == 0);
        /* aligned_pc = 0x1EC, delta = 0x1B8 - 0x1EC = -0x34, imm = -0x0D = 0xFFF3 */
        assert(insn[0] == 0x81);
        assert(insn[1] == 0xF3);
        assert(insn[2] == 0xFF);
        assert(DECODE_LIT(pc, insn[1], insn[2]) == tgt);
    }

    /* Case 4: maximum backward reach, -0x40000 bytes (imm16 = 0). */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        uint32_t pc = 0x40000, tgt = 0x0;
        int r = isa->encode_patch(insn, 3, pc, tgt, &site);
        assert(r == 0);
        assert(insn[1] == 0x00);
        assert(insn[2] == 0x00);
        assert(DECODE_LIT(pc, insn[1], insn[2]) == tgt);
    }

    /* Case 5: forward references are impossible on L32R and must be rejected. */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        int r = isa->encode_patch(insn, 3, 0x100, 0x104, &site);
        assert(r != 0);
    }

    /* Case 6: a zero delta (literal at the aligned pc) is out of range. */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        int r = isa->encode_patch(insn, 3, 0x100, 0x100, &site);
        assert(r != 0);
    }

    /* Case 7: one word past the maximum backward reach is out of range. */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        int r = isa->encode_patch(insn, 3, 0x40004, 0x0, &site);
        assert(r != 0);
    }

    /* Case 8: unaligned delta is rejected. */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = {.reg = 10};
        int r = isa->encode_patch(insn, 3, 0x100, 0x103, &site);
        assert(r != 0);
    }

    /* is_patch_valid: backward-only window of -0x40000..-4, word-aligned. */
    assert(isa->is_patch_valid(-4) == true);
    assert(isa->is_patch_valid(-8) == true);
    assert(isa->is_patch_valid(-0x40000) == true);  /* maximum reach */
    assert(isa->is_patch_valid(0) == false);         /* forward/zero not reachable */
    assert(isa->is_patch_valid(4) == false);         /* forward not reachable */
    assert(isa->is_patch_valid(2) == false);         /* not word-aligned */
    assert(isa->is_patch_valid(-0x40004) == false);  /* one word too far back */

#undef ALIGNED_PC
#undef DECODE_LIT

    /* find_patch_sites: simple code with one l32r at offset 0 */
    {
        uint8_t code[] = {
            0x11, 0x00, 0x00, /* l32r a1, imm=0 (literal = aligned_pc + 0) */
            0x30, 0x00,       /* mov.n a0, a0 (short opcode 0x3) */
        };
        elf_overlay_patch_site_t sites[8];
        size_t n = 8;
        int r = isa->find_patch_sites(code, sizeof(code), 0x1000, sites, &n);
        assert(r == 0);
        assert(n == 1);
        assert(sites[0].insn_offset == 0);
        assert(sites[0].reg == 1); /* (0x11 >> 4) & 0xF = 1 */
    }

    /* find_patch_sites: code with no l32r */
    {
        uint8_t code[] = {0x30, 0x00, 0x31, 0x00};
        elf_overlay_patch_site_t sites[8];
        size_t n = 8;
        int r = isa->find_patch_sites(code, sizeof(code), 0x1000, sites, &n);
        assert(r == 0);
        assert(n == 0);
    }

    printf("l32r tests passed\n");
    return 0;
}
