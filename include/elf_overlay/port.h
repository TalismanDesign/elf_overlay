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

/* Logging */
void elf_overlay_log_info(const char *tag, const char *fmt, ...);
void elf_overlay_log_warn(const char *tag, const char *fmt, ...);
void elf_overlay_log_error(const char *tag, const char *fmt, ...);

/* Memory sync */
void elf_overlay_sync(void);

#ifdef __cplusplus
}
#endif
