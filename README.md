# elf_overlay

## Description

`elf_overlay` is an ISA-abstracted ELF overlay runtime for embedded systems. It runs alongside an ELF loader (for example Espressif's `elf_loader`) and pages overlay bodies in and out of executable RAM, applying ISA-specific relocations at load time. On Xtensa this is `l32r` PC-relative literal patching; on RISC-V it is `auipc`.

The runtime makes no assumptions about the target. It does not depend on ESP-IDF, on any particular operating system, or on an RTOS. A small platform port layer (`port.h`) lets it run on any system with a C compiler and a file system, and the ESP-IDF port is one implementation of that layer.

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
(`include/elf_overlay/isa.h`). Two ISAs are registered: `xtensa` performs
`l32r` PC-relative literal patching (verified against the Xtensa ISA reference),
and `riscv` performs `auipc` PC-relative literal patching. To add another ISA,
implement `elf_overlay_isa_ops_t` and call `elf_overlay_isa_register()` from an
init function.

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

The runtime keeps a fixed number of executable-RAM frames and pages overlays in
on demand. `enter()` increments a frame's reference count and `leave()`
decrements it; a frame whose count is above zero is never evicted. Compiler
emitted literals are patched in place within the `.text` image at page-in time,
once the resident addresses are known, while the PC-relative loads themselves
(`l32r` / `auipc`) are left untouched. Tables are fixed-size, so there is no heap
churn during page-in.

## Hooking into elf_loader

`elf_loader` loads and relocates the resident application ELF; `elf_overlay`
manages the overlay bodies on top of it. The integration layer
(`include/elf_overlay/esp_elf_integration.h`,
`src/core/esp_elf_integration.c`) provides three call sites.

After `esp_elf_relocate()` returns, call
`elf_overlay_esp_elf_set_section_map()` to copy `esp_elf_t.sec[]` (canonical
virtual address to resident address) into the overlay section map. Before the
application entry point runs, call `elf_overlay_esp_elf_apply_redirects()` to
patch the redirect sites in resident memory so that paged functions are reached
through their stubs. At init, call
`elf_overlay_esp_elf_install_symbol_resolver()` so host references resolve
through `esp_elf_find_symbol()`.

A typical flow looks like this:

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

The ESP-IDF branch of `src/port/port.c` (guarded by `ELF_OVERLAY_ESP_IDF`)
implements `port.h` using ESP-IDF APIs (`heap_caps_malloc` with
`MALLOC_CAP_EXEC`, `ESP_LOG*`, stdio VFS, `esp_cache_msync` for PSRAM overlay
regions on ESP32-S3). `MALLOC_CAP_EXEC` only exists when the IDF exec heap is
enabled (`CONFIG_HEAP_HAS_EXEC_HEAP`, which defaults on when
`ESP_SYSTEM_MEMPROT` is off); the port fails to compile with a clear message
otherwise, since the overlay runtime jumps into the memory it allocates.

## Packaging

`elf_overlay` ships for two embedded build systems.

Under PlatformIO, `library.json` registers it as a library for the `espidf` and
`arduino` frameworks on `espressif32`. Add a dependency on it in
`platformio.ini` and add `-DELF_OVERLAY_ESP_IDF=1` to the build flags.

As an ESP-IDF component, `idf_component.yml` together with the `ESP_PLATFORM`
branch in the root `CMakeLists.txt` register it with `REQUIRES elf_loader` and
`PRIV_REQUIRES heap esp_common log esp_mm`. The `elf_loader` dependency is pulled
from the component registry by the component manager. Point
`EXTRA_COMPONENT_DIRS` at the repository root, or add it with the component
manager.

## Examples

The `examples/` directory holds runnable consumer examples that CI also builds.
`examples/host_pipeline` takes a single C function from source through
`relink`, `emit`, load, and `enter` / `leave` on the host; its `make check`
target round-trips the result against the committed test vectors.
`examples/esp_idf_component` is a minimal ESP-IDF project that consumes
`elf_overlay` as a component and is built by the `device-build` CI job. Each
example has its own README.

## Building

### Host (tests + tools)

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

CMake options:

| Option                     | Default | Effect                                        |
| -------------------------- | ------- | --------------------------------------------- |
| `ELF_OVERLAY_BUILD_TESTS`  | `ON`    | Build the unit/host test executables          |
| `ELF_OVERLAY_BUILD_TOOLS`  | `ON`    | Build `elf_overlay_relink` / `elf_overlay_emit` |
| `ELF_OVERLAY_ESP_IDF`      | `OFF`   | Build the ESP-IDF port + esp_elf integration  |
| `ELF_OVERLAY_ESP_ELF_INCLUDE` | `""` | Path to `esp_elf.h` parent dir (standalone)   |
| `ELF_OVERLAY_WERROR`       | `OFF`   | Promote warnings to errors (CI)               |
| `ELF_OVERLAY_ASAN`         | `OFF`   | AddressSanitizer + UBSan (host only)          |

### Device (ESP-IDF)

Build it as a component from the minimal project used by CI:

```bash
idf.py -C examples/esp_idf_component \
  -DELF_LOADER_DIR=/path/to/elf_loader set-target esp32s3 build
```

Omit `ELF_LOADER_DIR` to let the component manager fetch `elf_loader` from the
registry.

### Runtime limits

The runtime sizes are compile-time constants in
`include/elf_overlay/config.h`, each guarded by `#ifndef` so a consumer can
override it with a `-D` flag without editing the header.

| Define                           | Default | Meaning                              |
| -------------------------------- | ------- | ------------------------------------ |
| `ELF_OVERLAY_MAX_FRAMES`         | `4`     | Executable-RAM frames in the pool    |
| `ELF_OVERLAY_DEFAULT_FRAME_SIZE` | `16384` | Bytes per frame                      |
| `ELF_OVERLAY_MAX_SECTIONS`       | `8`     | Section-map entries                  |

## Testing

`ctest` runs seven tests covering ISA registration, blob validation, `l32r`
encoding, manifest round-trip, host blob load, redirect wiring, and host-ref
resolution (including the unresolved-symbol abort). Run the suite under
AddressSanitizer and UBSan by configuring with `-DELF_OVERLAY_ASAN=ON`.

`examples/host_pipeline` holds a real Xtensa C fixture compiled with
`xtensa-esp32s3-elf-gcc` and processed end to end through `relink`, `emit`,
`elf_overlay_load`, and `enter` / `leave`. Its `Makefile` regenerates the
committed `.elf`, `.o`, `.relk`, and `.elfo` vectors from source, and
`make check` fails if the current tools drift from the committed blobs.

## Formatting

`.clang-format` encodes the in-tree style (LLVM base, 4-space indent, 100-col,
`type *name` pointers, attached braces). Format the tree with:

```bash
find src tools include tests \( -name '*.c' -o -name '*.h' \) \
  -print0 | xargs -0 clang-format -i
```

## Continuous integration

`.github/workflows/ci.yml` runs five jobs on push and pull request. The `build`
job runs the host build and `ctest` with `-Werror`. The `sanitizers` job runs
the same suite under AddressSanitizer and UBSan. The `format` job runs
`clang-format --dry-run --Werror` over the tree. The `fixtures` job regenerates
the C fixture and verifies the committed blobs still match
(`make -C examples/host_pipeline check`). The `device-build` job builds
`elf_overlay` as an ESP-IDF component for ESP32-S3.

## Contributing

See `CONTRIBUTING.md` for the build, test, and formatting steps expected before
a pull request. User-visible changes are recorded in `CHANGELOG.md`.

## License

Apache-2.0. See the `LICENSE` file.
