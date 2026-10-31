#include "elf_overlay/isa.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

void elf_overlay_xtensa_init(void);

int main(void) {
    elf_overlay_xtensa_init();
    const elf_overlay_isa_ops_t *isa = elf_overlay_isa_get(ELF_OVERLAY_ISA_XTENSA);
    assert(isa != NULL);

    /* encode_patch: l32r loads from aligned_pc + (imm << 2)
     * We want l32r at pc to load from target_addr.
     * aligned_pc = (pc + 3) & ~3
     * imm = (target_addr - aligned_pc) >> 2 */

    /* Case 1: target 4 bytes after aligned pc */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = { .reg = 10 };
        int r = isa->encode_patch(insn, 3, 0x100, 0x104, &site);
        assert(r == 0);
        /* byte0 = 0x1 | (reg << 4), byte1 = imm & 0xFF, byte2 = (imm >> 8) & 0xFF */
        /* imm = (0x104 - ((0x100+3)&~3)) >> 2 = (0x104 - 0x100) >> 2 = 1 */
        assert(insn[0] == (0x1 | (10 << 4)));
        assert(insn[1] == 1);
        assert(insn[2] == 0);
    }

    /* Case 2: negative imm, matches encode_l32r(0x208, 8, 0x1C8) == 81f0ff */
    {
        uint8_t insn[3] = {0x81, 0, 0};
        elf_overlay_patch_site_t site = { .reg = 8 };
        int r = isa->encode_patch(insn, 3, 0x208, 0x1C8, &site);
        assert(r == 0);
        /* delta = 0x1C8 - 0x208 = -0x40, imm = -0x10 = 0xFFF0 */
        assert(insn[0] == 0x81);      /* 0x1 | (8<<4) = 0x81 */
        assert(insn[1] == 0xF0);      /* imm low */
        assert(insn[2] == 0xFF);      /* imm high */
    }

    /* Case 3: matches encode_l32r(0x1EB, 8, 0x1B8) == 81f3ff */
    {
        uint8_t insn[3] = {0x81, 0, 0};
        elf_overlay_patch_site_t site = { .reg = 8 };
        int r = isa->encode_patch(insn, 3, 0x1EB, 0x1B8, &site);
        assert(r == 0);
        /* aligned_pc = 0x1EE, delta = 0x1B8 - 0x1EE = -0x36, imm = -0x0D = 0xFFF3 */
        assert(insn[0] == 0x81);
        assert(insn[1] == 0xF3);
        assert(insn[2] == 0xFF);
    }

    /* Case 3: out of range */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = { .reg = 10 };
        /* delta too large for 16-bit imm */
        int r = isa->encode_patch(insn, 3, 0x100, 0x100000, &site);
        assert(r != 0);
    }

    /* Case 4: unaligned delta */
    {
        uint8_t insn[3] = {0xA1, 0, 0};
        elf_overlay_patch_site_t site = { .reg = 10 };
        int r = isa->encode_patch(insn, 3, 0x100, 0x103, &site);
        assert(r != 0);
    }

    /* is_patch_valid */
    assert(isa->is_patch_valid(0) == true);
    assert(isa->is_patch_valid(4) == true);
    assert(isa->is_patch_valid(-4) == true);
    assert(isa->is_patch_valid(2) == false); /* not divisible by 4 */
    assert(isa->is_patch_valid(0x20000) == false); /* imm too large (0x8000 << 2 = 0x20000, but boundary) */
    /* imm range: -0x8000 to 0x7FFF, so delta range: -0x20000 to 0x1FFFC */
    assert(isa->is_patch_valid(0x1FFFC) == true);
    assert(isa->is_patch_valid(-0x20000) == true);
    assert(isa->is_patch_valid(0x20000) == false);
    assert(isa->is_patch_valid(-0x20004) == false);

    /* find_patch_sites: simple code with one l32r at offset 0 */
    {
        uint8_t code[] = {
            0x11, 0x00, 0x00,   /* l32r a1, imm=0 (literal = aligned_pc + 0) */
            0x30, 0x00,         /* mov.n a0, a0 (short opcode 0x3) */
        };
        elf_overlay_patch_site_t sites[8];
        size_t n = 8;
        int r = isa->find_patch_sites(code, sizeof(code), 0x1000, sites, &n);
        assert(r == 0);
        assert(n == 1);
        assert(sites[0].insn_offset == 0);
        assert(sites[0].reg == 1);  /* (0x11 >> 4) & 0xF = 1 */
    }

    /* find_patch_sites: code with no l32r */
    {
        uint8_t code[] = { 0x30, 0x00, 0x31, 0x00 };
        elf_overlay_patch_site_t sites[8];
        size_t n = 8;
        int r = isa->find_patch_sites(code, sizeof(code), 0x1000, sites, &n);
        assert(r == 0);
        assert(n == 0);
    }

    printf("l32r tests passed\n");
    return 0;
}
