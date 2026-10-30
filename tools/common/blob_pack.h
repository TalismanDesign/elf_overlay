#pragma once

#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t id;
    uint16_t pad;
    uint32_t offset;
    uint32_t length;
} pack_overlay_t;

typedef struct {
    uint32_t canonical;
    uint16_t overlay_id;
    uint16_t body_offset;
    uint32_t stub_off;
} pack_stub_t;

typedef struct {
    uint32_t site;
    uint32_t stub;
} pack_redirect_t;

typedef struct {
    uint16_t overlay_id;
    uint16_t blob_offset;
    uint16_t insn_offset;
    uint16_t pad;
} pack_patch_t;

typedef struct {
    uint16_t overlay_id;
    uint16_t blob_offset;
    uint16_t insn_offset;
    uint16_t pad;
    uint32_t name_off;
} pack_hostref_t;

/* Intermediate representation produced by relink, consumed by emit */
typedef struct {
    const char *app;
    uint16_t isa;
    uint32_t slot_enter;
    uint32_t slot_leave;
    uint8_t *stub_block;
    size_t stub_block_size;

    pack_overlay_t *overlays;
    size_t overlay_count;

    pack_stub_t *stubs;
    size_t stub_count;

    pack_redirect_t *redirects;
    size_t redirect_count;

    pack_patch_t *patches;
    size_t patch_count;

    pack_patch_t *stub_patches;
    size_t stub_patch_count;

    pack_hostref_t *host_refs;
    size_t host_ref_count;

    /* Raw overlay body data, concatenated (overlays[].offset indexes into this) */
    uint8_t *overlay_data;
    size_t overlay_data_size;

    /* String table for host refs */
    char *strtab;
    size_t strtab_size;
} pack_manifest_t;

/* Pack a pack_manifest_t into an ELFO blob buffer. Returns bytes written. */
size_t elf_overlay_pack_blob(const pack_manifest_t *m, uint8_t *out, size_t out_cap);

/* Intermediate binary format (relink -> emit):
 *   magic "RELK" (4) + version uint16 + isa uint16
 *   slot_enter u32 + slot_leave u32 + stub_block_size u32
 *   counts: overlay, stub, redirect, patch, stub_patch, host_ref (u16 each)
 *   stub_block bytes (stub_block_size)
 *   overlay_data bytes (sum of overlay lengths)
 *   pack_overlay_t array (overlay_count)
 *   pack_stub_t array (stub_count)
 *   pack_redirect_t array (redirect_count)
 *   pack_patch_t array (patch_count)
 *   pack_patch_t array (stub_patch_count)
 *   pack_hostref_t array (host_ref_count)
 *   strtab bytes (strtab_size)
 */
#define RELK_MAGIC "RELK"
#define RELK_VERSION 1
#define RELK_HDR_SIZE 28

int elf_overlay_manifest_serialize(const pack_manifest_t *m, uint8_t **out, size_t *out_len);
int elf_overlay_manifest_deserialize(pack_manifest_t *m, const uint8_t *data, size_t len);
void elf_overlay_manifest_free(pack_manifest_t *m);

#ifdef __cplusplus
}
#endif
