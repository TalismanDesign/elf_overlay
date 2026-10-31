#include "elf_overlay/overlay_loader.h"
#include "elf_overlay/overlay_format.h"
#include "elf_overlay/blob.h"
#include "elf_overlay/isa.h"
#include "elf_overlay/config.h"
#include "elf_overlay/port.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "elf_overlay";

struct elf_overlay_frame {
    uint8_t *base;
    int overlay_id;
    uint32_t body_offset;
    uint32_t body_len; /* overlay body length in bytes; bounds-check key */
    uint32_t last_use;
    int occupied;
    int refcount; /* enter/leave reference count; 0 = evictable */
};

struct elf_overlay_loader {
    elf_overlay_file_t *file;
    elf_overlay_header_t hdr;
    void *stub_block;
    size_t stub_block_size;

    elf_overlay_section_map_t section_map;
    int section_map_set;

    elf_overlay_entry_t *overlays;
    uint16_t overlay_count;
    elf_overlay_stub_t *stubs;
    uint16_t stub_count;
    elf_overlay_redirect_t *redirects;
    uint16_t redirect_count;
    elf_overlay_patch_t *patches;
    uint16_t patch_count;
    elf_overlay_patch_t *stub_patches;
    uint16_t stub_patch_count;
    elf_overlay_hostref_t *host_refs;
    uint16_t host_ref_count;
    char *strtab;
    elf_overlay_symbol_resolver_t symbol_resolver;

    struct elf_overlay_frame frames[ELF_OVERLAY_MAX_FRAMES];
    size_t frame_count;
    size_t frame_size;
    uint32_t clock;
};

static struct elf_overlay_loader *g_active_loader = NULL;

static int alloc_tables(struct elf_overlay_loader *l) {
    l->overlays =
        l->overlay_count ? elf_overlay_alloc(l->overlay_count * sizeof(*l->overlays)) : NULL;
    l->stubs = l->stub_count ? elf_overlay_alloc(l->stub_count * sizeof(*l->stubs)) : NULL;
    l->redirects =
        l->redirect_count ? elf_overlay_alloc(l->redirect_count * sizeof(*l->redirects)) : NULL;
    l->patches = l->patch_count ? elf_overlay_alloc(l->patch_count * sizeof(*l->patches)) : NULL;
    l->stub_patches = l->stub_patch_count
                          ? elf_overlay_alloc(l->stub_patch_count * sizeof(*l->stub_patches))
                          : NULL;
    l->host_refs =
        l->host_ref_count ? elf_overlay_alloc(l->host_ref_count * sizeof(*l->host_refs)) : NULL;

    if ((l->overlay_count && !l->overlays) || (l->stub_count && !l->stubs) ||
        (l->redirect_count && !l->redirects) || (l->patch_count && !l->patches) ||
        (l->stub_patch_count && !l->stub_patches) || (l->host_ref_count && !l->host_refs)) {
        return ELF_OVERLAY_ERR_MEM;
    }
    return ELF_OVERLAY_OK;
}

static size_t read_at(elf_overlay_loader_t *loader, size_t off, void *buf, size_t len) {
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    return elf_overlay_file_read(l->file, off, buf, len);
}

void elf_overlay_set_active(elf_overlay_loader_t *loader) {
    g_active_loader = (struct elf_overlay_loader *)loader;
}

elf_overlay_loader_t *elf_overlay_get_active(void) {
    return (elf_overlay_loader_t *)g_active_loader;
}

static int pick_victim(struct elf_overlay_loader *l) {
    int victim = -1;
    for (size_t i = 0; i < l->frame_count; i++) {
        if (!l->frames[i].occupied) return (int)i;
        if (l->frames[i].refcount > 0) continue;
        if (victim < 0 || l->frames[i].last_use < l->frames[victim].last_use) {
            victim = (int)i;
        }
    }
    return victim;
}

static int find_resident(struct elf_overlay_loader *l, int overlay_id) {
    for (size_t i = 0; i < l->frame_count; i++) {
        if (l->frames[i].occupied && l->frames[i].overlay_id == overlay_id) {
            return (int)i;
        }
    }
    return -1;
}

