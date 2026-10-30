# elf_overlay

Universal, ISA-abstracted ELF overlay runtime library for embedded systems.

## Overview

`elf_overlay` is a portable, ISA-agnostic runtime for paged overlay execution on embedded targets. It works alongside ELF loaders (e.g. Espressif's `esp_elf_loader`) to page overlay bodies in and out of exec RAM, applying ISA-specific relocations (l32r on Xtensa, auipc on RISC-V) at load time.

It is target-agnostic: no ESP-IDF, no PocketMageOS, and no RTOS assumptions. A small platform port layer (`port.h`) lets it run on any system with a C compiler and a file system.

## Architecture

```text
include/elf_overlay/   Public C API headers
src/isa/               ISA-specific patching (xtensa, riscv, arm...)
src/core/              Blob parsing, loader, frame pool, section map
src/port/              Platform abstraction (posix, esp_idf)
tools/                 Host build tools (relink, emit)
tests/                 Unit and integration tests
```

### Blob format (ELFO)

Magic `ELFO`, version 1, ISA-tagged header. Layout:

```text
+----------------------+
| elf_overlay_header_t |  magic, version, ISA tag, table counts
+----------------------+
| overlay entries      |  id, offset, length (per overlay)
+----------------------+
| stub entries         |  canonical vaddr, overlay_id, body_offset, stub_off
+----------------------+
| redirect entries     |  site, stub_off
+----------------------+
| patch entries        |  overlay_id, blob_offset, insn_offset
+----------------------+
| stub_patch entries   |  overlay_id, blob_offset, insn_offset
+----------------------+
| host_ref entries     |  overlay_id, blob_offset, insn_offset, name_off
+----------------------+
| string table         |  null-terminated host symbol names
+----------------------+
| stub block           |  enter/leave slots + stub code
+----------------------+
| overlay bodies       |  packed code + literal pools
+----------------------+
```

### ISA abstraction

ISA-specific code lives behind `elf_overlay_isa_ops_t` (`include/elf_overlay/isa.h`).
Registered ISAs:

- `xtensa` - l32r PC-relative literal patching (verified against Xtensa ISA doc)
- `riscv` - auipc PC-relative literal patching

Add a new ISA by implementing `elf_overlay_isa_ops_t` and calling
`elf_overlay_isa_register()` from an init function.

## Host tools

### `elf_overlay_relink <elf32_file> <output.relk>`

Reads an ELF32 executable, scans `.text` for ISA-specific patch sites
(e.g. l32r on Xtensa), builds a literal pool, classifies relocations, and
writes a `.relk` intermediate file.

### `elf_overlay_emit <input.relk> <output.elfo>`

Packs a `.relk` into an ELFO blob ready for the runtime loader.

## Runtime API

```c
/* Load/unload */
int elf_overlay_load(elf_overlay_loader_t **out, const char *path);
void elf_overlay_unload(elf_overlay_loader_t *loader);

/* Enter/leave: called by position-independent stubs */
void *elf_overlay_enter(uint32_t key);   /* key = (overlay_id << 16) | body_offset */
void elf_overlay_leave(uint32_t key);    /* decrements refcount; 0 = evictable */

/* Section map: canonical vaddr -> resident address */
int elf_overlay_set_section_map(elf_overlay_loader_t *loader,
                                 const elf_overlay_section_map_t *map);
int elf_overlay_resolve(elf_overlay_loader_t *loader,
                        uint32_t canonical, uint32_t *resident);

/* Symbol resolver for host references */
typedef uintptr_t (*elf_overlay_symbol_resolver_t)(const char *name);
int elf_overlay_set_symbol_resolver(elf_overlay_loader_t *loader,
                                     elf_overlay_symbol_resolver_t);

/* Redirects: patch resident app code to jump through stubs */
int elf_overlay_apply_redirects(elf_overlay_loader_t *loader);

/* Utilities */
void *elf_overlay_get_stub_block(elf_overlay_loader_t *loader);
int elf_overlay_find_stub(elf_overlay_loader_t *loader,
                          uint32_t canonical, uint32_t *stub_off);
void elf_overlay_set_active(elf_overlay_loader_t *loader);
elf_overlay_loader_t *elf_overlay_get_active(void);
```

### Key design

- **Frame pool**: fixed number of exec-RAM frames; overlays page in on demand.
- **Refcounting**: `enter()` increments, `leave()` decrements. Frames with
  `refcount > 0` are never evicted.
- **Lazy patching**: literal pool words and l32r/auipc immediates are patched
  at page-in time, once the resident addresses are known.
- **Minimal allocations**: fixed-size tables, no heap churn during page-in.

## Hooking into esp_elf_loader

`esp_elf_loader` loads and relocates the resident app ELF. `elf_overlay`
manages the overlay bodies on top. The integration layer
(`include/elf_overlay/esp_elf_integration.h`, `src/core/esp_elf_integration.c`)
provides three call sites:

1. **After `esp_elf_relocate()`** - call
   `elf_overlay_esp_elf_set_section_map()` to copy `esp_elf_t.sec[]`
   (canonical v_addr -> resident addr) into the overlay section map.
2. **Before app entry** - call
   `elf_overlay_esp_elf_apply_redirects()` to patch redirect sites in resident
   memory so paged functions are reached through their stubs.
3. **At init** - call
   `elf_overlay_esp_elf_install_symbol_resolver()` so host refs resolve via
   `esp_elf_find_symbol()`.

Example flow:

```c
esp_elf_t elf;
esp_elf_init(&elf);
esp_elf_relocate(&elf, buf, size);

elf_overlay_loader_t *ov;
elf_overlay_load(&ov, "app.elfo");
elf_overlay_esp_elf_set_section_map(ov, &elf);
elf_overlay_esp_elf_install_symbol_resolver(ov);
elf_overlay_esp_elf_apply_redirects(ov);

/* App can now call into overlays via stubs */
```

### ESP-IDF port

`src/port/esp_idf.c` implements `port.h` using ESP-IDF APIs
(`heap_caps_malloc` with `MALLOC_CAP_EXEC`, `ESP_LOG*`, stdio VFS). Enable with
`-DELF_OVERLAY_ESP_IDF=ON`.

## Building

```bash
# Host build (tests + tools)
mkdir build && cd build
cmake ..
make
ctest

# Device build (ESP-IDF)
cmake .. -DELF_OVERLAY_ESP_IDF=ON
make
```

## Testing

- `ctest` runs 5 tests: ISA registration, blob validation, l32r encoding,
  manifest round-trip, host blob load.
- `tests/integration/c_fixtures/` contains a real Xtensa C fixture compiled
  with `xtensa-esp32s3-elf-gcc`, processed end-to-end through
  `relink` -> `emit` -> `elf_overlay_load` -> `enter`/`leave`.

## License

See LICENSE file.
