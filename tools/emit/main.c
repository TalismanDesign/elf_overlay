#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include "elf_overlay/isa.h"
#include "blob_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define XTENSA_STUB_STRIDE 44

static int find_tool(char *out, size_t out_size, const char *name) {
    const char *path = getenv("PATH");
    if (!path) return -1;

    char *copy = strdup(path);
    if (!copy) return -1;
    char *save = NULL;
    for (char *dir = strtok_r(copy, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        int n = snprintf(out, out_size, "%s/%s", dir, name);
        if (n <= 0 || (size_t)n >= out_size) continue;
        if (access(out, X_OK) == 0) {
            free(copy);
            return 0;
        }
    }
    free(copy);
    return -1;
}

static int build_xtensa_stub_block(pack_manifest_t *m) {
    if (m->isa != ELF_OVERLAY_ISA_XTENSA || m->stub_count == 0) return 0;

    char as_path[512];
    char objcopy_path[512];
    if (find_tool(as_path, sizeof(as_path), "xtensa-esp32s3-elf-as") != 0) {
        fprintf(stderr, "xtensa-esp32s3-elf-as not found on PATH\n");
        return -1;
    }
    if (find_tool(objcopy_path, sizeof(objcopy_path), "xtensa-esp32s3-elf-objcopy") != 0) {
        fprintf(stderr, "xtensa-esp32s3-elf-objcopy not found on PATH\n");
        return -1;
    }

    char tmpl[] = "/tmp/elf_overlay_stub_XXXXXX";
    char *tmpdir = mkdtemp(tmpl);
    if (!tmpdir) return -1;

    char asm_path[512], obj_path[512], bin_path[512], cmd[2048];
    snprintf(asm_path, sizeof(asm_path), "%s/stubs.S", tmpdir);
    snprintf(obj_path, sizeof(obj_path), "%s/stubs.o", tmpdir);
    snprintf(bin_path, sizeof(bin_path), "%s/stubs.bin", tmpdir);

    FILE *f = fopen(asm_path, "w");
    if (!f) return -1;

    fprintf(f, "    .section .overlay.stubblock, \"awx\", @progbits\n");
    fprintf(f, "    .p2align 2\n");
    fprintf(f, "overlay_enter_slot:\n    .word 0\n");
    fprintf(f, "overlay_leave_slot:\n    .word 0\n");

    for (size_t i = 0; i < m->stub_count; i++) {
        uint32_t key = ((uint32_t)m->stubs[i].overlay_id << 16) | m->stubs[i].body_offset;
        fprintf(f, ".Lkey_%zu:\n    .word 0x%08x\n", i, key);
    }

    for (size_t i = 0; i < m->stub_count; i++) {
        fprintf(f, "    .p2align 2\n");
        fprintf(f, "ov_stub_%zu:\n", i);
        fprintf(f, "    entry a1, 32\n");
        fprintf(f, "    l32r a10, .Lkey_%zu\n", i);
        fprintf(f, "    l32r a8, overlay_enter_slot\n");
        fprintf(f, "    callx8 a8\n");
        fprintf(f, "    mov.n a8, a10\n");
        fprintf(f, "    mov.n a10, a2\n");
        fprintf(f, "    mov.n a11, a3\n");
        fprintf(f, "    mov.n a12, a4\n");
        fprintf(f, "    mov.n a13, a5\n");
        fprintf(f, "    mov.n a14, a6\n");
        fprintf(f, "    mov.n a15, a7\n");
        fprintf(f, "    callx8 a8\n");
        fprintf(f, "    mov.n a2, a10\n");
        fprintf(f, "    l32r a10, .Lkey_%zu\n", i);
        fprintf(f, "    l32r a8, overlay_leave_slot\n");
        fprintf(f, "    callx8 a8\n");
        fprintf(f, "    retw.n\n");
        fprintf(f, "    .space %u - (. - ov_stub_%zu)\n", XTENSA_STUB_STRIDE, i);
    }
    fprintf(f, "    .p2align 2\n");
    fclose(f);

    snprintf(cmd, sizeof(cmd), "%s -o %s %s", as_path, obj_path, asm_path);
    if (system(cmd) != 0) return -1;

    snprintf(cmd, sizeof(cmd), "%s -O binary --only-section=.overlay.stubblock %s %s",
             objcopy_path, obj_path, bin_path);
    if (system(cmd) != 0) return -1;

    FILE *bin = fopen(bin_path, "rb");
    if (!bin) return -1;
    fseek(bin, 0, SEEK_END);
    long len = ftell(bin);
    fseek(bin, 0, SEEK_SET);
    if (len <= 0) { fclose(bin); return -1; }

    uint8_t *block = malloc((size_t)len);
    if (!block) { fclose(bin); return -1; }
    if (fread(block, 1, (size_t)len, bin) != (size_t)len) {
        fclose(bin);
        free(block);
        return -1;
    }
    fclose(bin);

    free(m->stub_block);
    m->stub_block = block;
    m->stub_block_size = (size_t)len;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: elf_overlay_emit <input.relk> <output.elfo>\n");
        return 2;
    }

    const char *relk_path = argv[1];
    const char *elfo_path = argv[2];

    FILE *fp = fopen(relk_path, "rb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", relk_path);
        return 2;
    }
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = malloc(fsize);
    if (!data) { fclose(fp); return 2; }
    fread(data, 1, fsize, fp);
    fclose(fp);

    pack_manifest_t m;
    memset(&m, 0, sizeof(m));
    if (elf_overlay_manifest_deserialize(&m, data, fsize) != 0) {
        fprintf(stderr, "deserialize failed\n");
        free(data);
        return 2;
    }
    free(data);

    if (build_xtensa_stub_block(&m) != 0) {
        fprintf(stderr, "stub block generation failed\n");
        elf_overlay_manifest_free(&m);
        return 2;
    }

    /* Compute required ELFO buffer size */
    size_t strtab_off = sizeof(elf_overlay_header_t)
        + 12 * m.overlay_count
        + 12 * m.stub_count
        + 8 * m.redirect_count
        + 8 * m.patch_count
        + 8 * m.stub_patch_count
        + 12 * m.host_ref_count;
    size_t strtab_size = m.strtab_size;
    size_t stub_block_off = (strtab_off + strtab_size + 3) & ~3;
    size_t total = stub_block_off + m.stub_block_size + m.overlay_data_size;

    uint8_t *blob = malloc(total);
    if (!blob) {
        elf_overlay_manifest_free(&m);
        return 2;
    }

    size_t written = elf_overlay_pack_blob(&m, blob, total);
    if (written == 0 || written != total) {
        fprintf(stderr, "pack failed: wrote %zu of %zu\n", written, total);
        free(blob);
        elf_overlay_manifest_free(&m);
        return 1;
    }

    fp = fopen(elfo_path, "wb");
    if (!fp) {
        fprintf(stderr, "cannot open %s\n", elfo_path);
        free(blob);
        elf_overlay_manifest_free(&m);
        return 1;
    }
    fwrite(blob, 1, total, fp);
    fclose(fp);

    printf("wrote %zu bytes to %s\n", total, elfo_path);

    free(blob);
    elf_overlay_manifest_free(&m);
    return 0;
}
