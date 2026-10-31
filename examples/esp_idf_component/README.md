## ESP-IDF component example

A minimal ESP-IDF project that consumes `elf_overlay` as a component. It shows
how a downstream firmware adds the library to its build, and it doubles as the
`device-build` CI job, which confirms that the on-device path compiles and links
for ESP32-S3.

`main/main.c` only references the public API so the linker pulls the component
in. It is not flashed or run; the example checks the build, not runtime
behavior.

### Layout

```
esp_idf_component/
|-- CMakeLists.txt        project; adds elf_overlay (and optionally elf_loader)
|                         to EXTRA_COMPONENT_DIRS
|-- sdkconfig.defaults    enables elf_loader and turns memory protection off so
|                         the exec heap (MALLOC_CAP_EXEC) is available
`-- main/
    |-- CMakeLists.txt    REQUIRES elf_overlay
    `-- main.c            app_main() that references the overlay API
```

### Build

With a local checkout of `elf_loader`:

```bash
idf.py -C . \
  -DELF_LOADER_DIR=/path/to/elf_loader set-target esp32s3 build
```

The project passes `ELF_OVERLAY_DIR` (the repository root, two levels up) and
`ELF_LOADER_DIR` through `EXTRA_COMPONENT_DIRS`. Omit `ELF_LOADER_DIR` to let the
component manager fetch `elf_loader` from the registry through
`idf_component.yml`.

### Consuming elf_overlay in your own project

Add `elf_overlay` to your component search path, either vendored under
`components/`, as a submodule, or fetched through the component manager, and list
it in the consuming component's `REQUIRES`:

```cmake
idf_component_register(SRCS "app.c" REQUIRES elf_overlay)
```

The overlay runtime jumps into the memory it allocates, so an executable heap
must exist. On targets without memory protection this is the default; otherwise
set `CONFIG_ESP_SYSTEM_MEMPROT_FEATURE=n`, as `sdkconfig.defaults` does here.
