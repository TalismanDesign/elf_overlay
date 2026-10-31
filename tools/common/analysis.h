#pragma once

#include "elf_read.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int index;
    char *name;
    uint32_t address;
    uint32_t offset;
    uint32_t size;
} analysis_section_t;

typedef struct {
    char *name;
    uint32_t address;
    uint32_t size;
    char *section;
} analysis_function_t;

typedef struct {
    char *func;
    uint32_t address;
    char *via;       /* "direct" or "indirect" */
    uint32_t target; /* address for direct calls */
    char *symbol;    /* symbol name for indirect/host calls */
} analysis_call_t;

typedef struct {
    analysis_function_t *functions;
    size_t function_count;
    analysis_call_t *calls;
    size_t call_count;
} analysis_t;

int analysis_parse(elf_reader_t *reader, analysis_t *out);
void analysis_free(analysis_t *a);

typedef struct {
    char *caller;
    char *callee;
} analysis_edge_t;

int analysis_edges(const analysis_t *a, analysis_edge_t **out, size_t *count);

#ifdef __cplusplus
}
#endif
