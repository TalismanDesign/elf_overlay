#pragma once

#include "elf_read.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Xtensa dynamic relocation type numbers, per the Espressif Xtensa ISA
 * reference, chapter 5 (ELF Object Files). */
#define R_XTENSA_RTLD 2
#define R_XTENSA_GLOB_DAT 3
#define R_XTENSA_JMP_SLOT 4
#define R_XTENSA_RELATIVE 5

#define XTENSA_RELATIVE_STR "R_XTENSA_RELATIVE"
#define XTENSA_JMP_SLOT_STR "R_XTENSA_JMP_SLOT"
#define XTENSA_RTLD_STR "R_XTENSA_RTLD"

typedef struct {
    uint32_t offset;
    const char *type;
    const char *symbol;
} relocation_t;

int parse_relocations(elf_reader_t *reader, relocation_t **out, size_t *count);

#ifdef __cplusplus
}
#endif
