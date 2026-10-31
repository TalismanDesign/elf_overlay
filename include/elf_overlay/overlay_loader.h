#pragma once

#include <stddef.h>
#include <stdint.h>
#include "elf_overlay/config.h"
#include "elf_overlay/errors.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct elf_overlay_loader elf_overlay_loader_t;

/* Section map for canonical to resident address translation */
typedef struct elf_overlay_section_map_entry {
    uint32_t v_addr;
    uint32_t addr;
    uint32_t size;
} elf_overlay_section_map_entry_t;

typedef struct elf_overlay_section_map {
    elf_overlay_section_map_entry_t entries[ELF_OVERLAY_MAX_SECTIONS];
} elf_overlay_section_map_t;

/* Load/unload */
int elf_overlay_load(elf_overlay_loader_t **out, const char *path);
int elf_overlay_load_from_memory(elf_overlay_loader_t **out, const void *data, size_t size);
void elf_overlay_unload(elf_overlay_loader_t *loader);

/* Enter/leave callbacks */
void *elf_overlay_enter(uint32_t key);
void elf_overlay_leave(uint32_t key);

typedef uintptr_t (*elf_overlay_symbol_resolver_t)(const char *name);

int elf_overlay_set_symbol_resolver(elf_overlay_loader_t *loader,
                                    elf_overlay_symbol_resolver_t resolver);

/* Section mapping */
int elf_overlay_set_section_map(elf_overlay_loader_t *loader, const elf_overlay_section_map_t *map);
int elf_overlay_resolve(const elf_overlay_loader_t *loader, uint32_t canonical, uint32_t *resident);

/* Apply redirects */
int elf_overlay_apply_redirects(elf_overlay_loader_t *loader);

/* Utilities */
void *elf_overlay_get_stub_block(elf_overlay_loader_t *loader);
int elf_overlay_find_stub(elf_overlay_loader_t *loader, uint32_t canonical, uint32_t *stub_off);
size_t elf_overlay_get_frame_size(elf_overlay_loader_t *loader);
size_t elf_overlay_get_frame_count(elf_overlay_loader_t *loader);

/* Active loader management */
void elf_overlay_set_active(elf_overlay_loader_t *loader);
elf_overlay_loader_t *elf_overlay_get_active(void);

#ifdef __cplusplus
}
#endif
