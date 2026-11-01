#include "blob_pack.h"
#include <string.h>
#include <stdlib.h>

#define ELFO_STUB_ALIGN 4
#define RELK_HDR_SIZE 28
/* Six u16 table counts follow the 28-byte fixed header. */
#define RELK_COUNTS_SIZE 12

static size_t align_up(size_t v, size_t a) {
    return (v + a - 1) & ~(a - 1);
}

static void wr_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}
static void wr_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}
static uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t elf_overlay_pack_blob(const pack_manifest_t *m, uint8_t *out, size_t out_cap) {
    if (!m || !out) return 0;

    size_t strtab_off = sizeof(elf_overlay_header_t) + 12 * m->overlay_count + 12 * m->stub_count +
                        8 * m->redirect_count + 8 * m->patch_count + 8 * m->stub_patch_count +
                        12 * m->host_ref_count;

    size_t strtab_size = m->strtab_size;
    size_t stub_block_off = align_up(strtab_off + strtab_size, ELFO_STUB_ALIGN);

    size_t total = stub_block_off + m->stub_block_size + m->overlay_data_size;

    if (out_cap < total) return 0;

    elf_overlay_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, ELF_OVERLAY_MAGIC, 4);
    hdr.version = ELF_OVERLAY_VERSION;
    hdr.flags = 0;
    hdr.isa = m->isa;
    hdr.overlays = (uint16_t)m->overlay_count;
    hdr.stubs = (uint16_t)m->stub_count;
    hdr.redirects = (uint16_t)m->redirect_count;
    hdr.patches = (uint16_t)m->patch_count;
    hdr.stub_patches = (uint16_t)m->stub_patch_count;
    hdr.host_refs = (uint16_t)m->host_ref_count;
    hdr.stub_block_off = (uint32_t)stub_block_off;
    hdr.stub_block_size = (uint32_t)m->stub_block_size;
    hdr.slot_enter = m->slot_enter;
    hdr.slot_leave = m->slot_leave;
    hdr.strtab_off = (uint32_t)strtab_off;
    hdr.strtab_size = (uint32_t)strtab_size;

    size_t off = 0;
    memcpy(out + off, &hdr, sizeof(hdr));
    off += sizeof(hdr);

    for (size_t i = 0; i < m->overlay_count; i++) {
        elf_overlay_entry_t e;
        memset(&e, 0, sizeof(e));
        e.id = m->overlays[i].id;
        e.pad = 0;
        e.offset = (uint32_t)(stub_block_off + m->stub_block_size + m->overlays[i].offset);
        e.length = m->overlays[i].length;
        memcpy(out + off, &e, sizeof(e));
        off += sizeof(e);
    }

    for (size_t i = 0; i < m->stub_count; i++) {
        elf_overlay_stub_t s;
        memset(&s, 0, sizeof(s));
        s.canonical = m->stubs[i].canonical;
        s.overlay_id = m->stubs[i].overlay_id;
        s.body_offset = m->stubs[i].body_offset;
        s.stub_off = m->stubs[i].stub_off;
        memcpy(out + off, &s, sizeof(s));
        off += sizeof(s);
    }

    for (size_t i = 0; i < m->redirect_count; i++) {
        elf_overlay_redirect_t r;
        memset(&r, 0, sizeof(r));
        r.site = m->redirects[i].site;
        r.stub = m->redirects[i].stub;
        memcpy(out + off, &r, sizeof(r));
        off += sizeof(r);
    }

    for (size_t i = 0; i < m->patch_count; i++) {
        elf_overlay_patch_t p;
        memset(&p, 0, sizeof(p));
        p.overlay_id = m->patches[i].overlay_id;
        p.blob_offset = m->patches[i].blob_offset;
        p.insn_offset = m->patches[i].insn_offset;
        p.pad = m->patches[i].pad;
        memcpy(out + off, &p, sizeof(p));
        off += sizeof(p);
    }

    for (size_t i = 0; i < m->stub_patch_count; i++) {
        elf_overlay_patch_t p;
        memset(&p, 0, sizeof(p));
        p.overlay_id = m->stub_patches[i].overlay_id;
        p.blob_offset = m->stub_patches[i].blob_offset;
        p.insn_offset = m->stub_patches[i].insn_offset;
        p.pad = m->stub_patches[i].pad;
        memcpy(out + off, &p, sizeof(p));
        off += sizeof(p);
    }

    for (size_t i = 0; i < m->host_ref_count; i++) {
        elf_overlay_hostref_t h;
        memset(&h, 0, sizeof(h));
        h.overlay_id = m->host_refs[i].overlay_id;
        h.blob_offset = m->host_refs[i].blob_offset;
        h.insn_offset = m->host_refs[i].insn_offset;
        h.pad = m->host_refs[i].pad;
        h.name_off = m->host_refs[i].name_off;
        memcpy(out + off, &h, sizeof(h));
        off += sizeof(h);
    }

    if (m->strtab_size > 0 && m->strtab) {
        memcpy(out + off, m->strtab, m->strtab_size);
        off += m->strtab_size;
    }

    while (off < stub_block_off) {
        out[off++] = 0;
    }

    if (m->stub_block_size > 0 && m->stub_block) {
        memcpy(out + off, m->stub_block, m->stub_block_size);
        off += m->stub_block_size;
    }

    if (m->overlay_data_size > 0 && m->overlay_data) {
        memcpy(out + off, m->overlay_data, m->overlay_data_size);
        off += m->overlay_data_size;
    }

    return off;
}