static int find_overlay(struct elf_overlay_loader *l, uint16_t id) {
    for (uint16_t i = 0; i < l->overlay_count; i++) {
        if (l->overlays[i].id == id) return (int)i;
    }
    return -1;
}

void *elf_overlay_enter(uint32_t key) {
    struct elf_overlay_loader *l = g_active_loader;
    if (!l) return NULL;

    uint16_t overlay_id = (key >> 16) & 0xFFFF;

    int slot = find_resident(l, overlay_id);
    if (slot < 0) {
        int oidx = find_overlay(l, overlay_id);
        if (oidx < 0) return NULL;
        elf_overlay_entry_t *ov = &l->overlays[oidx];

        size_t read_off = ov->offset;
        size_t ov_len = ov->length;
        /* The frame must hold the body plus the up-to-3-byte zero pad that the
         * word-store exec copy writes for a non-word-multiple length. */
        if (ov_len + ELF_OVERLAY_ALIGN > l->frame_size) {
            elf_overlay_log_error(TAG, "overlay %u too large: %u > frame %u", overlay_id,
                                  (unsigned)ov_len, (unsigned)l->frame_size);
            return NULL;
        }

        uint8_t *staging = elf_overlay_alloc_rw(ov_len);
        if (!staging) return NULL;
        if (read_at(l, read_off, staging, ov_len) != 0) {
            elf_overlay_free(staging);
            return NULL;
        }

        slot = pick_victim(l);
        if (slot < 0) {
            elf_overlay_free(staging);
            return NULL;
        }
        uint32_t body_base = (uint32_t)(uintptr_t)l->frames[slot].base;

        const elf_overlay_isa_ops_t *isa = elf_overlay_isa_get(l->hdr.isa);
        size_t insn_len = isa ? isa->get_insn_len() : 4;

        for (uint16_t i = 0; i < l->patch_count; i++) {
            if (l->patches[i].overlay_id == overlay_id) {
                uint16_t boff = l->patches[i].blob_offset;
                if ((size_t)boff + 4 > ov_len) continue;
                uint32_t *w = (uint32_t *)(staging + boff);
                uint32_t resident = 0;
                if (elf_overlay_resolve(l, *w, &resident) == ELF_OVERLAY_OK) {
                    *w = resident;
                    uint16_t insn_off = l->patches[i].insn_offset;
                    if (insn_off != 0 && insn_off + insn_len <= ov_len) {
                        if (isa && isa->encode_patch) {
                            uint32_t pc_runtime = body_base + insn_off;
                            uint32_t literal_addr = body_base + boff;
                            isa->encode_patch(staging + insn_off, insn_len, pc_runtime,
                                              literal_addr, &(elf_overlay_patch_site_t){.reg = 0});
                        }
                    }
                }
            }
        }
        for (uint16_t i = 0; i < l->stub_patch_count; i++) {
            if (l->stub_patches[i].overlay_id == overlay_id) {
                uint16_t boff = l->stub_patches[i].blob_offset;
                if ((size_t)boff + 4 > ov_len || !l->stub_block) continue;
                uint32_t *w = (uint32_t *)(staging + boff);
                *w = (uint32_t)(uintptr_t)l->stub_block + *w;
                uint16_t insn_off = l->stub_patches[i].insn_offset;
                if (insn_off != 0 && insn_off + insn_len <= ov_len) {
                    if (isa && isa->encode_patch) {
                        uint32_t pc_runtime = body_base + insn_off;
                        uint32_t literal_addr = body_base + boff;
                        isa->encode_patch(staging + insn_off, insn_len, pc_runtime, literal_addr,
                                          &(elf_overlay_patch_site_t){.reg = 0});
                    }
                }
            }
        }
        int host_ref_failed = 0;
        for (uint16_t i = 0; i < l->host_ref_count; i++) {
            if (l->host_refs[i].overlay_id == overlay_id) {
                uint16_t boff = l->host_refs[i].blob_offset;
                if ((size_t)boff + 4 > ov_len) continue;
                uint32_t *w = (uint32_t *)(staging + boff);
                const char *name = l->strtab + l->host_refs[i].name_off;
                uintptr_t sym = 0;
                if (l->symbol_resolver) {
                    sym = l->symbol_resolver(name);
                }
                /* An unresolved host symbol must abort the page-in: writing 0
                 * here would leave a null call/load target that faults the
                 * moment the overlay code runs, with no diagnostic. Fail loud
                 * instead. */
                if (sym == 0) {
                    elf_overlay_log_error(TAG, "overlay %u: unresolved host symbol '%s'",
                                          overlay_id, name);
                    host_ref_failed = 1;
                    break;
                }
                *w = (uint32_t)sym;
                uint16_t insn_off = l->host_refs[i].insn_offset;
                if (insn_off != 0 && insn_off + insn_len <= ov_len) {
                    if (isa && isa->encode_patch) {
                        uint32_t pc_runtime = body_base + insn_off;
                        uint32_t literal_addr = body_base + boff;
                        isa->encode_patch(staging + insn_off, insn_len, pc_runtime, literal_addr,
                                          &(elf_overlay_patch_site_t){.reg = 0});
                    }
                }
            }
        }
        if (host_ref_failed) {
            elf_overlay_free(staging);
            return NULL;
        }

        elf_overlay_copy_exec(l->frames[slot].base, staging, ov_len);
        elf_overlay_sync_exec(l->frames[slot].base, ov_len);
        elf_overlay_free(staging);

        l->frames[slot].overlay_id = overlay_id;
        l->frames[slot].occupied = 1;
        l->frames[slot].body_offset = 0;
        l->frames[slot].body_len = ov_len;
        l->frames[slot].refcount = 1;
        l->frames[slot].last_use = ++l->clock;
    } else {
        l->frames[slot].refcount++;
    }

    l->frames[slot].last_use = ++l->clock;
    uint32_t body_off = key & 0xFFFF;
    if (body_off >= l->frames[slot].body_len) {
        elf_overlay_log_error(TAG, "bad body off ov=%u off=0x%x len=0x%x", overlay_id, body_off,
                              (unsigned)l->frames[slot].body_len);
        return NULL;
    }
    return (void *)(l->frames[slot].base + body_off);
}

