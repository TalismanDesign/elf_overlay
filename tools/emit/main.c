#include "elf_overlay/overlay_format.h"
#include "elf_overlay/errors.h"
#include "blob_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