int elf_overlay_manifest_serialize(const pack_manifest_t *m, uint8_t **out, size_t *out_len) {
    if (!m || !out || !out_len) return -1;

    size_t strtab_size = m->strtab_size ? m->strtab_size : 0;
    size_t overlay_data_size = m->overlay_data_size;

    size_t total = RELK_HDR_SIZE + 12 /* six u16 counts */
                   + m->stub_block_size + overlay_data_size + 12 * m->overlay_count +
                   12 * m->stub_count + 8 * m->redirect_count + 8 * m->patch_count +
                   8 * m->stub_patch_count + 12 * m->host_ref_count + strtab_size;

    uint8_t *buf = malloc(total);
    if (!buf) return -1;

    size_t off = 0;
    memcpy(buf + off, RELK_MAGIC, 4);
    off += 4;
    wr_u16(buf + off, RELK_VERSION);
    off += 2;
    wr_u16(buf + off, m->isa);
    off += 2;
    wr_u32(buf + off, m->slot_enter);
    off += 4;
    wr_u32(buf + off, m->slot_leave);
    off += 4;
    wr_u32(buf + off, (uint32_t)m->stub_block_size);
    off += 4;
    wr_u32(buf + off, (uint32_t)overlay_data_size);
    off += 4;
    wr_u32(buf + off, (uint32_t)strtab_size);
    off += 4;

    wr_u16(buf + off, (uint16_t)m->overlay_count);
    off += 2;
    wr_u16(buf + off, (uint16_t)m->stub_count);
    off += 2;
    wr_u16(buf + off, (uint16_t)m->redirect_count);
    off += 2;
    wr_u16(buf + off, (uint16_t)m->patch_count);
    off += 2;
    wr_u16(buf + off, (uint16_t)m->stub_patch_count);
    off += 2;
    wr_u16(buf + off, (uint16_t)m->host_ref_count);
    off += 2;

    if (m->stub_block_size > 0 && m->stub_block) {
        memcpy(buf + off, m->stub_block, m->stub_block_size);
        off += m->stub_block_size;
    }
    if (overlay_data_size > 0 && m->overlay_data) {
        memcpy(buf + off, m->overlay_data, overlay_data_size);
        off += overlay_data_size;
    }
    for (size_t i = 0; i < m->overlay_count; i++) {
        wr_u16(buf + off, m->overlays[i].id);
        off += 2;
        wr_u16(buf + off, m->overlays[i].pad);
        off += 2;
        wr_u32(buf + off, m->overlays[i].offset);
        off += 4;
        wr_u32(buf + off, m->overlays[i].length);
        off += 4;
    }
    for (size_t i = 0; i < m->stub_count; i++) {
        wr_u32(buf + off, m->stubs[i].canonical);
        off += 4;
        wr_u16(buf + off, m->stubs[i].overlay_id);
        off += 2;
        wr_u16(buf + off, m->stubs[i].body_offset);
        off += 2;
        wr_u32(buf + off, m->stubs[i].stub_off);
        off += 4;
    }
    for (size_t i = 0; i < m->redirect_count; i++) {
        wr_u32(buf + off, m->redirects[i].site);
        off += 4;
        wr_u32(buf + off, m->redirects[i].stub);
        off += 4;
    }
    for (size_t i = 0; i < m->patch_count; i++) {
        wr_u16(buf + off, m->patches[i].overlay_id);
        off += 2;
        wr_u16(buf + off, m->patches[i].blob_offset);
        off += 2;
        wr_u16(buf + off, m->patches[i].insn_offset);
        off += 2;
        wr_u16(buf + off, m->patches[i].pad);
        off += 2;
    }
    for (size_t i = 0; i < m->stub_patch_count; i++) {
        wr_u16(buf + off, m->stub_patches[i].overlay_id);
        off += 2;
        wr_u16(buf + off, m->stub_patches[i].blob_offset);
        off += 2;
        wr_u16(buf + off, m->stub_patches[i].insn_offset);
        off += 2;
        wr_u16(buf + off, m->stub_patches[i].pad);
        off += 2;
    }
    for (size_t i = 0; i < m->host_ref_count; i++) {
        wr_u16(buf + off, m->host_refs[i].overlay_id);
        off += 2;
        wr_u16(buf + off, m->host_refs[i].blob_offset);
        off += 2;
        wr_u16(buf + off, m->host_refs[i].insn_offset);
        off += 2;
        wr_u16(buf + off, m->host_refs[i].pad);
        off += 2;
        wr_u32(buf + off, m->host_refs[i].name_off);
        off += 4;
    }
    if (strtab_size > 0 && m->strtab) {
        memcpy(buf + off, m->strtab, strtab_size);
        off += strtab_size;
    }

    *out = buf;
    *out_len = total;
    return 0;
}