void elf_overlay_leave(uint32_t key) {
    struct elf_overlay_loader *l = g_active_loader;
    if (!l) return;

    uint16_t overlay_id = (key >> 16) & 0xFFFF;

    for (size_t i = 0; i < l->frame_count; i++) {
        if (l->frames[i].occupied && l->frames[i].overlay_id == overlay_id) {
            if (l->frames[i].refcount > 0) {
                l->frames[i].refcount--;
            }
            break;
        }
    }
    /* Intentional no-op if the overlay isn't resident: there's nothing to
     * decrement.*/
}

int elf_overlay_load(elf_overlay_loader_t **out, const char *path) {
    if (!out || !path) return ELF_OVERLAY_ERR_FORMAT;
    *out = NULL;

    elf_overlay_log_info(TAG, "load %s", path);

    struct elf_overlay_loader *l = elf_overlay_alloc(sizeof(*l));
    if (!l) return ELF_OVERLAY_ERR_MEM;
    memset(l, 0, sizeof(*l));

    l->file = elf_overlay_file_open(path);
    if (!l->file) {
        elf_overlay_free(l);
        return ELF_OVERLAY_ERR_IO;
    }

    if (read_at(l, 0, &l->hdr, sizeof(l->hdr)) != 0) {
        elf_overlay_file_close(l->file);
        elf_overlay_free(l);
        return ELF_OVERLAY_ERR_IO;
    }

    if (memcmp(l->hdr.magic, ELF_OVERLAY_MAGIC, 4) != 0 || l->hdr.version != ELF_OVERLAY_VERSION) {
        elf_overlay_file_close(l->file);
        elf_overlay_free(l);
        return ELF_OVERLAY_ERR_FORMAT;
    }

    l->overlay_count = l->hdr.overlays;
    l->stub_count = l->hdr.stubs;
    l->redirect_count = l->hdr.redirects;
    l->patch_count = l->hdr.patches;
    l->stub_patch_count = l->hdr.stub_patches;
    l->host_ref_count = l->hdr.host_refs;

    if (alloc_tables(l) != ELF_OVERLAY_OK) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_MEM;
    }

    size_t off = sizeof(l->hdr);
    if (elf_overlay_file_read(l->file, off, l->overlays, l->overlay_count * sizeof(*l->overlays)) !=
        0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->overlay_count * sizeof(*l->overlays);
    if (elf_overlay_file_read(l->file, off, l->stubs, l->stub_count * sizeof(*l->stubs)) != 0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->stub_count * sizeof(*l->stubs);
    if (elf_overlay_file_read(l->file, off, l->redirects,
                              l->redirect_count * sizeof(*l->redirects)) != 0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->redirect_count * sizeof(*l->redirects);
    if (elf_overlay_file_read(l->file, off, l->patches, l->patch_count * sizeof(*l->patches)) !=
        0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->patch_count * sizeof(*l->patches);
    if (elf_overlay_file_read(l->file, off, l->stub_patches,
                              l->stub_patch_count * sizeof(*l->stub_patches)) != 0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->stub_patch_count * sizeof(*l->stub_patches);
    if (elf_overlay_file_read(l->file, off, l->host_refs,
                              l->host_ref_count * sizeof(*l->host_refs)) != 0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_IO;
    }
    off += l->host_ref_count * sizeof(*l->host_refs);

    if (l->hdr.strtab_size > 0 && l->hdr.strtab_off == off) {
        l->strtab = elf_overlay_alloc(l->hdr.strtab_size);
        if (!l->strtab) {
            elf_overlay_unload(l);
            return ELF_OVERLAY_ERR_MEM;
        }
        if (elf_overlay_file_read(l->file, off, l->strtab, l->hdr.strtab_size) != 0) {
            elf_overlay_unload(l);
            return ELF_OVERLAY_ERR_IO;
        }
        off += l->hdr.strtab_size;
    }

    l->stub_block_size = l->hdr.stub_block_size;
    if (l->stub_block_size > 0) {
        l->stub_block = elf_overlay_alloc_exec(l->stub_block_size);
        if (!l->stub_block) {
            elf_overlay_unload(l);
            return ELF_OVERLAY_ERR_MEM;
        }
        uint8_t *stub_staging = elf_overlay_alloc_rw(l->stub_block_size);
        if (!stub_staging) {
            elf_overlay_unload(l);
            return ELF_OVERLAY_ERR_MEM;
        }
        if (elf_overlay_file_read(l->file, l->hdr.stub_block_off, stub_staging,
                                  l->stub_block_size) != 0) {
            elf_overlay_free(stub_staging);
            elf_overlay_unload(l);
            return ELF_OVERLAY_ERR_IO;
        }
        /* Wire enter/leave slots in the staging copy before it reaches IRAM. */
        if (l->hdr.slot_enter + 4 <= l->stub_block_size) {
            uint32_t v = (uint32_t)(uintptr_t)&elf_overlay_enter;
            memcpy(stub_staging + l->hdr.slot_enter, &v, 4);
        }
        if (l->hdr.slot_leave + 4 <= l->stub_block_size) {
            uint32_t v = (uint32_t)(uintptr_t)&elf_overlay_leave;
            memcpy(stub_staging + l->hdr.slot_leave, &v, 4);
        }
        elf_overlay_copy_exec(l->stub_block, stub_staging, l->stub_block_size);
        elf_overlay_sync_exec(l->stub_block, l->stub_block_size);
        elf_overlay_free(stub_staging);
    }

    l->frame_count = ELF_OVERLAY_MAX_FRAMES;
    l->frame_size = ELF_OVERLAY_DEFAULT_FRAME_SIZE;
    for (size_t i = 0; i < l->frame_count; i++) {
        l->frames[i].base = elf_overlay_alloc_exec(l->frame_size);
        if (!l->frames[i].base) {
            l->frame_count = i;
            break;
        }
        l->frames[i].occupied = 0;
        l->frames[i].overlay_id = -1;
        l->frames[i].body_offset = 0;
        l->frames[i].body_len = 0;
        l->frames[i].last_use = 0;
    }
    if (l->frame_count == 0 && l->overlay_count > 0) {
        elf_overlay_unload(l);
        return ELF_OVERLAY_ERR_MEM;
    }
    l->clock = 0;

    *out = (elf_overlay_loader_t *)l;
    return ELF_OVERLAY_OK;
}

int elf_overlay_load_from_memory(elf_overlay_loader_t **out, const void *data, size_t size) {
    (void)data;
    (void)size;
    if (!out) return ELF_OVERLAY_ERR_FORMAT;
    *out = NULL;
    return ELF_OVERLAY_ERR_UNSUPPORTED;
}

void elf_overlay_unload(elf_overlay_loader_t *loader) {
    if (!loader) return;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    if (l->file) elf_overlay_file_close(l->file);
    elf_overlay_free(l->overlays);
    elf_overlay_free(l->stubs);
    elf_overlay_free(l->redirects);
    elf_overlay_free(l->patches);
    elf_overlay_free(l->stub_patches);
    elf_overlay_free(l->host_refs);
    elf_overlay_free(l->strtab);
    if (l->stub_block) elf_overlay_free(l->stub_block);
    for (size_t i = 0; i < l->frame_count; i++) {
        if (l->frames[i].base) elf_overlay_free(l->frames[i].base);
    }
    elf_overlay_free(l);
}

int elf_overlay_set_section_map(elf_overlay_loader_t *loader,
                                const elf_overlay_section_map_t *map) {
    if (!loader || !map) return ELF_OVERLAY_ERR_RANGE;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    l->section_map = *map;
    l->section_map_set = 1;
    return ELF_OVERLAY_OK;
}

int elf_overlay_set_symbol_resolver(elf_overlay_loader_t *loader,
                                    elf_overlay_symbol_resolver_t resolver) {
    if (!loader) return ELF_OVERLAY_ERR_RANGE;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    l->symbol_resolver = resolver;
    return ELF_OVERLAY_OK;
}

int elf_overlay_resolve(const elf_overlay_loader_t *loader, uint32_t canonical,
                        uint32_t *resident) {
    if (!loader || !resident) return ELF_OVERLAY_ERR_RANGE;
    const struct elf_overlay_loader *l = (const struct elf_overlay_loader *)loader;
    if (!l->section_map_set) return ELF_OVERLAY_ERR_RANGE;
    for (int i = 0; i < ELF_OVERLAY_MAX_SECTIONS; i++) {
        const elf_overlay_section_map_entry_t *e = &l->section_map.entries[i];
        if (e->size == 0) continue;
        if (canonical >= e->v_addr && canonical - e->v_addr < e->size) {
            *resident = e->addr + (canonical - e->v_addr);
            return ELF_OVERLAY_OK;
        }
    }
    return ELF_OVERLAY_ERR_RANGE;
}

int elf_overlay_apply_redirects(elf_overlay_loader_t *loader) {
    if (!loader) return ELF_OVERLAY_ERR_FORMAT;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    if (!l->stub_block) return ELF_OVERLAY_ERR_FORMAT;
    if (!l->section_map_set) return ELF_OVERLAY_ERR_FORMAT;

    for (uint16_t i = 0; i < l->redirect_count; i++) {
        uint32_t site = l->redirects[i].site;
        uint32_t val = (uint32_t)(uintptr_t)l->stub_block + l->redirects[i].stub;
        uint32_t dst = 0;
        if (elf_overlay_resolve(loader, site, &dst) != ELF_OVERLAY_OK) continue;
        *(uint32_t *)(uintptr_t)dst = val;
        elf_overlay_sync_exec((void *)(uintptr_t)dst, 4);
    }
    return ELF_OVERLAY_OK;
}

void *elf_overlay_get_stub_block(elf_overlay_loader_t *loader) {
    if (!loader) return NULL;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    return l->stub_block;
}

int elf_overlay_find_stub(elf_overlay_loader_t *loader, uint32_t canonical, uint32_t *stub_off) {
    if (!loader || !stub_off) return ELF_OVERLAY_ERR_RANGE;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    for (uint16_t i = 0; i < l->stub_count; i++) {
        if (l->stubs[i].canonical == canonical) {
            *stub_off = l->stubs[i].stub_off;
            return ELF_OVERLAY_OK;
        }
    }
    return ELF_OVERLAY_ERR_RANGE;
}

size_t elf_overlay_get_frame_size(elf_overlay_loader_t *loader) {
    if (!loader) return 0;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    return l->frame_size;
}

size_t elf_overlay_get_frame_count(elf_overlay_loader_t *loader) {
    if (!loader) return 0;
    struct elf_overlay_loader *l = (struct elf_overlay_loader *)loader;
    return l->frame_count;
}
