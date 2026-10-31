#include "elf_overlay/esp_elf_integration.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/config.h"
#include <string.h>

static uintptr_t esp_elf_symbol_resolver(const char *name) {
    return (uintptr_t)esp_elf_find_symbol(name);
}

int elf_overlay_esp_elf_set_section_map(elf_overlay_loader_t *loader, esp_elf_t *elf) {
    if (!loader || !elf) return ELF_OVERLAY_ERR_RANGE;

    elf_overlay_section_map_t map;
    memset(&map, 0, sizeof(map));

    /* esp_elf_t.sec[] entries with size == 0 are unused. */
    for (int i = 0; i < ELF_OVERLAY_MAX_SECTIONS; i++) {
        if (elf->sec[i].size == 0) continue;
        map.entries[i].v_addr = (uint32_t)elf->sec[i].v_addr;
        map.entries[i].addr = (uint32_t)elf->sec[i].addr;
        map.entries[i].size = (uint32_t)elf->sec[i].size;
    }

    return elf_overlay_set_section_map(loader, &map);
}

int elf_overlay_esp_elf_install_symbol_resolver(elf_overlay_loader_t *loader) {
    if (!loader) return ELF_OVERLAY_ERR_RANGE;
    return elf_overlay_set_symbol_resolver(loader, esp_elf_symbol_resolver);
}

int elf_overlay_esp_elf_apply_redirects(elf_overlay_loader_t *loader) {
    if (!loader) return ELF_OVERLAY_ERR_FORMAT;
    return elf_overlay_apply_redirects(loader);
}
