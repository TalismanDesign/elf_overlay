#include "elf_overlay/port.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#if defined(ELF_OVERLAY_ESP_IDF)
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_memory_utils.h"

#if CONFIG_IDF_TARGET_ESP32S3 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
#include "esp_cache.h"
#endif

struct elf_overlay_file {
  FILE* fp;
  size_t size;
};

void* elf_overlay_alloc(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}

void* elf_overlay_alloc_exec(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_EXEC);
}

void* elf_overlay_alloc_rw(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void elf_overlay_free(void* ptr) {
  heap_caps_free(ptr);
}

elf_overlay_file_t* elf_overlay_file_open(const char* path) {
  FILE* fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);

  elf_overlay_file_t* file = malloc(sizeof(elf_overlay_file_t));
  if (!file) {
    fclose(fp);
    return NULL;
  }
  file->fp = fp;
  file->size = (size_t)sz;
  return file;
}

int elf_overlay_file_read(elf_overlay_file_t* file, size_t offset, void* buf, size_t len) {
  if (!file)
    return -1;
  if (len == 0)
    return 0;
  if (!buf)
    return -1;
  if (fseek(file->fp, (long)offset, SEEK_SET) != 0)
    return -1;
  size_t r = fread(buf, 1, len, file->fp);
  if (r != len)
    return -1;
  return 0;
}

size_t elf_overlay_file_size(elf_overlay_file_t* file) {
  if (!file)
    return 0;
  return file->size;
}

void elf_overlay_file_close(elf_overlay_file_t* file) {
  if (!file)
    return;
  fclose(file->fp);
  free(file);
}

#define LOG_BUF_SIZE 256

void elf_overlay_copy_exec(void* dst, const void* src, size_t len) {
  volatile uint32_t* words = (volatile uint32_t*)dst;
  const uint8_t* in = (const uint8_t*)src;
  size_t i = 0;
  for (; len - i >= sizeof(uint32_t); i += sizeof(uint32_t)) {
    uint32_t value;
    memcpy(&value, in + i, sizeof(value));
    words[i / sizeof(uint32_t)] = value;
  }
  if (i < len) {
    uint32_t value = 0;
    memcpy(&value, in + i, len - i);
    words[i / sizeof(uint32_t)] = value;
  }
}

void elf_overlay_log_info(const char* tag, const char* fmt, ...) {
  char buf[LOG_BUF_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ESP_LOGI(tag, "%s", buf);
}

void elf_overlay_log_warn(const char* tag, const char* fmt, ...) {
  char buf[LOG_BUF_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ESP_LOGW(tag, "%s", buf);
}

void elf_overlay_log_error(const char* tag, const char* fmt, ...) {
  char buf[LOG_BUF_SIZE];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  ESP_LOGE(tag, "%s", buf);
}

void elf_overlay_sync_exec(void* addr, size_t len) {
  if (!addr || len == 0)
    return;
  __sync_synchronize();
#if CONFIG_IDF_TARGET_ESP32S3 && ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0)
  if (esp_ptr_external_ram(addr)) {
    esp_cache_msync(addr, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    esp_cache_msync(addr, len,
                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_INST |
                        ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  }
#endif
}

#else /* !ELF_OVERLAY_ESP_IDF: POSIX host port */

#include <stdio.h>
#include <sys/stat.h>

struct elf_overlay_file {
  FILE* fp;
  size_t size;
};

void* elf_overlay_alloc(size_t size) {
  return malloc(size);
}

void* elf_overlay_alloc_exec(size_t size) {
  void* ptr = malloc(size);
  return ptr;
}

void* elf_overlay_alloc_rw(size_t size) {
  return malloc(size);
}

void elf_overlay_free(void* ptr) {
  free(ptr);
}

elf_overlay_file_t* elf_overlay_file_open(const char* path) {
  FILE* fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  struct stat st;
  if (fstat(fileno(fp), &st) != 0) {
    fclose(fp);
    return NULL;
  }

  elf_overlay_file_t* file = malloc(sizeof(elf_overlay_file_t));
  if (!file) {
    fclose(fp);
    return NULL;
  }
  file->fp = fp;
  file->size = st.st_size;
  return file;
}

int elf_overlay_file_read(elf_overlay_file_t* file, size_t offset, void* buf, size_t len) {
  if (!file)
    return -1;
  if (len == 0)
    return 0;
  if (!buf)
    return -1;
  if (fseek(file->fp, offset, SEEK_SET) != 0)
    return -1;
  size_t r = fread(buf, 1, len, file->fp);
  if (r != len)
    return -1;
  return 0;
}

size_t elf_overlay_file_size(elf_overlay_file_t* file) {
  if (!file)
    return 0;
  return file->size;
}

void elf_overlay_file_close(elf_overlay_file_t* file) {
  if (!file)
    return;
  fclose(file->fp);
  free(file);
}

void elf_overlay_copy_exec(void* dst, const void* src, size_t len) {
  memcpy(dst, src, len);
}

void elf_overlay_log_info(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("[I][%s] ", tag);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void elf_overlay_log_warn(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("[W][%s] ", tag);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void elf_overlay_log_error(const char* tag, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("[E][%s] ", tag);
  vprintf(fmt, args);
  printf("\n");
  va_end(args);
}

void elf_overlay_sync_exec(void* addr, size_t len) {
  (void)addr;
  (void)len;
  __sync_synchronize();
}

#endif /* ELF_OVERLAY_ESP_IDF */
