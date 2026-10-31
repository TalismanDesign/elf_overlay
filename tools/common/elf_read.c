#include "elf_read.h"
#include <string.h>
#include <stdlib.h>

#define ELF_MAGIC "\177ELF"
#define ELF_CLASS32 1
#define ELF_DATA_LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_XTENSA 94
#define EM_RISCV 243
#define EM_ARM 40

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

#define STN_UNDEF 0

/* ELF32 raw field layouts */
typedef struct {
    uint8_t e_ident[16];
    uint8_t e_type[2];
    uint8_t e_machine[2];
    uint8_t e_version[4];
    uint8_t e_entry[4];
    uint8_t e_phoff[4];
    uint8_t e_shoff[4];
    uint8_t e_flags[4];
    uint8_t e_ehsize[2];
    uint8_t e_phentsize[2];
    uint8_t e_phnum[2];
    uint8_t e_shentsize[2];
    uint8_t e_shnum[2];
    uint8_t e_shstrndx[2];
} elf32_ehdr_raw_t;

typedef struct {
    uint8_t sh_name[4];
    uint8_t sh_type[4];
    uint8_t sh_flags[4];
    uint8_t sh_addr[4];
    uint8_t sh_offset[4];
    uint8_t sh_size[4];
    uint8_t sh_link[4];
    uint8_t sh_info[4];
    uint8_t sh_addralign[4];
    uint8_t sh_entsize[4];
} elf32_shdr_raw_t;

typedef struct {
    uint8_t st_name[4];
    uint8_t st_value[4];
    uint8_t st_size[4];
    uint8_t st_info;
    uint8_t st_other;
    uint8_t st_shndx[2];
} elf32_sym_raw_t;

typedef struct {
    uint8_t r_offset[4];
    uint8_t r_info[4];
    int8_t r_addend[4];
} elf32_rela_raw_t;