int elf_overlay_manifest_deserialize(pack_manifest_t *m, const uint8_t *data, size_t len) {
    if (!m || !data) return -1;
    memset(m, 0, sizeof(*m));

    /* The fixed header plus the six u16 counts must both be present before any
     * field is read, or the count reads at offsets 28..40 run past the end. */
    if (len < RELK_HDR_SIZE + RELK_COUNTS_SIZE) return -1;
    if (memcmp(data, RELK_MAGIC, 4) != 0) return -1;

    size_t off = 4;
    uint16_t version = rd_u16(data + off);
    off += 2;
    (void)version;
    m->isa = rd_u16(data + off);
    off += 2;
    m->slot_enter = rd_u32(data + off);
    off += 4;
    m->slot_leave = rd_u32(data + off);
    off += 4;
    m->stub_block_size = rd_u32(data + off);
    off += 4;
    size_t overlay_data_size = rd_u32(data + off);
    off += 4;
    size_t strtab_size = rd_u32(data + off);
    off += 4;

    m->overlay_count = rd_u16(data + off);
    off += 2;
    m->stub_count = rd_u16(data + off);
    off += 2;
    m->redirect_count = rd_u16(data + off);
    off += 2;
    m->patch_count = rd_u16(data + off);
    off += 2;
    m->stub_patch_count = rd_u16(data + off);
    off += 2;
    m->host_ref_count = rd_u16(data + off);
    off += 2;

    /* stub block */
    if (m->stub_block_size > 0) {
        if (off + m->stub_block_size > len) return -1;
        m->stub_block = malloc(m->stub_block_size);
        if (!m->stub_block) return -1;
        memcpy(m->stub_block, data + off, m->stub_block_size);
        off += m->stub_block_size;
    }

    /* overlay data */
    if (overlay_data_size > 0) {
        if (off + overlay_data_size > len) return -1;
        m->overlay_data = malloc(overlay_data_size);
        if (!m->overlay_data) return -1;
        memcpy(m->overlay_data, data + off, overlay_data_size);
        m->overlay_data_size = overlay_data_size;
        off += overlay_data_size;
    }

    /* overlay entries */
    m->overlays = calloc(m->overlay_count ? m->overlay_count : 1, sizeof(pack_overlay_t));
    if (!m->overlays) return -1;
    for (size_t i = 0; i < m->overlay_count; i++) {
        if (off + 12 > len) return -1;
        m->overlays[i].id = rd_u16(data + off);
        off += 2;
        off += 2; /* pad */
        m->overlays[i].offset = rd_u32(data + off);
        off += 4;
        m->overlays[i].length = rd_u32(data + off);
        off += 4;
    }

    /* stub entries */
    m->stubs = calloc(m->stub_count ? m->stub_count : 1, sizeof(pack_stub_t));
    if (!m->stubs) return -1;
    for (size_t i = 0; i < m->stub_count; i++) {
        if (off + 12 > len) return -1;
        m->stubs[i].canonical = rd_u32(data + off);
        off += 4;
        m->stubs[i].overlay_id = rd_u16(data + off);
        off += 2;
        m->stubs[i].body_offset = rd_u16(data + off);
        off += 2;
        m->stubs[i].stub_off = rd_u32(data + off);
        off += 4;
    }

    /* redirect entries */
    m->redirects = calloc(m->redirect_count ? m->redirect_count : 1, sizeof(pack_redirect_t));
    if (!m->redirects) return -1;
    for (size_t i = 0; i < m->redirect_count; i++) {
        if (off + 8 > len) return -1;
        m->redirects[i].site = rd_u32(data + off);
        off += 4;
        m->redirects[i].stub = rd_u32(data + off);
        off += 4;
    }

    /* patch entries */
    m->patches = calloc(m->patch_count ? m->patch_count : 1, sizeof(pack_patch_t));
    if (!m->patches) return -1;
    for (size_t i = 0; i < m->patch_count; i++) {
        if (off + 8 > len) return -1;
        m->patches[i].overlay_id = rd_u16(data + off);
        off += 2;
        m->patches[i].blob_offset = rd_u16(data + off);
        off += 2;
        m->patches[i].insn_offset = rd_u16(data + off);
        off += 2;
        m->patches[i].pad = rd_u16(data + off);
        off += 2;
    }

    /* stub_patch entries */
    m->stub_patches = calloc(m->stub_patch_count ? m->stub_patch_count : 1, sizeof(pack_patch_t));
    if (!m->stub_patches) return -1;
    for (size_t i = 0; i < m->stub_patch_count; i++) {
        if (off + 8 > len) return -1;
        m->stub_patches[i].overlay_id = rd_u16(data + off);
        off += 2;
        m->stub_patches[i].blob_offset = rd_u16(data + off);
        off += 2;
        m->stub_patches[i].insn_offset = rd_u16(data + off);
        off += 2;
        m->stub_patches[i].pad = rd_u16(data + off);
        off += 2;
    }

    /* host_ref entries */
    m->host_refs = calloc(m->host_ref_count ? m->host_ref_count : 1, sizeof(pack_hostref_t));
    if (!m->host_refs) return -1;
    for (size_t i = 0; i < m->host_ref_count; i++) {
        if (off + 12 > len) return -1;
        m->host_refs[i].overlay_id = rd_u16(data + off);
        off += 2;
        m->host_refs[i].blob_offset = rd_u16(data + off);
        off += 2;
        m->host_refs[i].insn_offset = rd_u16(data + off);
        off += 2;
        m->host_refs[i].pad = rd_u16(data + off);
        off += 2;
        m->host_refs[i].name_off = rd_u32(data + off);
        off += 4;
    }

    /* strtab */
    if (strtab_size > 0) {
        if (off + strtab_size > len) return -1;
        m->strtab = malloc(strtab_size);
        if (!m->strtab) return -1;
        memcpy(m->strtab, data + off, strtab_size);
        m->strtab_size = strtab_size;
    }

    return 0;
}

void elf_overlay_manifest_free(pack_manifest_t *m) {
    if (!m) return;
    free(m->stub_block);
    free(m->overlay_data);
    free(m->overlays);
    free(m->stubs);
    free(m->redirects);
    free(m->patches);
    free(m->stub_patches);
    free(m->host_refs);
    free(m->strtab);
    memset(m, 0, sizeof(*m));
}
