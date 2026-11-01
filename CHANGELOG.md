# ChangeLog

## Unreleased

### Fixed

- relink: drive host_ref emission from the relocation table so GOT slots whose
  `l32r` load the linear scan fails to decode still get patched. A missed slot
  was left at its link-time value and faulted on first call.

## v1.0.0 - 2026-10-30

First release. `elf_overlay` is an ISA-abstracted ELF overlay runtime for
embedded systems. It runs alongside an ELF loader (for example Espressif's
`elf_loader`) and pages overlay bodies in and out of executable RAM, patching
compiler-emitted literals in place once the resident addresses are known. The
runtime makes no assumptions about the operating system or RTOS; a small
platform port layer (`port.h`) is all a new host needs. Created by NellowTCS and
maintained by NellowTCS and Ashtf under the TalismanDesign organization.

### Runtime

- Load and unload overlay blobs (`elf_overlay_load` / `elf_overlay_unload`) and
  page overlay bodies on demand through position-independent enter / leave stubs
  (`elf_overlay_enter` / `elf_overlay_leave`).
- Manage executable RAM with a fixed-size frame pool and per-frame reference
  counting: `enter()` increments and `leave()` decrements, and a frame whose
  count is above zero is never evicted.
- Patch compiler-emitted literals in place within the `.text` image at page-in
  time, leaving the PC-relative loads themselves (`l32r` / `auipc`) untouched.
- Translate canonical virtual addresses to resident addresses through a section
  map (`elf_overlay_set_section_map` / `elf_overlay_resolve`), and patch resident
  application code to reach paged functions through their stubs
  (`elf_overlay_apply_redirects`).
- Resolve host references through a caller-supplied symbol resolver
  (`elf_overlay_set_symbol_resolver`), aborting with a clear message on an
  unresolved host symbol.
- Use fixed-size tables throughout, so there is no heap churn during page-in.
  The frame count, frame size, and section-map capacity are compile-time
  constants in `include/elf_overlay/config.h`, each overridable with a `-D` flag.

### Formats

- Define the ELFO v1 blob format: magic `ELFO`, a 46-byte packed header carrying
  the format version and an ISA tag, followed by overlay, stub, redirect, patch,
  stub-patch, and host-ref tables, a string table, the stub block, and the
  verbatim overlay bodies.
- Define the intermediate RELK format (magic `RELK`) produced by the relinker and
  consumed by the emitter.

### ISA abstraction

- Route all ISA-specific work through `elf_overlay_isa_ops_t`
  (`include/elf_overlay/isa.h`). A new ISA is added by implementing the ops table
  and calling `elf_overlay_isa_register()`.
- Ship two backends: Xtensa `l32r` PC-relative literal patching (verified against
  the Xtensa ISA reference) and RISC-V `auipc` PC-relative literal patching. ARM
  is reserved for future work.

### Host tools

- `elf_overlay_relink` reads an ELF32 executable, scans `.text` for ISA-specific
  patch sites, builds a literal pool, classifies relocations, and writes a RELK
  intermediate.
- `elf_overlay_emit` packs a RELK intermediate into an ELFO blob ready for the
  runtime loader. The packing engine lives in the library so the tools stay
  thin; both tools are compiled C with no scripting runtime.

### ESP-IDF integration

- Provide an integration layer that copies `esp_elf_t` section addresses into the
  overlay section map (`elf_overlay_esp_elf_set_section_map`), applies redirect
  sites in resident memory (`elf_overlay_esp_elf_apply_redirects`), and resolves
  host references through `esp_elf_find_symbol()`
  (`elf_overlay_esp_elf_install_symbol_resolver`).
- Implement the ESP-IDF port of `port.h`: `heap_caps_malloc` with
  `MALLOC_CAP_EXEC`, `ESP_LOG*`, the stdio VFS, and `esp_cache_msync` for PSRAM
  overlay regions on ESP32-S3. The port fails to compile with a clear message
  when the executable heap is unavailable, since the runtime jumps into the
  memory it allocates.

### Packaging and tooling

- Package the library for PlatformIO (`library.json`) and ESP-IDF
  (`idf_component.yml` plus the `ESP_PLATFORM` branch of the root
  `CMakeLists.txt`), pulling `elf_loader` from the component registry.
- Ship host unit and integration tests run under CTest, a Xtensa fixture
  round-trip (`examples/host_pipeline`), and a minimal ESP-IDF consumer project
  (`examples/esp_idf_component`), all exercised by CI.
- Add the CI workflow: host build with `-Werror`, AddressSanitizer and UBSan,
  `clang-format` enforcement, fixture round-trip verification, and an ESP32-S3
  component build.