static uint32_t rd_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t rd_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint16_t rd_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint16_t rd_u16_be(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static int32_t rd_i32_le(const void *p) {
    return (int32_t)rd_u32_le((const uint8_t *)p);
}

static int32_t rd_i32_be(const void *p) {
    return (int32_t)rd_u32_be((const uint8_t *)p);
}

typedef uint32_t (*rd_u32_fn)(const uint8_t *);
typedef uint16_t (*rd_u16_fn)(const uint8_t *);
typedef int32_t (*rd_i32_fn)(const void *);

int elf_reader_open(elf_reader_t *reader, const void *data, size_t size) {
    if (!reader || !data || size < sizeof(elf32_ehdr_raw_t)) return -1;

    memset(reader, 0, sizeof(*reader));
    reader->data = (const uint8_t *)data;
    reader->size = size;

    const uint8_t *p = (const uint8_t *)data;
    if (memcmp(p, ELF_MAGIC, 4) != 0) return -1;

    reader->is_64bit = (p[4] != ELF_CLASS32);
    reader->is_little_endian = (p[5] == ELF_DATA_LSB);

    if (reader->is_64bit) return -1; /* all current ESP targets are ELF32 */

    const elf32_ehdr_raw_t *ehdr = (const elf32_ehdr_raw_t *)p;
    rd_u32_fn u32 = reader->is_little_endian ? rd_u32_le : rd_u32_be;
    rd_u16_fn u16 = reader->is_little_endian ? rd_u16_le : rd_u16_be;

    reader->machine = u16(ehdr->e_machine);
    reader->entry = u32(ehdr->e_entry);

    return 0;
}

void elf_reader_close(elf_reader_t *reader) {
    if (!reader) return;
    for (size_t i = 0; i < reader->section_count; i++) {
        free(reader->sections[i].name);
        free(reader->sections[i].data);
    }
    free(reader->sections);
    for (size_t i = 0; i < reader->symbol_count; i++) {
        free(reader->symbols[i].name);
    }
    free(reader->symbols);
    for (size_t i = 0; i < reader->reloc_count; i++) {
        free(reader->relocs[i].symbol);
    }
    free(reader->relocs);
    memset(reader, 0, sizeof(*reader));
}

static int load_sections(elf_reader_t *reader) {
    const uint8_t *p = reader->data;
    const elf32_ehdr_raw_t *ehdr = (const elf32_ehdr_raw_t *)p;
    rd_u32_fn u32 = reader->is_little_endian ? rd_u32_le : rd_u32_be;
    rd_u16_fn u16 = reader->is_little_endian ? rd_u16_le : rd_u16_be;

    uint32_t shoff = u32(ehdr->e_shoff);
    uint16_t shnum = u16(ehdr->e_shnum);
    uint16_t shentsize = u16(ehdr->e_shentsize);
    uint16_t shstrndx = u16(ehdr->e_shstrndx);

    if (shoff == 0 || shoff + (uint32_t)shnum * shentsize > reader->size) return -1;

    const elf32_shdr_raw_t *shdrs = (const elf32_shdr_raw_t *)(p + shoff);

    /* Section header string table */
    const elf32_shdr_raw_t *shstr_hdr = &shdrs[shstrndx];
    const char *shstrtab = (const char *)(p + u32(shstr_hdr->sh_offset));

    reader->sections = calloc(shnum, sizeof(elf_section_t));
    if (!reader->sections) return -1;
    reader->section_count = shnum;

    for (uint16_t i = 0; i < shnum; i++) {
        const elf32_shdr_raw_t *s = &shdrs[i];
        elf_section_t *sec = &reader->sections[i];

        uint32_t name_off = u32(s->sh_name);
        sec->name = strdup(shstrtab + name_off);
        sec->type = u32(s->sh_type);
        sec->flags = u32(s->sh_flags);
        sec->addr = u32(s->sh_addr);
        sec->offset = u32(s->sh_offset);
        sec->size = u32(s->sh_size);
        sec->link = u32(s->sh_link);
        sec->info = u32(s->sh_info);
        sec->addralign = u32(s->sh_addralign);
        sec->entsize = u32(s->sh_entsize);

        if (sec->type != SHT_NOBITS && sec->size > 0 && sec->offset + sec->size <= reader->size) {
            sec->data = malloc(sec->size);
            if (sec->data) {
                memcpy(sec->data, p + sec->offset, sec->size);
            }
        }
    }
    return 0;
}

static int load_symbols(elf_reader_t *reader) {
    elf_section_t *symtab = NULL;
    elf_section_t *strtab = NULL;

    for (size_t i = 0; i < reader->section_count; i++) {
        if (reader->sections[i].type == SHT_SYMTAB) {
            symtab = &reader->sections[i];
            strtab = &reader->sections[symtab->link];
            break;
        }
    }
    if (!symtab || !strtab || !strtab->data) return 0;

    size_t sym_count = symtab->size / sizeof(elf32_sym_raw_t);
    const elf32_sym_raw_t *syms = (const elf32_sym_raw_t *)symtab->data;
    const char *strs = (const char *)strtab->data;
    rd_u32_fn u32 = reader->is_little_endian ? rd_u32_le : rd_u32_be;
    rd_u16_fn u16 = reader->is_little_endian ? rd_u16_le : rd_u16_be;

    reader->symbols = calloc(sym_count, sizeof(elf_symbol_t));
    if (!reader->symbols) return -1;
    reader->symbol_count = sym_count;

    for (size_t i = 0; i < sym_count; i++) {
        elf_symbol_t *s = &reader->symbols[i];
        s->name = strdup(strs + u32(syms[i].st_name));
        s->value = u32(syms[i].st_value);
        s->size = u32(syms[i].st_size);
        s->info = syms[i].st_info;
        s->other = syms[i].st_other;
        s->shndx = u16(syms[i].st_shndx);
    }
    return 0;
}

static int load_relocs(elf_reader_t *reader) {
    size_t total = 0;
    for (size_t i = 0; i < reader->section_count; i++) {
        if (reader->sections[i].type == SHT_RELA) {
            total += reader->sections[i].size / sizeof(elf32_rela_raw_t);
        }
    }
    if (total == 0) return 0;

    reader->relocs = calloc(total, sizeof(elf_reloc_t));
    if (!reader->relocs) return -1;

    size_t idx = 0;
    rd_u32_fn u32 = reader->is_little_endian ? rd_u32_le : rd_u32_be;
    rd_i32_fn i32 = reader->is_little_endian ? rd_i32_le : rd_i32_be;

    for (size_t i = 0; i < reader->section_count; i++) {
        elf_section_t *sec = &reader->sections[i];
        if (sec->type != SHT_RELA || !sec->data) continue;
        elf_section_t *symtab =
            sec->link < reader->section_count ? &reader->sections[sec->link] : NULL;
        elf_section_t *strtab = (symtab && symtab->link < reader->section_count)
                                    ? &reader->sections[symtab->link]
                                    : NULL;
        const elf32_sym_raw_t *syms =
            (symtab && symtab->data) ? (const elf32_sym_raw_t *)symtab->data : NULL;
        const char *strs = (strtab && strtab->data) ? (const char *)strtab->data : NULL;
        size_t sym_count = symtab && symtab->entsize ? symtab->size / symtab->entsize : 0;

        size_t n = sec->size / sizeof(elf32_rela_raw_t);
        const elf32_rela_raw_t *relas = (const elf32_rela_raw_t *)sec->data;
        for (size_t j = 0; j < n; j++) {
            uint32_t info = u32(relas[j].r_info);
            uint32_t sym_index = info >> 8;
            reader->relocs[idx].r_offset = u32(relas[j].r_offset);
            reader->relocs[idx].r_info = info;
            reader->relocs[idx].r_addend = i32(relas[j].r_addend);
            if (syms && strs && sym_index < sym_count) {
                uint32_t name_off = u32(syms[sym_index].st_name);
                if (name_off < strtab->size) {
                    reader->relocs[idx].symbol = strdup(strs + name_off);
                }
            }
            idx++;
        }
    }
    reader->reloc_count = total;
    return 0;
}

int elf_reader_sections(elf_reader_t *reader) {
    return load_sections(reader);
}

int elf_reader_symbols(elf_reader_t *reader) {
    return load_symbols(reader);
}

int elf_reader_relocs(elf_reader_t *reader) {
    return load_relocs(reader);
}

elf_section_t *elf_section_by_name(elf_reader_t *reader, const char *name) {
    if (!reader || !name) return NULL;
    for (size_t i = 0; i < reader->section_count; i++) {
        if (reader->sections[i].name && strcmp(reader->sections[i].name, name) == 0) {
            return &reader->sections[i];
        }
    }
    return NULL;
}

elf_section_t *elf_section_by_addr(elf_reader_t *reader, uint32_t addr) {
    if (!reader) return NULL;
    for (size_t i = 0; i < reader->section_count; i++) {
        elf_section_t *s = &reader->sections[i];
        if (s->size > 0 && addr >= s->addr && addr - s->addr < s->size) {
            return s;
        }
    }
    return NULL;
}
