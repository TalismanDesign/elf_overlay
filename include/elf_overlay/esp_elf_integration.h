#pragma once

#include "elf_overlay/overlay_loader.h"
#include "esp_elf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install the section map from an esp_elf_t after relocation.
 *
 * esp_elf_loader knows the canonical (pre-relocation) virtual address of each
 * loaded section and where it was placed in resident memory. Call this after
 * esp_elf_relocate() succeeds so elf_overlay can translate patch targets and
 * redirect sites through overlay_loader_resolve().
 *
 * @param loader   - initialized elf_overlay_loader_t
 * @param elf      - relocated esp_elf_t (sections already placed)
 * @return ELF_OVERLAY_OK on success
 */
int elf_overlay_esp_elf_set_section_map(elf_overlay_loader_t *loader, esp_elf_t *elf);

/**
 * @brief Install a symbol resolver that routes host references through
 *        esp_elf_find_symbol().
 *
 * Host refs (overlay code calling host functions like printf, malloc) need
 * symbol addresses. This wires elf_overlay's resolver to the esp_elf_loader
 * symbol table so host calls resolve correctly.
 *
 * @param loader - initialized elf_overlay_loader_t
 * @return ELF_OVERLAY_OK on success
 */
int elf_overlay_esp_elf_install_symbol_resolver(elf_overlay_loader_t *loader);

/**
 * @brief Apply overlay redirects to the resident app image.
 *
 * After the ELF is relocated and the section map is installed, call this to
 * patch redirect sites in resident memory so calls to paged functions go
 * through their stubs. Call before the app entry point is invoked.
 *
 * @param loader - initialized elf_overlay_loader_t
 * @return ELF_OVERLAY_OK on success
 */
int elf_overlay_esp_elf_apply_redirects(elf_overlay_loader_t *loader);

#ifdef __cplusplus
}
#endif
