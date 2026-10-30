#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ELF_OVERLAY_MAGIC "ELFO"

#define ELF_OVERLAY_VERSION 1

#define ELF_OVERLAY_ALIGN 4

/* Blob header */
typedef struct elf_overlay_header {
    char magic[4];
    uint16_t version;
    uint16_t flags;
    uint16_t isa;
    uint16_t overlays;
    uint16_t stubs;
    uint16_t redirects;
    uint16_t patches;
    uint16_t stub_patches;
    uint16_t host_refs;
    uint32_t stub_block_off;
    uint32_t stub_block_size;
    uint32_t slot_enter;
    uint32_t slot_leave;
    uint32_t strtab_off;
    uint32_t strtab_size;
} __attribute__((packed)) elf_overlay_header_t;

/* Table entries */
typedef struct elf_overlay_entry {
    uint16_t id;
    uint16_t pad;
    uint32_t offset;
    uint32_t length;
} __attribute__((packed)) elf_overlay_entry_t;

typedef struct elf_overlay_stub {
    uint32_t canonical;
    uint16_t overlay_id;
    uint16_t body_offset;
    uint32_t stub_off;
} __attribute__((packed)) elf_overlay_stub_t;

typedef struct elf_overlay_redirect {
    uint32_t site;
    uint32_t stub;
} __attribute__((packed)) elf_overlay_redirect_t;

typedef struct elf_overlay_patch {
    uint16_t overlay_id;
    uint16_t blob_offset;
    uint16_t insn_offset; /* instruction offset for patching */
    uint16_t pad;
} __attribute__((packed)) elf_overlay_patch_t;

typedef struct elf_overlay_hostref {
    uint16_t overlay_id;
    uint16_t blob_offset;
    uint16_t insn_offset;
    uint16_t pad;
    uint32_t name_off;
} __attribute__((packed)) elf_overlay_hostref_t;

#ifdef __cplusplus
}
#endif
