# elf_overlay

## Description

`elf_overlay` is an ISA-abstracted ELF overlay runtime for embedded systems. It sits next to an ELF loader (such as Espressif's `elf_loader`) and pages overlay bodies in and out of executable RAM, patching ISA-specific relocations at load time: `l32r` on Xtensa, `auipc` on RISC-V.

There are no ESP-IDF, OS, or RTOS assumptions baked in. Everything target-specific goes through a small port layer (`port.h`); the ESP-IDF port is just one implementation of it.

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
| overlay bodies       |  verbatim .text images
+----------------------+
```

### ISA abstraction

ISA-specific code lives behind `elf_overlay_isa_ops_t`
(`include/elf_overlay/isa.h`). Two backends are registered:

- `xtensa` - `l32r` PC-relative literal patching (checked against the Xtensa ISA
  reference)
- `riscv` - `auipc` PC-relative literal patching

To add one, implement `elf_overlay_isa_ops_t` and register it with
`elf_overlay_isa_register()`.

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

A fixed pool of executable-RAM frames holds overlays, paged in on demand.
`enter()` bumps a frame's refcount, `leave()` drops it, and a frame stays put
while its refcount is above zero. Literals get patched in place inside the
`.text` image at page-in, once resident addresses are known; the PC-relative
loads (`l32r` / `auipc`) are never touched. All tables are fixed-size, so
page-in does no heap allocation.

## Hooking into elf_loader

`elf_loader` loads and relocates the resident application ELF; `elf_overlay`
handles the overlay bodies on top. The integration layer
(`include/elf_overlay/esp_elf_integration.h`,
`src/core/esp_elf_integration.c`) hooks in at three points:

- after `esp_elf_relocate()` - `elf_overlay_esp_elf_set_section_map()` copies
  `esp_elf_t.sec[]` (canonical vaddr -> resident addr) into the section map
- before the entry point runs - `elf_overlay_esp_elf_apply_redirects()` patches
  redirect sites in resident memory so paged functions go through their stubs
- at init - `elf_overlay_esp_elf_install_symbol_resolver()` wires host-ref
  resolution to `esp_elf_find_symbol()`

Putting it together:

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

The ESP-IDF branch of `src/port/port.c` (behind `ELF_OVERLAY_ESP_IDF`)
implements `port.h` with `heap_caps_malloc`/`MALLOC_CAP_EXEC`, `ESP_LOG*`, the
stdio VFS, and `esp_cache_msync` for PSRAM overlay regions on ESP32-S3.

`MALLOC_CAP_EXEC` only exists when the exec heap is enabled
(`CONFIG_HEAP_HAS_EXEC_HEAP`, on by default when `ESP_SYSTEM_MEMPROT` is off).
Since the runtime executes out of the memory it allocates, the port `#error`s at
compile time if the exec heap is missing rather than failing at runtime.

## Packaging

Two build systems are supported.

**PlatformIO.** `library.json` registers it for the `espidf` and `arduino`
frameworks on `espressif32`. Depend on it in `platformio.ini` and add
`-DELF_OVERLAY_ESP_IDF=1` to the build flags.

**ESP-IDF component.** `idf_component.yml` plus the `ESP_PLATFORM` branch of the
root `CMakeLists.txt` register it (`REQUIRES elf_loader`, `PRIV_REQUIRES heap
esp_common log esp_mm`). The component manager pulls `elf_loader` from the
registry. Point `EXTRA_COMPONENT_DIRS` at the repo root, or add it through the
component manager.

## Examples

`examples/` holds runnable consumer examples that CI builds too. Each has its
own README.

- `host_pipeline` - one C function taken from source through `relink` -> `emit`
  -> load -> `enter`/`leave` on the host. `make check` round-trips it against the
  committed vectors.
- `esp_idf_component` - a minimal ESP-IDF project that pulls in `elf_overlay` as
  a component; the `device-build` CI job builds it.

## Building

### Host (tests + tools)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

CMake options:

| Option                        | Default | Effect                                          |
| ----------------------------- | ------- | ----------------------------------------------- |
| `ELF_OVERLAY_BUILD_TESTS`.    | `ON`    | Build the unit/host test executables            |
| `ELF_OVERLAY_BUILD_TOOLS`     | `ON`    | Build `elf_overlay_relink` / `elf_overlay_emit` |
| `ELF_OVERLAY_ESP_IDF`         | `OFF`   | Build the ESP-IDF port + esp_elf integration    |
| `ELF_OVERLAY_ESP_ELF_INCLUDE` | `""`    | Path to `esp_elf.h` parent dir (standalone)     |
| `ELF_OVERLAY_WERROR`          | `OFF`   | Promote warnings to errors (CI)                 |
| `ELF_OVERLAY_ASAN`            | `OFF`   | AddressSanitizer + UBSan (host only)            |

### Device (ESP-IDF)

Build it as a component from the minimal project used by CI:

```bash
idf.py -C examples/esp_idf_component \
  -DELF_LOADER_DIR=/path/to/elf_loader set-target esp32s3 build
```

Drop `ELF_LOADER_DIR` to let the component manager fetch `elf_loader` from the
registry.

### Runtime limits

Runtime sizes are `#ifndef`-guarded constants in
`include/elf_overlay/config.h`, so a consumer can override any of them with a
`-D` flag instead of editing the header.

| Define                           | Default | Meaning                           |
| -------------------------------- | ------- | --------------------------------- |
| `ELF_OVERLAY_MAX_FRAMES`         | `4`     | Executable-RAM frames in the pool |
| `ELF_OVERLAY_DEFAULT_FRAME_SIZE` | `16384` | Bytes per frame                   |
| `ELF_OVERLAY_MAX_SECTIONS`       | `8`     | Section-map entries               |

`ctest --test-dir build` runs the host test suite. See `CONTRIBUTING.md` for
the full build, test, and formatting workflow, and `CHANGELOG.md` for the
release history.

## License

Apache-2.0. See the `LICENSE` file.
