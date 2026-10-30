/* Small C fixture for elf_overlay testing.
 * Compiled with xtensa-esp32s3-elf-gcc to ELF32 Xtensa.
 */

#include <stdint.h>

/* Global data in .data section */
int32_t g_counter = 0;

/* Constant in .rodata section */
static const int32_t k_magic = 0xDEADBEEF;

/* Function that will be paged as an overlay body */
int32_t overlay_add(int32_t a, int32_t b) {
    g_counter++;
    return a + b + k_magic;
}

/* Second function in the same overlay */
int32_t overlay_sub(int32_t a, int32_t b) {
    g_counter--;
    return a - b;
}
