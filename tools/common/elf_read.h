#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct elf_section {
    char *name;
    uint32_t type;
    uint32_t flags;
    uint32_t addr;
    uint32_t offset;
    uint32_t size;
    uint32_t link;
    uint32_t info;
    uint32_t addralign;
    uint32_t entsize;
    uint8_t *data;
} elf_section_t;

typedef struct elf_symbol {
    char *name;
    uint32_t value;
    uint32_t size;
    uint8_t info;
    uint8_t other;
    uint16_t shndx;
} elf_symbol_t;

typedef struct elf_reloc {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t r_addend;
    char *symbol;
} elf_reloc_t;

typedef struct elf_reader {
    const uint8_t *data;
    size_t size;
    bool is_64bit;
    bool is_little_endian;
    uint16_t machine;
    uint32_t entry;
    elf_section_t *sections;
    size_t section_count;
    elf_symbol_t *symbols;
    size_t symbol_count;
    elf_reloc_t *relocs;
    size_t reloc_count;
} elf_reader_t;

int elf_reader_open(elf_reader_t *reader, const void *data, size_t size);
void elf_reader_close(elf_reader_t *reader);
elf_section_t *elf_section_by_name(elf_reader_t *reader, const char *name);
elf_section_t *elf_section_by_addr(elf_reader_t *reader, uint32_t addr);
int elf_reader_sections(elf_reader_t *reader);
int elf_reader_symbols(elf_reader_t *reader);
int elf_reader_relocs(elf_reader_t *reader);

#ifdef __cplusplus
}
#endif
