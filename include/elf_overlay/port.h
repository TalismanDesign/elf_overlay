#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory allocation */
void *elf_overlay_alloc(size_t size);
void *elf_overlay_alloc_exec(size_t size);
void *elf_overlay_alloc_rw(size_t size);
void elf_overlay_free(void *ptr);

/* File I/O */
typedef struct elf_overlay_file elf_overlay_file_t;
elf_overlay_file_t *elf_overlay_file_open(const char *path);
int elf_overlay_file_read(elf_overlay_file_t *file, size_t offset, void *buf, size_t len);
size_t elf_overlay_file_size(elf_overlay_file_t *file);
void elf_overlay_file_close(elf_overlay_file_t *file);

/* Copy into executable memory. On targets whose exec heap maps to
 * instruction RAM accepting only aligned 32-bit accesses (e.g. ESP32-S3
 * internal IRAM), a byte-wise memcpy faults; this helper stores whole words
 * and zero-pads the final partial word. dst and len are word-aligned by the
 * loader's frame allocation. */
void elf_overlay_copy_exec(void *dst, const void *src, size_t len);

/* Logging */
void elf_overlay_log_info(const char *tag, const char *fmt, ...);
void elf_overlay_log_warn(const char *tag, const char *fmt, ...);
void elf_overlay_log_error(const char *tag, const char *fmt, ...);

/* Sync executable memory */
void elf_overlay_sync_exec(void *addr, size_t len);

#ifdef __cplusplus
}
#endif
